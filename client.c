#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <libgen.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>

int setNonBlocking(int fd)
{
	int oldopt, newopt;
	oldopt = fcntl(fd, F_GETFL);
	newopt = oldopt | O_NONBLOCK;
	fcntl(fd, F_SETFL, newopt);
	return oldopt;
}
int setBlocking(int fd)
{
	int oldopt, newopt;
	oldopt = fcntl(fd, F_GETFL);
	newopt = oldopt & (~O_NONBLOCK);
	fcntl(fd, F_SETFL, newopt);
	return oldopt;
}

int main(int argc, char *argv[])
{
	if(argc < 3)
	{
		printf("usage: %s [ip] [port]\n", basename(argv[0]));
		return 1;
	}

	int port = atoi(argv[2]);
	char *ip = argv[1];
	struct sockaddr_in servAddr;
	servAddr.sin_family = AF_INET;
	servAddr.sin_port = htons(port);
	inet_aton(ip, &servAddr.sin_addr);

	int sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if(sockfd < 0)
	{
		perror("socket");
		return 2;
	}
	socklen_t len = sizeof(servAddr);
	int ret = connect(sockfd, (struct sockaddr*)&servAddr, len);
	if(ret < 0)
	{
		perror("connect");
		return 3;
	}
	//setNonBlocking(sockfd);

	char buf[1024];
	while(1)
	{
		printf("request# ");
		fflush(stdout);
		int size = read(0, buf, sizeof(buf)-1);
		if(size <= 0)
		{
			printf("read warning!\n");
			continue ;
		}
		buf[size] = 0;
		send(sockfd, buf, strlen(buf), 0);
	
		int s = 0;
		s = recv(sockfd, buf, sizeof(buf)-1, 0);
		buf[s] = 0;
		printf("from server#\n%s", buf);
		if(s >= 1023)
		{
			setNonBlocking(sockfd);
			int ret = 0;
			while((ret = recv(sockfd, buf+ret, sizeof(buf)-1-ret, 0)) > 0)
			{
				buf[ret] = 0;
				printf("%s", buf);
			}
		}
		printf("\n");
		setBlocking(sockfd);	
	}
	return 0;
}

