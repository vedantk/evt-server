/*
 * evt-server.c
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <event.h>
#include "evt-core.h"

/* Microseconds a thread should wait before checking for new conns. */
#define THREAD_POLL_TIME	1000

static struct {
	int port;
	int socktype;		/* SOCK_STREAM or SOCK_DGRAM */
	socklen_t socksize;	/* Network sockaddr size. */
	evt_callback handler;	/* Handles incoming connections. */
	struct addrinfo* ai;	/* Server information. */
	pthread_t server;
	sockfd_t serv_sock;
} serv;

typedef struct client {
	sockfd_t conn;		/* -1: shutdown request. */
	short evt;
	void* arg;
	struct client* next;
} client;

typedef struct {
	pthread_t tid;
	int size;
	client* head;
	client* tail;
	pthread_mutex_t lock;
} cli_queue;

static struct {
	int nr;			/* Number of active threads, or -1. */
	evt_callback slave;	/* Handles accepted connections. */
	int last_sched;		/* Round-robin scheduler. */
	cli_queue* conns;	/* Array of worker queues. */
} pool;

static const struct timeval min_time = {0, THREAD_POLL_TIME};

void*	server_worker(void* arg);
void*	dispatch_worker(void* arg);

client*	cq_front(cli_queue* q);
void	cq_push_back(cli_queue* q, sockfd_t conn, short evt, void* arg);
void	cq_pop_front(cli_queue* q);

int     evt_server_init(int port, int socktype)
{
#ifdef _WIN32
	WSADATA wsaData;
	if (WSAStartup(MAKEWORD(2, 0), &wsaData) != 0) {
		sys_error("WSAStartup");
		return 1;
	}
#endif

	serv.port = port;
	serv.socktype = socktype;
	serv.ai = getnetinfo(NULL, port, socktype);
	serv.socksize = serv.ai->ai_addrlen;
	pool.nr = -1;
	return 0;
}

int	evt_server_start(evt_callback cb, void* arg)
{
	event_init();
	serv.handler = cb;
	return pthread_create(&serv.server, NULL, server_worker, arg);
}

int	evt_server_threads(evt_callback tcb, int nr)
{
	if (pool.nr != -1) return -1;
	pool.nr = nr;
	pool.slave = tcb;
	if (nr <= 0) return 0;

	pool.last_sched = 0;
	pool.conns = calloc(nr, sizeof(cli_queue));

	int i;
	int ret;
	for (i = 0; i < nr; ++i) {
		ret = pthread_create(&pool.conns[i].tid, NULL,
				     dispatch_worker, &pool.conns[i]);
		if (ret != 0) {
			pool.nr = 0;
			free(pool.conns);
			return sys_error("Failed to start threads.");
		}
	}
	return nr;
}

void    evt_server_stop()
{
	if (event_loopexit(&min_time) != 0) {
		event_loopbreak();
	}
	if (pool.nr > 0) {
		int i;
		for (i = 0; i < pool.nr; ++i) {
			cq_push_back(&pool.conns[i], -1, 0, NULL);
			while (pool.conns[i].size > 1) {
				usleep(THREAD_POLL_TIME);
			}
		}
		free(pool.conns);
	}
	freeaddrinfo(serv.ai);
}

int	evt_tcp(sockfd_t fd, struct sockaddr* sock)
{
	sockfd_t conn = accept(fd, sock, &serv.socksize);
	if (conn == -1) {
		return sys_error("accept");
	}
	return conn;
}

int	evt_udp(sockfd_t fd, char* buf, int size, int flags,
		struct sockaddr* sock)
{
	int len = recvfrom(fd, buf, size, flags, sock, &serv.socksize);
	if (len == -1) {
		return sys_error("recvfrom");
	}
	return len;
}

void	evt_thread_dispatch(sockfd_t conn, short evt, void* arg)
{
	int i = (pool.last_sched + 1) % pool.nr;
	cq_push_back(&pool.conns[i], conn, evt, arg);
	pool.last_sched = i;
}

void    evt_close(sockfd_t fd)
{
	if (shutdown(fd, SHUT_RDWR) != 0) {
		sys_error("evt_close: shutdown");
	}
#ifdef _WIN32
	closesocket(fd);
#else
	close(fd);
#endif
}

struct addrinfo* get_server_addrinfo()
{
	return serv.ai;
}

struct addrinfo* getnetinfo(const char* host, int port, int socktype)
{
    struct addrinfo hints;
    struct addrinfo* result;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = socktype;
    hints.ai_flags = AI_PASSIVE;
    char portstr[8];
    snprintf(portstr, sizeof(portstr), "%d", port);

