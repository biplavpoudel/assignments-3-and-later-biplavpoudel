/*
 * socket-based program that opens stream socket bounded to port 9000
 * returns -1 on failure
 * listens/accepts connection
 * logs syslog as: 'Accepted conenction from xxxx'
 * receives data and appends to '/var/tmp/aesdsocketdata'; creates if it doesn't exist
 * returns full content of '/var/tmp/aesdsocketdata' to client
 * logs syslog as: 'Closed connection from xxxx'
 * restarts connection in a loop until SIGINT and SIGTERM
 * completes any open connections, closes open sockets and delete the /var/tmp/aesdsocketdata'
 * then, logs syslog as: 'Caught signal, exiting'
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <err.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netdb.h>
#include <syslog.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/file.h>
#include <stdbool.h>
#include <pthread.h>
#include "queue.h" //FreeBSD 10 based; thread-safe

#define PORT "9000"
#define BACKLOG 10					  // no. of queued pending connections before refusal
#define CHUNK_SIZE 4096				  // no. of bytes, we can read/write at once
#define PIDFILE "/tmp/aesdsocket.pid" // pid file to store pid of aesdsocket daemon;
static char *packet_file = "/var/tmp/aesdsocketdata";

// we need a global variable that is read/write atomic to inform `accept loop` of execution termination
// also we need to inform compiler that the variable can change outside of the normal flow of code
// like through signal interrupts; so compiler never caches this into register and reloads it time-to-time
static volatile sig_atomic_t exit_requested = 0;


// separate cleanup for child process for socket connection
void thread_cleanup(int sockfd, char *recv_buffer, int file_fd, const char *host)
{
	if (file_fd != -1)
		close(file_fd);
	if (sockfd != -1)
		close(sockfd);
	if (recv_buffer)
		free(recv_buffer);
	if (host)
		syslog(LOG_INFO, "Closed connection from %s", host);
	pthread_exit(NULL); // exits safely
}

// write pid to "/var/run/aesdsocket.pid"
static int write_pidfile()
{
	int fd = open(PIDFILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
	{
		syslog(LOG_ERR, "Failed to open pidfile: %s", strerror(errno));
		return -1;
	}

	char buf[32];
	int pid_len = snprintf(buf, sizeof(buf), "%d\n", getpid());
	if ((write(fd, buf, pid_len)) == -1)
	{
		syslog(LOG_ERR, "Failed to write to pidfile: %s", strerror(errno));
		close(fd);
		return -1;
	}
	close(fd);
	return 0;
}

// function to handle graceful termination of socket server
void handle_server_termination(int sig)
{
	(void)sig;
	exit_requested = 1;
}

// struct for each worker thread after accepting incoming socket connection
typedef struct thread_node
{
	pthread_t thread_id; // ID as returned by pthread_create()
	// int thread_num;				// Index of worker threads
	int client_fd; // file descriptor for client socket
	struct sockaddr_storage client_addr;
	socklen_t addr_len;
	char host[NI_MAXHOST]; // NI_MAXHOST and NI_MAXSERV are set from <netdb.h>
	char service[NI_MAXSERV];
	bool completed; // sets to true if the thread completes
	TAILQ_ENTRY(thread_node)
	nodes; // the TAILQ_ENTRY macro uses the struct name
} node_t;

/* this creates a head_t that makes it easy for us to pass pointers to
head_t without the compiler complaining. */
typedef TAILQ_HEAD(head_s, thread_node) head_t;

// to free up the linked list of worker threads
static void _free_queue(head_t *head)
{
	struct thread_node *e = NULL;
	while (!TAILQ_EMPTY(head))
	{
		e = TAILQ_FIRST(head);
		pthread_join(e->thread_id, NULL);
		TAILQ_REMOVE(head, e, nodes);
		free(e);
		e = NULL;
	}
}

// remove completed worker_threads
static void remove_completed_threads(head_t *head)
{
	struct thread_node *e = NULL;
	struct thread_node *next = NULL;
	TAILQ_FOREACH_SAFE(e, head, nodes, next)
	{
		if (e->completed)
		{
			pthread_join(e->thread_id, NULL);
			TAILQ_REMOVE(head, e, nodes);
			free(e);
		}
	}
}

