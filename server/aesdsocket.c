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
#include<netdb.h>
#include<syslog.h>
#include<signal.h>


#define PORT "9000"
#define BACKLOG 10	// no. of queued pending connections before refusal

// function that saves the current errno, clears all the zombie child processes and reverts errno to previous value
void sigchild_handler(int sig)
{	
	(void)s;					// supresses unused variable `s` warnings
	int parent_errno = errno;
	while(waitpid(-1, NULL, WNOHANG) > 0);		//-1 means wait for any child process; WNOHANG means don't block
	errno = parent_errno;
}

int main(int argc, char *argv[])
{	
	int sockfd;

	// first let's get addrinfo to bind the socket using getaddrinfo()
	struct addrinfo hints, *servinfo; 	//args for getaddrinfo(); *servinfo points to result
	struct addrinfo *p;			//p loops through all the linked-lists of addrinfo's
	
	memset(&hints, 0, sizeof(hints));	//making sure struct is empty
	hints.ai_flags = AI_PASSIVE;		// use my IP
	hints.ai_family = AF_UNSPEC;		// either IP family
	hints.ai_socktype = SOCK_STREAM;	// TCP type
	
	int yes = 1;				// for setsockopt's *optval
	
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
			fprinf(stderr, "socket options couldn't be set: %s\n", strerror(errno));
			close(sockfd);
			return -1;	
		}

		// now if no errors on creating socket file descriptor, we bind the address to socket
		if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1)
		{
			close(sockfd);
			fprinf(stderr, "socket couldn't be bound to the %s address: %s\n", pi->ai_addr, strerror(errno));
			continue;		// we try to bind new address from next servinfo to the socket
		}
		
		fprintf(stdout, "Socket of type %s was successfully created and bound to the address: %s\n", p->ai_socktype, p->ai_addr);
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
	
	struct sigaction sa;		//struct that stores the sa_handler, sa_mask and others for handling zombie child processes
	
	// now before accepting new connection, we have to remove all zombie child processes	
	sa.sa_handler = &sigchild_handler;	//pointer is passed; & is unneeded as C implicitly assigns fucntion pointer if no paranthesis passed
	sigemptyset(&sa.sa_mask);		// initializes the signalset `sa.sa_mask` to empty; so no signal gets blocked except SIGCHILD
	sa.sa_flags = SA_RESTART;		// restarts accept() syscall after SIGCHILD interrupts and is handled by sigchild_handler()	
	
	if(sigaction(SIGCHILD, &sa, NULL) == -1)
	{
		fprintf(stderr, "Sigaction failed to kill all the terminated child processes:%s\n", strerror(errno));
		return -1;
	}

	// Now we start accepting connections
	printf("Waiting for connections...");
	
	int new_sockfd;			// new_sockfd for new accepted socket connection; different from default listening sockfd
	struct sockaddr_storage incoming_addr;
	socklen_t size_inaddr = sizeof(incoming_addr);
	char host[NI_MAXHOST];		// NI_MAXHOST and NI_MAXSERV are set from <netdb.h>
	char service[NI_MAXSERV];
	
	openlog("server", LOG_PID | LOG_NDELAY, LOG_USER);

	while (1)
	{
		new_sockfd = accept(sockfd, (struct sockaddr *)&incoming_addr, &size_inaddr);

		if (new_sockfd == -1)
		{
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
	}

	closelog();
	return 0;
}