    int err;
    if ((err = getaddrinfo(host, portstr, &hints, &result)) != 0) {
        sys_error("getaddrinfo");
        fprintf(stderr, "getaddrinfo - %s\n", gai_strerror(err));
    }
    return result;
}

void    print_addrinfo(struct addrinfo* ai)
{
	printf ("ai_flags; %d\n"
		"ai_family; %d\n"
		"ai_socktype; %d\n"
		"ai_protocol; %d\n"
		"ai_addrlen; %u\n"
		"ai_canonname; %s\n"
	, ai->ai_flags, ai->ai_family, ai->ai_socktype,
	ai->ai_protocol, ai->ai_addrlen, ai->ai_canonname);
}

int     get_sockaddr_size()
{
	return serv.socksize;
}

struct sockaddr* new_sockaddr()
{
	struct sockaddr* sa = calloc(1, serv.socksize);
	sa->sa_family = serv.ai->ai_family;
	return sa;
}

typedef struct {
	short sin_family;
	unsigned short sin_port;
	unsigned int sin_addr;
} sa4_serial;

static const int IP4_SOCKLEN = sizeof(sa4_serial);
static const int IP6_SOCKLEN = sizeof(struct sockaddr_in6);

int	serialize_addr(struct sockaddr* src, char* dest, int size)
{
	if (serv.ai->ai_family == AF_INET) {
		if (size < IP4_SOCKLEN) return 0;
		sa4_serial* dsa4 = (sa4_serial*) dest;
		struct sockaddr_in* sa4 = (struct sockaddr_in*) src;
		dsa4->sin_family = htons(sa4->sin_family);
		dsa4->sin_port = htons(sa4->sin_port);
		dsa4->sin_addr = htonl(sa4->sin_addr.s_addr);
		return IP4_SOCKLEN;
	} else {
		if (size < IP6_SOCKLEN) return 0;
		struct sockaddr_in6* dsa6 = (struct sockaddr_in6*) dest;
		struct sockaddr_in6* sa6 = (struct sockaddr_in6*) src;
		dsa6->sin6_family = htons(sa6->sin6_family);
		dsa6->sin6_port = htons(sa6->sin6_port);
		dsa6->sin6_flowinfo = htonl(sa6->sin6_flowinfo);
		dsa6->sin6_scope_id = htonl(sa6->sin6_scope_id);
		memcpy(&dsa6->sin6_addr.s6_addr,
		       &sa6->sin6_addr.s6_addr, 16);
		return IP6_SOCKLEN;
	}
}

int	deserialize_addr(char* src, struct sockaddr* dest, int size)
{
	if (serv.ai->ai_family == AF_INET) {
		if (size < IP4_SOCKLEN) return 0;
		sa4_serial* sa4 = (sa4_serial*) src;
		struct sockaddr_in* dsa4 = (struct sockaddr_in*) dest;
		dsa4->sin_family = ntohs(sa4->sin_family);
		if (dsa4->sin_family != AF_INET) return 0;
		dsa4->sin_port = ntohs(sa4->sin_port);
		dsa4->sin_addr.s_addr = ntohl(sa4->sin_addr);
		return IP4_SOCKLEN;
	} else {
		if (size < IP6_SOCKLEN) return 0;
		struct sockaddr_in6* sa6 = (struct sockaddr_in6*) src;
		struct sockaddr_in6* dsa6 = (struct sockaddr_in6*) dest;
		dsa6->sin6_family = ntohs(sa6->sin6_family);
		if (dsa6->sin6_family != AF_INET6) return 0;
		dsa6->sin6_port = ntohs(sa6->sin6_port);
		dsa6->sin6_flowinfo = ntohl(sa6->sin6_flowinfo);
		memcpy(&dsa6->sin6_addr.s6_addr,
		       &sa6->sin6_addr.s6_addr, 16);
		dsa6->sin6_scope_id = ntohl(sa6->sin6_scope_id);
		return IP6_SOCKLEN;
	}
}

int get_conn_to_host(const char* host, int port, int socktype)
{
	sockfd_t sock;
	struct addrinfo *rp, *result = getnetinfo(host, port, socktype);
	for (rp = result; rp != NULL; rp = rp->ai_next) {
		sock = socket(rp->ai_family, rp->ai_socktype,
			      rp->ai_protocol);
		if (sock >= 0) break;
	}

	if (rp == NULL) {
		freeaddrinfo(result);
		return sys_error("socket");
	}

	if (socktype == SOCK_STREAM) {
		if (connect(sock, rp->ai_addr, serv.socksize) == -1) {
			evt_close(sock);
			freeaddrinfo(result);
			return sys_error("connect");
		}
	}

	freeaddrinfo(result);
	return sock;
}

