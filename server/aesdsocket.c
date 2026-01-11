/*
 * socket-based program that opens stream socket bounded to port 9000
 * returns -1 on failure
 * listens/accepts connection
 * logs syslog as: 'Accepted conenction from xxxx'
 * receives data and appends to '/var/tmp/aesdsocketdata'; creates if doesn't exist
 * returns full content of '/var/tmp/aesdsocketdata' to client 
 * logs syslog as: 'Closed connection from xxxx'
 * restarts connection in a loop until SIGINT and SIGTERM
 * completes any open connections, closes open sockets and delete the /var/tmp/aesdsocketdata'
 * then, logs syslog as: 'Caught signal, exiting'
*/

#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include<errno.h>
#include<string.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<sys/wait.h>
#include<netdb.h>
#include<syslog.h>
#include<signal.h>
#include<fcntl.h>


#define PORT "9000"
#define BACKLOG 10	// no. of queued pending connections before refusal

#define CHUNK_SIZE 4096		// no. of bytes we can read/write at once

// we need a global variable that is read/write atomic to inform `accept loop` of execution termination
// also we need to inform compiler that the variable can change outside of the normal flow of code
// like through signal interrupts; so compiler never caches this into register and reloads it time-to-time
static volatile sig_atomic_t exit_requested = 0;

// function that saves the current errno, clears all the zombie child processes and reverts errno to previous value
void sigchild_handler(int sig)
{	
	(void)sig;					// supresses unused variable `sig` warnings
	int parent_errno = errno;
	pid_t pid;
	while(pid = waitpid(-1, NULL, WNOHANG)) > 0){	//-1 means wait for any child process; WNOHANG means don't block
		printf("Child process %d reaped\n", pid);
	}
	if (pid = -1 && errno != ECHILD){			// errno due to no child should be safely ignored
		perror("waitpid for child gave error");
	}
	errno = parent_errno;
}

// function to handle graceful termination of socket server
void handle_server_termination(int sig)
{	
	(void)sig;
	exit_requested = 1;
}

