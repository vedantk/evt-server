#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <assert.h>
#include "evt-core.h"

typedef unsigned int u32;
const int nr_threads = 4;

/* Overhead hovers around 90 to 110 usecs. */
#define TIMEIT(__expr, __usecs) do {		\
		struct timeval __beg, __end;	\
		gettimeofday(&__beg, NULL);	\
		do { __expr } while (0);	\
		gettimeofday(&__end, NULL);	\
		u32 __end_us = (__end.tv_sec * 1000000) + __end.tv_usec; \
		u32 __beg_us = (__beg.tv_sec * 1000000) + __beg.tv_usec; \
		__usecs = __end_us - __beg_us;	\
	} while(0); \

int port;
char* data;
int size;
int nr_requests;
pthread_mutex_t lock;
unsigned long total = 0;
int nr_accepted = 0;

const char* HOST = NULL;

void* worker(void* arg) {
	int i;
	for (i = 0; i < nr_requests / nr_threads; ++i) {
		int sfd;
		int ret;
		u32 usecs;
		TIMEIT({
			sfd = get_conn_to_host(HOST, port, SOCK_STREAM);
			ret = write(sfd, data, size);
			ret += read(sfd, data, size);
			evt_close(sfd);
		}, usecs);

		pthread_mutex_lock(&lock);
		total += usecs;
		if (ret == 2 * size) ++nr_accepted;
		pthread_mutex_unlock(&lock);
	}
	return arg;
}

int main(int argc, char** argv) {
	puts("Usage: ./flooder [port] [nr_conns]");

	assert(argc == 3);
	port = atoi(argv[1]);
	nr_requests = atoi(argv[2]);
	size = 512;

	int pad = nr_requests % nr_threads;
	nr_requests += nr_threads - pad;

	data = malloc(size);
	pthread_t pool[nr_threads];
	pthread_mutex_init(&lock, NULL);
	evt_server_init(port, SOCK_STREAM);
	int i;
	for (i = 0; i < nr_threads; ++i) {
		pthread_create(&pool[i], NULL, worker, NULL);
	}
	pthread_join(pool[nr_threads - 1], NULL);
	fflush(stdout);

	double avg = ((double) total) / ((double) nr_requests);
	printf ("nr_accepted = %d\n"
			"nr_requests = %d\n"
			"total time (microseconds) = %lu\n"
			"avg. time per req (microseconds) = %f\n"
	, nr_accepted, nr_requests, total, avg);

	return 0;
}

