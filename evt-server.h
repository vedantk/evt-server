/*
 * evt-server.h
 */

#ifndef EVT_SERVER_H
#define EVT_SERVER_H

#ifdef _WIN32
# define WIN32_LEAN_AND_MEAN
# include <windows.h>
typedef intptr_t sockfd_t;
#else
# include <unistd.h>
# include <sys/socket.h>
# include <arpa/inet.h>
# include <netdb.h>
typedef int sockfd_t;
#endif

#include <event.h>

typedef void    (*evt_callback)(sockfd_t fd, short evt, void* arg);

int	evt_server_init(int port, int socktype);
int	evt_server_start(evt_callback cb, void* arg);
int	evt_server_threads(evt_callback tcb, int nr);
void	evt_server_stop();

/* evt_callback helper functions. */
int	evt_tcp(sockfd_t fd, struct sockaddr* sock);
int	evt_udp(sockfd_t fd, char* buf, int size, int flags,
		struct sockaddr* sock);
void	evt_thread_dispatch(sockfd_t conn, short evt, void* arg);
void	evt_close(sockfd_t fd);

/* See evt-core.h for other useful functions. */

#endif /* EVT_SERVER_H */
