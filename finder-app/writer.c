/*
 * An alternative to the “writer.sh” test script created in assignment1 and using File IO as described in LSP chapter 2.
 * Additional instructions:
 * 1. One difference from the write.sh instructions in Assignment 1:
 *    You do not need to make your "writer" utility create directories which do not exist.  You can assume the directory is created by the caller.
 * 2. Setup syslog logging for your utility using the LOG_USER facility.
 * 3. Use the syslog capability to write a message “Writing <string> to <file>” 
 * 	where <string> is the text string written to file (second argument) 
 * 	and <file> is the file created by the script.  This should be written with LOG_DEBUG level.
 * 4. Use the syslog capability to log any unexpected errors with LOG_ERR level.
 */

#include<stdio.h>
#include<string.h>
#include<sys/types.h>
#include<sys/stat.h>
#include<fcntl.h>
#include<unistd.h>
#include<syslog.h>
#include<errno.h>

int main(int argc, char *argv[])
{	 
	openlog(NULL, 0, LOG_USER);

	if (argc !=3)
	{
		syslog(LOG_WARNING, "Illegal number of arguments passed to the writer");
		fprintf(stderr, "Missing arguments! Proper syntax: ./writer <filename> <string>\n");
		closelog();
		return 1;
	}

	char *writefile = argv[1];
	char *writestr = argv[2];
	int fd;
	fd = open(writefile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd == -1)
	{
		fprintf(stderr, "File %s couldn't be created.\n", writefile);
		syslog(LOG_ERR, "File %s couldn't be created: %s", writefile, strerror(errno));
		closelog();
		return 1;
	}

	ssize_t nr;
	ssize_t count = strlen(writestr);
		
	syslog(LOG_DEBUG, "Writing %s to %s", writestr, count);

	while (count !=0 && (nr = write(fd, writestr, count) !=0))
	{
		if (nr == -1)
		{	
			if (errno == EINTR) continue;

			syslog(LOG_ERR, "Writing failed: %s", strerror(errno));
			closefd();
			closelog();
			break;
		}
		count -= nr;
		writestr += nr;

	}

	if (close(fd) == -1)
	{
		syslog(LOG_ERR, "Failed to close file: %s", writefile);
		closelog();
		return 1;
	}

	return 0;
}
