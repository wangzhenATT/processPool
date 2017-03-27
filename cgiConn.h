//#include <stdio.h>
//#include <unistd.h>
//#include <assert.h>
//#include <stdlib.h>
//#include <sys/types.h>
//#include <sys/socket.h>
//#include <fcntl.h>
//#include <errno.h>
//#include <sys/epoll.h>
//#include <signal.h>
//#include <sys/wait.h>
//#include <netinet/in.h>
//#include <arpa/inet.h>
//#include <string.h>
#include "processPool.h"

class cgiConn
{
private:
	static const int MAXBUFSIZE = 1024;
	int m_sockfd;
	int m_epollfd;
	int m_readindex;
	char buf[MAXBUFSIZE];
	struct sockaddr_in clientAddr;
public:
	cgiConn(): m_sockfd(-1), m_readindex(0) {}
	void init(int epollfd, int sockfd, struct sockaddr_in &addr)
	{
		m_epollfd = epollfd;
		m_sockfd = sockfd;
		m_readindex = 0;
		clientAddr = addr;
		memset(buf, '\0', MAXBUFSIZE);
	}
	void deal()
	{
		char buf[1024];
		char dstbuf[1024];
		while(1)
		{
			//m_readindex = 0;
			//memset(buf, '\0', sizeof(buf));
			//memset(dstbuf, '\0', sizeof(buf));
			//printf("cgiConn :: deal\n");
			int size = read(m_sockfd, buf+m_readindex, sizeof(buf)-1);
			if((size < 0) && (errno != EINTR))
			{
				perror("read");
				return ;
			}
			else if(size == 0)
			{
				printf("connection %d is closed!\n", m_sockfd);
				removefd(m_epollfd, m_sockfd);
				m_sockfd = -1;
				return ;
			}
			else
			{
				int index = m_readindex;
				m_readindex += size;
				for(; index < m_readindex; index++)
				{
					if((index > 0) && (buf[index] == '\n'))
						break;
				}
				if(index == m_readindex)
					continue ;

				m_readindex = 0;	//weight

				buf[index] = '\0';
				getcwd(dstbuf, sizeof(dstbuf));
				strcat(dstbuf, "/");
				strcat(dstbuf, buf);
				if(access(dstbuf, F_OK) == -1)
				{
					//printf("client$ %s\n", buf);
					char *msg = "sorry, request not found";
					send(m_sockfd, msg, strlen(msg), 0);
					return ;
				}
				else
				{
					pid_t pid = fork();
					if(pid < 0)
					{
						perror("fork");
						return ;
					}
					else if(pid == 0)
					{
						close(1);
						dup(m_sockfd);
						execl(dstbuf, dstbuf, NULL);
					}
					else
					{
						waitpid(pid, NULL, 0);
						return ;
					}
				}
			}
		}
	}
	int getsockfd()
	{
		return m_sockfd;
	}
};