int main()
{	
	int sockfd;
	char *packet_file = "/var/tmp/aesdsocketdata";

	// first let's get addrinfo to bind the socket using getaddrinfo()
	struct addrinfo hints, *servinfo; 	//args for getaddrinfo(); *servinfo points to result
	struct addrinfo *p;			//p loops through all the linked-lists of addrinfo's
	
	memset(&hints, 0, sizeof(hints));	//making sure struct is empty
	hints.ai_flags = AI_PASSIVE;		// use my IP
	hints.ai_family = AF_UNSPEC;		// either IP family
	hints.ai_socktype = SOCK_STREAM;	// TCP type
	
	int yes = 1;				// for setsockopt's *optval
	int status;
	
	if ((status = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0)
	{
		fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
		return -1;
	}

	// now servinfo points to a linked-list of 1 or more addrinfo's
	// to loop through all the resulting addrinfo's and bind to the first we can
	for (p = servinfo; p != NULL; p = p->ai_next)
	{	
		// first we create socket endpoint connection
		if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1)
		{	
			fprintf(stderr, "socket create failed: %s\n", strerror(errno));
			continue;	//try again to create socket with another struct servinfo in next loop
		}
		
		// now we set socket options at socket API level
		// SO_REUSEADDR relaxes addr/port reuse at bind time
		// i.e. to avoid 'Address already in use' bind error upon restart, when the address in TIME_WAIT is already free
		if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1)
		{
			fprintf(stderr, "socket options couldn't be set: %s\n", strerror(errno));
			close(sockfd);
			return -1;	
		}

		// now if no errors on creating socket file descriptor, we bind the address to socket
		if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1)
		{
			close(sockfd);
			fprintf(stderr, "socket couldn't be bound: %s\n", strerror(errno));
			continue;		// we try to bind new address from next servinfo to the socket
		}
		
		// if success, we try to get the socket information using getsockname()
		struct sockaddr_storage sa;
    	socklen_t sa_len = sizeof(sa);

    	if (getsockname(sockfd, (struct sockaddr *)&sa, &sa_len) == 0) {
        	char host[NI_MAXHOST];
        	char service[NI_MAXSERV];

        	if (getnameinfo((struct sockaddr *)&sa, sa_len, host, sizeof(host), service, sizeof(service), NI_NUMERICHOST | NI_NUMERICSERV) == 0) {
        	    printf("Server successfully bound to %s:%s\n", host, service);
        	}
    	}	

		break;		// if successfully bound, we break from the loop
	}
	
	freeaddrinfo(servinfo);		// freeing servinfo to avoid memory leak
		
	// struct servinfo exhausted. no socket bounded to address!	
	if (p == NULL)
	{
		fprintf(stderr, "Server socket failed to bind!");
		return -1;
	}

	//now we listen for incoming connection
	if (listen(sockfd, BACKLOG) == -1)
	{
		fprintf(stderr, "Listening on socket refused: %s\n", strerror(errno));
		return -1;
	}	
	
	struct sigaction purge;		//struct that stores the sa_handler, sa_mask and others for handling zombie child processes
	
	// now before accepting new connection, we have to remove all zombie child processes	
	purge.sa_handler = &sigchild_handler;	//pointer is passed; & is unneeded as C implicitly assigns fucntion pointer if no paranthesis passed
	sigemptyset(&purge.sa_mask);		// initializes the signalset `sa.sa_mask` to empty; so no signal gets blocked except SIGCHLD
	purge.sa_flags = SA_RESTART;		// restarts accept() syscall after SIGCHLD interrupts and is handled by sigchild_handler()	
	
	if(sigaction(SIGCHLD, &purge, NULL) == -1)
	{
		fprintf(stderr, "Sigaction failed to kill all the terminated child processes:%s\n", strerror(errno));
		return -1;
	}

	// Now we start accepting connections
	printf("Waiting for connections...\n");
	
	int new_sockfd;			// new_sockfd for new accepted socket connection; different from default listening sockfd
	struct sockaddr_storage incoming_addr;
	socklen_t size_inaddr = sizeof(incoming_addr);
	char host[NI_MAXHOST];		// NI_MAXHOST and NI_MAXSERV are set from <netdb.h>
	char service[NI_MAXSERV];
	
	openlog("server", LOG_PID | LOG_NDELAY, LOG_USER);
	
	// sigaction handling in the case of SIGINT or SIGTERM
	struct sigaction grace_term;
	memset(&grace_term, 0, sizeof grace_term);
	grace_term.sa_handler = handle_server_termination;
	sigemptyset(&grace_term.sa_mask);
	grace_term.sa_flags = 0;	// restart should be avoided in case of SIGINT or SIGTERM
	
	if (sigaction(SIGINT, &grace_term, NULL) == -1) 
	{
		fprintf(stderr, "sigaction(SIGINT) failed: %s\n", strerror(errno));
		return -1;
	}

	if (sigaction(SIGTERM, &grace_term, NULL) == -1) 
	{
		fprintf(stderr, "sigaction(SIGTERM) failed: %s\n", strerror(errno));
		return -1;
	}
	

	while (!exit_requested)
	{
		new_sockfd = accept(sockfd, (struct sockaddr *)&incoming_addr, &size_inaddr);

		if (new_sockfd == -1){
			if (errno == EINTR && exit_requested) break;			// if -1 is due to SIGINT/SIGTERM, break out of the accept loop; else try again for connection
			fprintf(stderr, "Socket connection refused: %s\n", strerror(errno));
			continue;
		}

		// if connection was established, we log the client information
		int rc = getnameinfo((struct sockaddr *)&incoming_addr, size_inaddr,
			       	host, sizeof(host), service, sizeof(service),
			       	NI_NUMERICHOST | NI_NUMERICSERV);	
		if (rc == 0)
		{	
			printf("Server connected with client %s:%s\n", host, service);
			syslog(LOG_INFO, "Accepted connection from %s\n", host);
		}
		else
			fprintf(stderr, "Client information couldn't be determined");
		
		pid_t pid = fork();
		
		if (pid == 0)
		{
			close(sockfd);		// close child process's inherited sockfd

			//now we read packets from the client and append them in `/var/tmp/aesdsocketdata`
			//newline is used to separate packets received

			char *recv_buffer = NULL;	// buffer is dynamically allocated as packets are read from client
			size_t buffer_size = 0;

			char temp[CHUNK_SIZE];
			ssize_t bytes_read;

			while((bytes_read = recv(new_sockfd, temp, CHUNK_SIZE, 0)) > 0) 	// return value of 0 means end-of-file
			{
				char *new_buffer = realloc(recv_buffer, buffer_size + bytes_read);	// resize recv_buffer based on bytes_read
				if (!new_buffer)
				{
					perror("realloc to recv_buffer failed");
					free(recv_buffer);
					close(new_sockfd);
					break;
				}
				recv_buffer = new_buffer;		//passing ptr from newly allocated memory
				memcpy(recv_buffer + buffer_size, temp, bytes_read);		// copies `bytes_read` bytes to recv_buffer
				buffer_size += bytes_read;		//updating offset for recv_buffer
				
				// now we check for end of packet inside recv_buffer by looking for newline
				for (size_t i = 0; i < buffer_size; i++)
				{
					if (recv_buffer[i] == '\n'){
						ssize_t packet_len = i + 1;			// extra 1 for `\n`

						//appending packet of size `packetlen`
						int fd = open(packet_file, O_CREAT | O_RDWR | O_APPEND, 0644);
						if (fd == -1){
							perror("file /var/tmp/aesdsocketdata couldn't be either created or appended");
							close(new_sockfd);
							free(recv_buffer);
							return -1;
						}
						// considering partial writes
						ssize_t total_written = 0;
						while (total_written < packet_len){
							ssize_t nr = write(fd, recv_buffer + total_written, packet_len - total_written);
							if (nr == -1){
								if (errno == EINTR) continue;
								perror("write failed");
								close(fd);
								close(new_sockfd);
								free(recv_buffer);
								return -1;
							}
							total_written += nr;
						}
						if (close(fd) == -1)
						{
							perror("file close failed");
							return -1;
						}
						
						// after each successful packet read/append, we now send back the full file to client
						fd = open(packet_file, O_RDONLY);
						if (fd == -1){
							perror("file opening failed for read");
							return -1;
						}

						char buf[CHUNK_SIZE];
						while((ssize_t nr = read(fd, buf, CHUNK_SIZE)) > 0){
							ssize_t total_sent = 0;
							// accounting for partial read from the buffer
							while(total_sent < nr){
								ssize_t ns = send(new_sockfd, buf + total_sent, nr - total_sent, 0);
								
								if (ns == -1){
									if (errno == EINTR)	continue;
									perror("sending failed");
									close(fd);
									close(new_sockfd);
									return -1;
								}
								total_sent += ns;
							}
						}
						if (nr == 0)	break;			//file transfer completed
						else if (nr == -1){
							if (errno == EINTR)	continue;
							perror("failed to read from file");
						}
						if (close(fd) == -1)
                        {
                            perror("file close failed");
							return -1;
                    	}

						//now we remove processed packet out of the buffer
						size_t remaining_packets = buffer_size - packet_len;
						memmove(recv_buffer, recv_buffer + packet_len, remaining_packets);	// replace the recv_buffer with remianing data
						buffer_size = remaining_packets;
					
						// restart scan for newline from beginning of remaining packets
						i = -1; 

					}
				}
			}
			close(new_sockfd);
			free(recv_buffer);
			syslog(LOG_INFO, "Closed connection from %s\n", host);
			_exit(0);	//exiting out of child immediately
		}
		else if (pid > 0)	
			close(new_sockfd);
		else
		{
			fprintf(stderr, "Child process couldn't be forked: %s\n", strerror(errno));
			close(new_sockfd);
		}
	}

	syslog(LOG_INFO, "Caught signal, exiting");

	close(sockfd);
	unlink("/var/tmp/aesdsocketdata");

	closelog();

	return 0;
}
