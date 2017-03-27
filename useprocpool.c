#include "processPool.h"
#include "cgiConn.h"
#include <libgen.h>

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

	int listenfd = socket(AF_INET, SOCK_STREAM, 0);
	if(listenfd < 0)
	{
		perror("socket");
		return 2;
	}
	socklen_t len = sizeof(servAddr);
	int ret = bind(listenfd, (struct sockaddr*)&servAddr, len);
	if(ret < 0)
	{
		perror("bind");
		return 3;
	}
	ret = listen(listenfd, 5);
	if(ret < 0)
	{
		perror("listen");
		return 4;
	}

	processPool<cgiConn> *pool = processPool<cgiConn>::create(listenfd, 8);
	//
	pool->run();
	//
	delete pool;
	close(listenfd);
	return 0;
}


