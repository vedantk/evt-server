#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include "evt-core.h"

#define USE_EVT_THREADS		(get_nr_cpus())

void thread_worker(sockfd_t conn, short evt, void* arg)
{
	char buf[512];
    read(conn, buf, sizeof(buf));
    write(conn, buf, sizeof(buf));
    evt_close(conn);
    free(arg);
}

void callback(sockfd_t fd, short evt, void* arg)
{
    struct sockaddr* sock = new_sockaddr();
    sockfd_t conn = evt_tcp(fd, sock);
#ifdef USE_EVT_THREADS
	evt_thread_dispatch(conn, evt, sock);
#else
	thread_worker(conn, evt, sock);
#endif
}

int main(int argc, char** argv) {
	puts("Usage: ./evt_test [port]");
	assert(argc == 2);

	int port = atoi(argv[1]);
    evt_server_init(port, SOCK_STREAM);
    print_addrinfo(get_server_addrinfo());
	evt_server_start(callback, NULL);
#ifdef USE_EVT_THREADS
	evt_server_threads(thread_worker, USE_EVT_THREADS);
#endif

	while (1) {
		sleep(1000);
	}
    return 0;
}
