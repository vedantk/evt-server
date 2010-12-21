#include <stdio.h>
#include <stdlib.h>
#include "evt-core.h"

int main()
{
	evt_server_init(9999, SOCK_STREAM);

	int cpus = get_nr_cpus();

	int size = get_sockaddr_size();
	char buf[size];

	struct sockaddr* sock = new_sockaddr();
	struct addrinfo* ai = get_server_addrinfo();
	print_buffer("sock", sock, size);
	print_buffer("ai_addr", ai->ai_addr, size);
	int cp1 = copy_addr_into_buf(ai->ai_addr, buf, size);
	print_buffer("ai_addr -> *buf*", buf, size);
	int cp2 = copy_buf_into_addr(buf, sock, size);
	print_buffer("buf -> *ai_addr*", ai->ai_addr, size);

	printf ("cpus: %d\n"
			"socksize: %d\n"
			"cp1 -- cp2: %d -- %d\n"
	, cpus, get_sockaddr_size(), cp1, cp2);

	return 0;
}