void *readWriteSocket(void *arg)
{
	node_t *worker = arg;
	int client_fd = worker->client_fd;
	// REST OF THE PROCESSING

	// if connection was established, we log the client information
	int rc = getnameinfo((struct sockaddr *)&worker->client_addr, worker->addr_len,
						 worker->host, sizeof(worker->host), worker->service, sizeof(worker->service),
						 NI_NUMERICHOST | NI_NUMERICSERV);

	if (rc == 0) syslog(LOG_INFO, "Accepted connection from %s", worker->host);
	else syslog(LOG_WARNING, "Client information couldn't be determined");

	int fd = -1;

	// now we read packets from the client and append them in `/var/tmp/aesdsocketdata`
	// newline is used to separate packets received

	char *recv_buffer = NULL; // buffer is dynamically allocated as packets are read from client
	size_t buffer_size = 0;

	char temp[CHUNK_SIZE];
	ssize_t bytes_read;

	while ((bytes_read = recv(client_fd, temp, CHUNK_SIZE, 0)) > 0) // return value of 0 means end-of-file
	{
		char *new_buffer = realloc(recv_buffer, buffer_size + bytes_read); // resize recv_buffer based on bytes_read
		if (!new_buffer)
		{
			perror("realloc to recv_buffer failed");
			thread_cleanup(client_fd, recv_buffer, fd, worker->host);
		}
		recv_buffer = new_buffer;							 // passing ptr from newly allocated memory
		memcpy(recv_buffer + buffer_size, temp, bytes_read); // copies `bytes_read` bytes to recv_buffer
		buffer_size += bytes_read;							 // updating offset for recv_buffer

		// now we check for end of packet inside recv_buffer by looking for newline
		for (size_t i = 0; i < buffer_size; i++)
		{
			if (recv_buffer[i] == '\n')
			{
				ssize_t packet_len = i + 1; // extra 1 for `\n`
				// appending packet of size `packetlen`
				int fd = open(packet_file, O_CREAT | O_RDWR | O_APPEND, 0644);
				if (fd == -1)
				{
					perror("file /var/tmp/aesdsocketdata couldn't be either created or appended");
					thread_cleanup(client_fd, recv_buffer, fd, worker->host);
				}
				flock(fd, LOCK_EX); // mutex lock for writing
				// considering partial writes
				ssize_t total_written = 0;
				while (total_written < packet_len)
				{
					ssize_t nr = write(fd, recv_buffer + total_written, packet_len - total_written);
					if (nr == -1)
					{
						if (errno == EINTR)
							continue;
						perror("write failed");
						thread_cleanup(client_fd, recv_buffer, fd, worker->host);
					}
					total_written += nr;
				}
				if (close(fd) == -1)
				{
					perror("file close failed");
					thread_cleanup(client_fd, recv_buffer, fd, worker->host);
				}
				flock(fd, LOCK_UN); // unlock mutex

				// after each successful packet read/append, we now send back the full file to client
				fd = open(packet_file, O_RDONLY);
				if (fd == -1)
				{
					perror("file opening failed for read");
					thread_cleanup(client_fd, recv_buffer, fd, worker->host);
				}
				flock(fd, LOCK_SH); // shared mutex lock
				char buf[CHUNK_SIZE];
				ssize_t nr;
				while ((nr = read(fd, buf, CHUNK_SIZE)) > 0)
				{
					ssize_t total_sent = 0;
					// accounting for partial read from the buffer
					while (total_sent < nr)
					{
						ssize_t ns = send(client_fd, buf + total_sent, nr - total_sent, 0);

						if (ns == -1)
						{
							if (errno == EINTR)
								continue;
							perror("sending failed");
							thread_cleanup(client_fd, recv_buffer, fd, worker->host);
						}
						total_sent += ns;
					}
				}
				if (nr == 0)
					break; // file transfer completed
				else if (nr == -1)
				{
					if (errno == EINTR)
						continue;
					perror("failed to read from file");
					thread_cleanup(client_fd, recv_buffer, fd, worker->host);
				}
				flock(fd, LOCK_UN); // unlock mutex
				if (close(fd) == -1)
				{
					perror("file close failed");
					return NULL;
				}
				// now we remove processed packet out of the buffer
				size_t remaining_packets = buffer_size - packet_len;
				memmove(recv_buffer, recv_buffer + packet_len, remaining_packets); // replace the recv_buffer with remianing data
				buffer_size = remaining_packets;

				// restart scan for newline from beginning of remaining packets
				i = -1;
			}
		}
	}

	close(client_fd);
	free(recv_buffer);
	syslog(LOG_INFO, "Closed connection from %s", worker->host);
	worker->completed = true;
	return NULL;
}

