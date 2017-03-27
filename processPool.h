// processPool.h

#ifndef PROCESSPOLL_H
#define PROCESSPOLL_H

#pragma once

#include <stdio.h>
#include <unistd.h>
#include <assert.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/epoll.h>
#include <signal.h>
#include <sys/wait.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>

class process
{
public:
	pid_t m_pid;
	int m_pipefd[2];
public:
	process(): m_pid(-1)
	{}
};
template<class T>
class processPool
{
private:
	processPool(int listenfd, int procNum);
public:
	static processPool<T>* create(int listenfd, int procNum)
	{
		if(m_instance == NULL)
		{
			m_instance = new processPool<T>(listenfd, procNum);
		}
		return m_instance;
	}
	~processPool()
	{
		delete[] m_subProc;
	}
	void run();
private:
	void setSigPipe();
	void runFather();
	void runChild();
private:
	static const int MAX_PROC_NUM = 16;
	static const int USERS_PER_PROC = 1024;
	static const int MAX_EPOLL_EVENT = 1000;
	int m_procNum;
	int m_index;
	int m_listenfd;
	int m_stop;
	//int m_userCount;
	int m_epollfd;
	process *m_subProc;
	static processPool<T> *m_instance;
};
template<class T>
processPool<T>* processPool<T>::m_instance = NULL;

static int sigPipe[2];

//static part
static int setNonBlocking(int fd)
{
	int oldOption = fcntl(fd, F_GETFL);
	int newOption = oldOption | O_NONBLOCK;
	fcntl(fd, F_SETFL, newOption);
	return oldOption;
}
static void sigHandler(int sig)
{
	int saveErrno = errno;
	int msg = sig;
	send(sigPipe[1], (char*)&msg, 1, 0);
	errno = saveErrno;
}
static void addSig(int sig, void(*sigHandler)(int sig), int restart = 0)
{
	struct sigaction oldaction, newaction;
	newaction.sa_handler = sigHandler;
	sigfillset(&newaction.sa_mask);
	if(restart != 0)
	{
		newaction.sa_flags |= SA_RESTART;
	}
	sigaction(sig, &newaction, &oldaction);
}
static void addfd(int epollfd, int fd)
{
	setNonBlocking(fd);
	struct epoll_event event;
	event.events = EPOLLIN | EPOLLET;
	event.data.fd = fd;
	epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
}
//static end
void removefd(int epollfd, int fd)
{
	epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
	close(fd);
}

