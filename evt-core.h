/*
 * evt-core.h
 */

#ifndef EVT_CORE_H
#define EVT_CORE_H

#include "evt-server.h"

struct addrinfo* get_server_addrinfo();
struct addrinfo* getnetinfo(const char* host, int port, int socktype);
void	print_addrinfo(struct addrinfo* ai);

int	get_sockaddr_size();
struct sockaddr* new_sockaddr();
int	serialize_addr(struct sockaddr* src, char* dest, int size);
int	deserialize_addr(char* src, struct sockaddr* dest, int size);

int get_conn_to_host(const char* host, int port, int socktype);
int get_conn_to_addr(struct sockaddr* addr, int port, int socktype);

int	get_nr_cpus();
int	set_nonblocking(sockfd_t fd);
void	print_buffer(const char* name, void* buf, int size);
int	sys_error(const char* msg);

#endif /* EVT_CORE_H */