int get_conn_to_addr(struct sockaddr* addr, int port, int socktype)
/* Convert "port" to network-order before calling this function. */
{
	int ai_protocol = (socktype == SOCK_STREAM)
			  ? IPPROTO_TCP : IPPROTO_UDP;
	sockfd_t fd = socket(serv.ai->ai_family, socktype, ai_protocol);
	if (fd < 0) {
		return sys_error("socket");
	}

	memcpy(addr + 2, &port, 2);
	return fd;
}

int     get_nr_cpus()
{
#ifdef _WIN32
	SYSTEM_INFO sysinfo;
	GetSystemInfo(&sysinfo);
	return sysinfo.dwNumberOfProcessors;
#else
	return sysconf(_SC_NPROCESSORS_ONLN);
#endif
}

int	set_nonblocking(sockfd_t fd)
{
	int flags;
	if ((flags = fcntl(fd, F_GETFL, 0)) == -1) {
		flags = 0;
	}

	int ret = fcntl(fd, F_SETFL, flags | O_NONBLOCK);
	if (ret < 0) {
		sys_error("Setting O_NONBLOCK failed.");
	}
	return ret;
}

void	print_buffer(const char* name, void* buf, int size)
{
	int i;
	char* buffer = buf;
	puts(name);
	for (i = 0; i < size; ++i) {
		printf("%d ", buffer[i]);
	}
	puts("");
}

int	sys_error(const char* msg)
{
#ifdef _WIN32
	fprintf(stderr, "sys_error (%s): %s\n", WSAGetLastError(), msg);
#else
	perror(msg);
#endif
	return -1;
}

void*	server_worker(void* arg)
{
	sockfd_t sock = 0;
	struct addrinfo *rp;
	for (rp = serv.ai; rp != NULL; rp = rp->ai_next) {
		sock = socket(rp->ai_family, rp->ai_socktype,
			      rp->ai_protocol);
		if (sock < 0) continue;

		int ret = 1;
		int err = setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
				     &ret, 4);
		if (err != 0) {
			sys_error("setsockopt: SO_REUSEADDR");
		}

		if (bind(sock, rp->ai_addr, rp->ai_addrlen) == 0) {
			break;
		}
	}

	if (rp == NULL) {
		sys_error("bind");
		goto server_worker_exit;
	}

	if (set_nonblocking(sock) == -1) {
		goto server_worker_exit;
	}

	if (serv.socktype == SOCK_STREAM) {
		if (listen(sock, 5) == -1) {
			sys_error("listen");
			goto server_worker_exit;
		}
	}

	serv.serv_sock = sock;
	struct event evt;
	event_set(&evt, sock, EV_READ | EV_PERSIST, serv.handler, arg);
	event_add(&evt, NULL);
	event_dispatch();

server_worker_exit:
	evt_close(sock);
#ifdef _WIN32
	WSACleanup();
#endif
	return NULL;
}

void*	dispatch_worker(void* arg)
{
	cli_queue* q = arg;
	q->size = 0;
	q->head = q->tail = NULL;
	pthread_mutex_init(&q->lock, NULL);

	while (1) {
		pthread_mutex_lock(&q->lock);
		if (q->size > 0) {
			client* cli = cq_front(q);
			cq_pop_front(q);
			pthread_mutex_unlock(&q->lock);

			if (cli->conn == -1) {
				free(cli);
				pthread_mutex_destroy(&q->lock);
				return NULL;
			} else {
				pool.slave(cli->conn, cli->evt,
					   cli->arg);
				free(cli);
			}
		} else {
			pthread_mutex_unlock(&q->lock);
			usleep(THREAD_POLL_TIME);
		}
	}
	return NULL;
}

client*	cq_front(cli_queue* q)
{
	return q->head;
}

void	cq_push_back(cli_queue* q, sockfd_t conn, short evt, void* arg)
{
	client* p = malloc(sizeof(client));
	p->conn = conn;
	p->evt = evt;
	p->arg = arg;
	p->next = NULL;

	pthread_mutex_lock(&q->lock);
	if (NULL == q->head && NULL == q->tail) {
		q->head = q->tail = p;
	} /* else if (NULL == q->head || NULL == q->tail) {
		sys_error("Encountered internal memory corruption.");
		abort();
	} */ else {
		q->tail->next = p;
		q->tail = p;
	}
	++q->size;
	pthread_mutex_unlock(&q->lock);
}

void	cq_pop_front(cli_queue* q)
/* Aquire the queue mutex before calling this function and free the
 * head element after processing it. */
{
	client* h = q->head;
	client* p = h->next;
	q->head = p;
	if (NULL == q->head) {
		q->tail = NULL;
	}
	--q->size;
}

#ifdef __cplusplus
}
#endif
