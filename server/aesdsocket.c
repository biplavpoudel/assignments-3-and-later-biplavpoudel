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
#include<syslog.h>
#include<signal.h>


#define PORT 9000
#define BACKLOG 10	// no. of pending connections before refusal

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
	
	// struct servinfo exhausted. no socket bounded to address!	
	if (p == NULL)
	{
		fprintf(stderr, "Server socket failed to bind!");
	}

	//now we listen for incoming connection
		



	return 0;
}
