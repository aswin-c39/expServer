#include "xps_upstream.h"

xps_connection_t *xps_upstream_create(xps_core_t *core, const char *host, u_int port) {
    assert(core != NULL);
    assert(host != NULL);
    assert(is_valid_port(port));

    // Resolve upstream address
    struct addrinfo *addr_info = xps_getaddrinfo(host, port);

    if (addr_info == NULL) {
        logger(LOG_ERROR, "xps_upstream_create()", "xps_getaddrinfo() failed");
        return NULL;
    }

    // Create socket instance
    int sock_fd = socket(addr_info->ai_family, addr_info->ai_socktype | SOCK_NONBLOCK,addr_info->ai_protocol);

    if (sock_fd < 0) {
        logger(LOG_ERROR, "xps_upstream_create()", "socket() failed");
        perror("Error message");
        freeaddrinfo(addr_info);
        return NULL;
    }

    // Connect to upstream server
    int connect_error = connect(sock_fd, addr_info->ai_addr, addr_info->ai_addrlen);

    if (!(connect_error == 0 || (connect_error == -1 && errno == EINPROGRESS))) {
        logger(LOG_ERROR, "xps_upstream_create()", "connect() failed");
        perror("Error message");

        freeaddrinfo(addr_info);
        close(sock_fd);
        return NULL;
    } 

    freeaddrinfo(addr_info);

    // Create connection instance
    xps_connection_t *connection = xps_connection_create(core, sock_fd);

    if (connection == NULL) {
        logger(LOG_ERROR, "xps_upstream_create()", "xps_connection_create() failed");
        close(sock_fd);
        return NULL;
    }

    logger(LOG_INFO, "xps_upstream_create()", "created upstream connection to %s:%u", host, port);
    return connection;
}