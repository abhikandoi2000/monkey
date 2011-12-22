/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*  Monkey HTTP Daemon
 *  ------------------
 *  Copyright (C) 2001-2011, Eduardo Silva P.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Library General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <string.h>

#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/sendfile.h>
#include <netdb.h>
#include <fcntl.h>

#include "MKPlugin.h"

MONKEY_PLUGIN("liana",         /* shortname */
              "Liana Network", /* name */
              VERSION,        /* version */
              MK_PLUGIN_NETWORK_IO); /* hooks */

struct mk_config *conf;

int _mkp_init(void **api, char *confdir)
{
    mk_api = *api;
    return 0;
}

void _mkp_exit()
{
}

int _mkp_network_io_accept(int server_fd)
{
    int remote_fd;
    struct sockaddr sock_addr;
    socklen_t socket_size = sizeof(struct sockaddr);

#ifdef ACCEPT_GENERIC
    remote_fd = accept(server_fd, &sock_addr, &socket_size);

    if (fcntl(remote_fd, F_SETFL, fcntl(remote_fd, F_GETFD, 0) | O_NONBLOCK) == -1) {
        mk_err("Can't set to non-blocking the socket");
    }
#else
    remote_fd = accept4(server_fd, &sock_addr, &socket_size, SOCK_NONBLOCK);
#endif

    return remote_fd;
}

int _mkp_network_io_read(int socket_fd, void *buf, int count)
{
    ssize_t bytes_read;

    bytes_read = read(socket_fd, (void *)buf, count);

    return bytes_read;
}

int _mkp_network_io_write(int socket_fd, const void *buf, size_t count )
{
    ssize_t bytes_sent = -1;

    bytes_sent = write(socket_fd, buf, count);

    return bytes_sent;
}

int _mkp_network_io_writev(int socket_fd, struct mk_iov *mk_io)
{
    ssize_t bytes_sent = -1;

    bytes_sent = mk_api->iov_send(socket_fd, mk_io);

    return bytes_sent;
}

int _mkp_network_io_close(int socket_fd)
{
    close(socket_fd);
    return 0;
}

int _mkp_network_io_create_socket(int domain, int type, int protocol)
{
    int socket_fd;

    socket_fd = socket(domain, type, protocol);

    return socket_fd;
}

/* We need to know how to solve the problem with AF_INET and AF_INET6 */
int _mkp_network_io_connect(char *host, int port)
{
    int ret;
    int socket_fd = -1;
    char *port_str = 0;
    unsigned long len;
    struct addrinfo hints;
    struct addrinfo *res, *rp;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    mk_api->str_build(&port_str, &len, "%d", port);

    ret = getaddrinfo(host, port_str, &hints, &res);
    mk_api->mem_free(port_str);
    if(ret != 0) {
        mk_err("Can't get addr info: %s", gai_strerror(ret));
        return -1;
    }
    for(rp = res; rp != NULL; rp = rp->ai_next) {
        socket_fd = _mkp_network_io_create_socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);

        if( socket_fd == -1) {
            mk_warn("Error creating client socket");
            return -1;
        }

        if (connect(socket_fd,
                    (struct sockaddr *) rp->ai_addr, rp->ai_addrlen) == -1) {
            close(socket_fd);
            mk_err("Can't connect to %s", host);
            return -1;
        }

        break;
    }

    return socket_fd;
}

int _mkp_network_io_send_file(int socket_fd, int file_fd, off_t *file_offset,
                              size_t file_count)
{
    ssize_t bytes_written = -1;

    bytes_written = sendfile(socket_fd, file_fd, file_offset, file_count);

    if (bytes_written == -1) {
        PLUGIN_TRACE("[FD %i] error from sendfile() = -1", socket_fd);
        return -1;
    }

    return bytes_written;
}

int _mkp_network_io_bind(int socket_fd, const struct sockaddr *addr, socklen_t addrlen, int backlog)
{
    int ret;

    ret = bind(socket_fd, addr, addrlen);

    if( ret == -1 ) {
        mk_warn("Error binding socket");
        return ret;
    }

    ret = listen(socket_fd, backlog);

    if(ret == -1 ) {
        mk_warn("Error setting up the listener");
        return -1;
    }

    return ret;
}

int _mkp_network_io_server(int port, char *listen_addr)
{
    int socket_fd = -1;
    int ret;
    char *port_str = 0;
    unsigned long len;
    struct addrinfo hints;
    struct addrinfo *res, *rp;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    mk_api->str_build(&port_str, &len, "%d", port);

    ret = getaddrinfo(listen_addr, port_str, &hints, &res);
    mk_api->mem_free(port_str);
    if(ret != 0) {
        mk_err("Can't get addr info: %s", gai_strerror(ret));
        return -1;
    }

    for(rp = res; rp != NULL; rp = rp->ai_next) {
        socket_fd = _mkp_network_io_create_socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);

        if( socket_fd == -1) {
            mk_warn("Error creating server socket");
            return -1;
        }

        mk_api->socket_set_tcp_nodelay(socket_fd);
        mk_api->socket_reset(socket_fd);
        ret = _mkp_network_io_bind(socket_fd, rp->ai_addr, rp->ai_addrlen, MK_SOMAXCONN);

        if(ret == -1) {
            mk_err("Port %i cannot be used\n", port);
            return -1;
        }
        break;
    }

    return socket_fd;
}

char * _mkp_network_io_ip_str(int socket_fd, int *size)
{
    struct sockaddr *addr = NULL;
    socklen_t len = sizeof(struct sockaddr);
    char *ip = (char *)mk_api->mem_alloc_z(INET_ADDRSTRLEN + 1);

    *size = INET_ADDRSTRLEN + 1;

    if(getpeername(socket_fd, addr, &len) == -1 ) {
        PLUGIN_TRACE("[FD %i] Can't get addr for this socket", socket_fd);
        mk_api->mem_free(ip);
        return NULL;
    }

    if(addr->sa_family == AF_INET) {
        if(inet_ntop(addr->sa_family, &((struct sockaddr_in *)addr)->sin_addr,
                     ip, INET_ADDRSTRLEN) == NULL) {
            PLUGIN_TRACE("Can't get the IP text form");
            mk_api->mem_free(ip);
            return NULL;
        }
    }

    if(addr->sa_family == AF_INET6) {
        if(inet_ntop(addr->sa_family, &((struct sockaddr_in6 *)addr)->sin6_addr,
                     ip, INET_ADDRSTRLEN) == NULL) {
            PLUGIN_TRACE("Can't get the IP text form");
            mk_api->mem_free(ip);
            return NULL;
        }
    }


    return ip;
}