int main(int argc, char *argv[])
{
	// we first check if this socket server runs as `normal process` or as `daemon` with "-d" argument
	int daemon_mode = 0;
	int opt;

	char host[NI_MAXHOST]; // NI_MAXHOST and NI_MAXSERV are set from <netdb.h>
	char service[NI_MAXSERV];

	// for linked list of worker threads
	head_t head;	   // declaring head
	TAILQ_INIT(&head); // initializing head of the queue

	// getopt returns each option one-by-one; if everything is parsed, it returns -1
	while ((opt = getopt(argc, argv, "d")) != -1)
	{
		switch (opt)
		{
		case 'd':
			daemon_mode = 1;
			printf("Sever running in daemon_mode = %d...\n", daemon_mode);
			break;
		case '?':
		default:
			fprintf(stderr, "Usage: %s [-d]\n", argv[0]);
			exit(EXIT_FAILURE);
		}
	}

	int listen_fd;

	// first let's get addrinfo to bind the socket using getaddrinfo()
	struct addrinfo hints, *servinfo; // args for getaddrinfo(); *servinfo points to result
	struct addrinfo *p;				  // p loops through all the linked-lists of addrinfo's

	memset(&hints, 0, sizeof(hints)); // making sure struct is empty
	hints.ai_flags = AI_PASSIVE;	  // use my IP
	hints.ai_family = AF_UNSPEC;	  // either IP family
	hints.ai_socktype = SOCK_STREAM;  // TCP type

	int yes = 1; // for setsockopt's *optval
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
		if ((listen_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1)
		{
			fprintf(stderr, "socket create failed: %s\n", strerror(errno));
			continue; // try again to create socket with another struct servinfo in next loop
		}

		// now we set socket options at socket API level
		// SO_REUSEADDR relaxes addr/port reuse at bind time
		// i.e. to avoid 'Address already in use' bind error upon restart, when the address in TIME_WAIT is already free
		if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1)
		{
			fprintf(stderr, "socket options couldn't be set: %s\n", strerror(errno));
			close(listen_fd);
			return -1;
		}

		// now if no errors on creating socket file descriptor, we bind the address to socket
		if (bind(listen_fd, p->ai_addr, p->ai_addrlen) == -1)
		{
			close(listen_fd);
			fprintf(stderr, "socket couldn't be bound: %s\n", strerror(errno));
			continue; // we try to bind new address from next servinfo to the socket
		}

		// if success, we try to get the socket information using getsockname()
		struct sockaddr_storage sa;
		socklen_t sa_len = sizeof(sa);

		if (getsockname(listen_fd, (struct sockaddr *)&sa, &sa_len) == 0)
		{
			char host[NI_MAXHOST];
			char service[NI_MAXSERV];

			if (getnameinfo((struct sockaddr *)&sa, sa_len, host, sizeof(host), service, sizeof(service), NI_NUMERICHOST | NI_NUMERICSERV) == 0)
			{
				printf("Server successfully bound to %s:%s\n", host, service);
			}
		}

		break; // if successfully bound, we break from the loop
	}

	freeaddrinfo(servinfo); // freeing servinfo to avoid memory leak

	// struct servinfo exhausted. no socket bounded to address!
	if (p == NULL)
	{
		fprintf(stderr, "Server socket failed to bind!\n");
		return -1;
	}

	// now we listen for incoming connection
	if (listen(listen_fd, BACKLOG) == -1)
	{
		fprintf(stderr, "Listening on socket refused: %s\n", strerror(errno));
		return -1;
	}

	// Now we start accepting connections
	printf("Waiting for connections...\n");

	int client_fd; // client_fd for new accepted socket connection; different from default listening listen_fd
	struct sockaddr_storage incoming_addr;
	socklen_t size_inaddr = sizeof(incoming_addr);

	openlog("server", LOG_PID | LOG_NDELAY, LOG_USER);

	// sigaction handling in the case of SIGINT or SIGTERM
	struct sigaction grace_term;
	memset(&grace_term, 0, sizeof grace_term);
	grace_term.sa_handler = handle_server_termination;
	sigemptyset(&grace_term.sa_mask);
	grace_term.sa_flags = 0; // restart should be avoided in case of SIGINT or SIGTERM

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

	/* Now we daemonize the process before accept loop by:
	 *	1. forking into a child,
	 * 	2. exiting the parent,
	 *	3. creating a new session,
	 *	4. exiting the child again, and
	 *	5. detaching the grandchild process
	 */
	if (daemon_mode)
	{
		// if pidfile already exists, that means aesdsocket is already running as daemon
		// if (access(PIDFILE, F_OK) == 0) {
		//	syslog(LOG_ERR, "Pidfile exists, daemon already running?");
		//	exit(EXIT_FAILURE);
		//}

		pid_t pid = fork();
		if (pid < 0)
		{
			perror("first fork");
			close(listen_fd);
			closelog();
			return -1;
		}
		else if (pid > 0)
			exit(EXIT_SUCCESS); // parent process exits; child is orphaned

		if (setsid() < 0)
		{ // runs the daemon process in a new session; detaches from the old controlling terminal
			perror("setsid");
			close(listen_fd);
			closelog();
			return -1;
		}

		// now we again fork the child process and orphan the grandchild process
		pid = fork();
		if (pid < 0)
		{
			perror("second fork");
			close(listen_fd);
			closelog();
			return -1;
		}
		else if (pid > 0)
			exit(EXIT_SUCCESS);

		// now grandchild is not the session leader of the newly created session; it is fully detached
		// now we change root directory to avoid blocking filesystem unmounts and redirect stdin/stdout/stderr
		if (chdir("/") != 0)
		{
			perror("chdir");
			exit(EXIT_FAILURE);
		}

		if (freopen("/dev/null", "r", stdin) == NULL)
		{
			perror("freopen stdin");
			exit(EXIT_FAILURE);
		}

		if (freopen("/dev/null", "w", stdout) == NULL)
		{
			perror("freopen stdout");
			exit(EXIT_FAILURE);
		}

		if (freopen("/dev/null", "w", stderr) == NULL)
		{
			perror("freopen stderr");
			exit(EXIT_FAILURE);
		}
		// now our process is a true daemon

		// now we write pidfile for start-stop init script
		if (write_pidfile() != 0)
		{
			syslog(LOG_ERR, "Failed to write to pidfile, exiting...");
			exit(EXIT_FAILURE);
		}
		syslog(LOG_INFO, "pid successfully written to %s", PIDFILE);
	}

	while (!exit_requested)
	{
		client_fd = accept(listen_fd, (struct sockaddr *)&incoming_addr, &size_inaddr);

		if (client_fd == -1)
		{
			if (errno == EINTR && exit_requested)
				break; // if -1 is due to SIGINT/SIGTERM, break out of the accept loop; else try again for connection
			syslog(LOG_ERR, "Socket connection refused: %s", strerror(errno));
			continue;
		}

		// Create worker node and add to queue
		node_t *worker_node = calloc(1, sizeof(*worker_node));
		if (worker_node == NULL)
		{
			close(client_fd);
			continue;
		}
		worker_node->client_fd = client_fd;
		memcpy(&worker_node->client_addr, &incoming_addr, size_inaddr);
		strncpy(worker_node->host, host, NI_MAXHOST);
		strncpy(worker_node->service, service, NI_MAXSERV);

		int s = pthread_create(&worker_node->thread_id, NULL, readWriteSocket, worker_node);
		if (s != 0)
		{
			close(listen_fd);
			unlink("/var/tmp/aesdsocketdata");
			closelog();
			errc(EXIT_FAILURE, s, "pthread_create");
		}
		TAILQ_INSERT_TAIL(&head, worker_node, nodes);
		remove_completed_threads(&head);
	}
	

	syslog(LOG_INFO, "Caught signal, exiting");
	_free_queue(&head);

	close(listen_fd);
	unlink("/var/tmp/aesdsocketdata");

	closelog();

	return 0;
}