template<class T>
processPool<T>::processPool(int listenfd, int procNum): m_listenfd(listenfd), m_procNum(procNum), m_stop(0), m_index(0), m_subProc(NULL)
{
	m_subProc = new process[procNum+1];
	assert(m_subProc != NULL);
	
	for(int i = 1; i <= procNum; i++)
	{
		int ret = socketpair(AF_LOCAL, SOCK_STREAM, 0, m_subProc[i].m_pipefd);
		if(ret < 0)
		{
			perror("socketpair");
			exit(1);
		}
		pid_t pid = fork();
		m_subProc[i].m_pid = pid;
		if(pid < 0)
		{
			perror("fork");
			exit(3);
		}
		else if(pid == 0)
		{
			close(m_subProc[i].m_pipefd[0]);
			m_index = i;
			break;
		}
		else
		{
			close(m_subProc[i].m_pipefd[1]);
			setNonBlocking(m_subProc[i].m_pipefd[0]);
		}
	}
}
template<class T>
void processPool<T>::setSigPipe()
{
	m_epollfd = epoll_create(5);
	if(m_epollfd < 0)
	{
		perror("epoll_create");
		exit(2);
	}
	int ret = socketpair(AF_LOCAL, SOCK_STREAM, 0, sigPipe);
	if(ret < 0)
	{
		perror("socketpair");
		exit(1);
	}
	addfd(m_epollfd, sigPipe[0]);
	setNonBlocking(sigPipe[1]);
	addSig(SIGCHLD, sigHandler);
	addSig(SIGTERM, sigHandler);
	addSig(SIGINT, sigHandler);
	addSig(SIGPIPE, SIG_IGN);
}
template<class T>
void processPool<T>::run()
{
	if(m_index == 0)
	{
		runFather();
		return ;
	}
	runChild();
}
template<class T>
void processPool<T>::runFather()
{
	setSigPipe();

	addfd(m_epollfd, m_listenfd);
	addfd(m_epollfd, m_listenfd);
	int subProcUse = 1;
	int subProcStart;
	int newConn = 1;

	while(!m_stop)
	{
		struct epoll_event events[MAX_EPOLL_EVENT];
		int s = epoll_wait(m_epollfd, events, MAX_EPOLL_EVENT, -1);
		if((s < 0) && (errno != EINTR))
		{
			perror("epoll_wait");
			exit(4);
		}
		for(int i = 0; i < s; i++)
		{
			int sockfd = events[i].data.fd;
			if((sockfd == m_listenfd) && (events[i].events & EPOLLIN))
			{
				subProcStart = subProcUse;
				do
				{
					if(m_subProc[subProcUse].m_pid != -1)
					{
						break;
					}
					if(subProcUse == m_procNum)
						subProcUse = 1;
					else
						subProcUse = subProcUse + 1;
				}while(subProcStart != subProcUse);
				if(m_subProc[subProcUse].m_pid == -1)
				{
					printf("no subprocess to use\n");
					m_stop = 1;
					break;
				}
				send(m_subProc[subProcUse].m_pipefd[0], (char*)&newConn, sizeof(newConn), 0);
				printf("send a new connection to subprocess %d\n", subProcUse);
				if(subProcUse == m_procNum)
					subProcUse = 1;
				else
					subProcUse++;
			}
			else if((sockfd == sigPipe[0]) && (events[i].events & EPOLLIN))
			{
				char buf[1024];
				int num = read(sockfd, buf, sizeof(buf)-1);
				if(num  < 0)
				{
					perror("read");
					continue ;
				}
				for(int k = 0; k < num; k++)
				{
					int ret;
					switch(buf[k])
					{
						case SIGCHLD:
								while((ret = waitpid(-1, NULL, WNOHANG)) > 0)
								{
									for(int n = 1; n <= m_procNum; n++)
									{
										if(m_subProc[n].m_pid == ret)
										{
											printf("subprocess %d is quit!\n", ret);
											close(m_subProc[n].m_pipefd[0]);
											m_subProc[n].m_pid = -1;
											break;
										}
									}
								}
								m_stop = 1;
								for(int n = 1; n <= m_procNum; n++)
								{
									if(m_subProc[n].m_pid != -1)
									{
										m_stop = 0;
										break;
									}
								}
								break;
						case SIGTERM:
						case SIGINT:
								//for(int n = 1; n <= m_procNum; n++)
								//{
								//	if(m_subProc[n].m_pid != -1)
								//	{
								//		kill(m_subProc[n].m_pid, SIGINT);
								//	}
								//}
								m_stop = 1;
								break;
						default:
								break;
					}
				}
			}
			else
			{
			}
		}
	}
	//close(m_listenfd);
	for(int n = 1; n <= m_procNum; n++)
	{
		if(m_subProc[n].m_pid != -1)
		{
			kill(m_subProc[n].m_pid, SIGINT);
		}
	}
	while(waitpid(-1, NULL, WNOHANG) > 0);
	close(m_epollfd);
}
template<class T>
void processPool<T>::runChild()
{
	setSigPipe();

	addfd(m_epollfd, m_subProc[m_index].m_pipefd[1]);

	//???????????????????????????????????????????//
	T* users = new T[USERS_PER_PROC];
	struct epoll_event events[MAX_EPOLL_EVENT];
	while(!m_stop)
	{
		char buf[1024];
		memset(buf, '\0', sizeof(buf));
		int s = epoll_wait(m_epollfd, events, MAX_EPOLL_EVENT, -1);
		if((s < 0) && (errno != EINTR))
		{
			perror("epoll_wait");
			exit(4);
		}
		for(int i = 0; i < s; i++)
		{
			int sockfd = events[i].data.fd;
			if((sockfd == m_subProc[m_index].m_pipefd[1]) && (events[i].events & EPOLLIN))
			{
				int ret = read(m_subProc[m_index].m_pipefd[1], buf, sizeof(buf)-1);
				if(((ret < 0) && (errno != EINTR)) || ret == 0)
				{
					continue ;
				}
				struct sockaddr_in clientAddr;
				socklen_t len = sizeof(clientAddr);
				int connfd = accept(m_listenfd, (struct sockaddr*)&clientAddr, &len);
				if(connfd < 0)
				{
					perror("accept");
					continue ;
				}
				printf("subprocess %d get a new connection\n", getpid());
				addfd(m_epollfd, connfd);

				//????????????????????????????????????????????
				for(int j = 0; j < USERS_PER_PROC; j++)
				{
					if(users[j].getsockfd() == -1)
					{
						users[j].init(m_epollfd, connfd, clientAddr);
						break;
					}
				}
			}
			else if((sockfd == sigPipe[0]) && (events[i].events & EPOLLIN))
			{
				int size = read(sockfd, buf, sizeof(buf)-1);
				if((size < 0) && (errno != EINTR))
				{
					perror("read");
					continue ;
				}
				for(int n = 0; n < size; n++)
				{
					switch(buf[n])
					{
						case SIGCHLD:
								while(waitpid(-1, NULL, WNOHANG) > 0);
								break;
						case SIGTERM:
						case SIGINT:
								m_stop = 1;
								break;
						default:
								break;
					}
				}
			}
			else if(events[i].events & EPOLLIN)
			{

				//???????????????????????????????????????????????
				//users[connfd].process();

				for(int j = 0; j < USERS_PER_PROC; j++)
				{
					if(users[j].getsockfd() == sockfd)
					{
						//printf("users[j].deal()\n");
						users[j].deal();
						break;
					}
				}

				//int _s = read(sockfd, buf, sizeof(buf)-1);
				//if((_s < 0) && (errno != EINTR))
				//{
				//	perror("read");
				//	continue ;
				//}
				//else if(_s == 0)
				//{
				//	printf("connection %d is closed!\n", sockfd);
				//	removefd(m_epollfd, sockfd);
				//}
				//else
				//{
				//	buf[_s] = 0;
				//	printf("from client %d# %s\n", sockfd, buf);
				//}
			}
			else
			{
			}
		}
	}
	close(m_epollfd);
	delete[] m_subProc;
	m_subProc = NULL;
}

#endif
