#include "xps_connection.h"

void connection_loop_read_handler(void *ptr);

xps_connection_t *xps_connection_create(xps_core_t *core, int sock_fd) {

    xps_connection_t *connection = (xps_connection_t *) malloc(sizeof(xps_connection_t));
    if (connection == NULL) {
        logger(LOG_ERROR, "xps_connection_create()", "malloc() failed for 'connection'");
        return NULL;
    }

    /* attach sock_fd to epoll */
    xps_loop_attach(core->loop, sock_fd, EPOLLIN, connection, connection_loop_read_handler);

    // Init values
    connection->core = core;
    connection->sock_fd = sock_fd;
    connection->listener = NULL;
    connection->remote_ip = get_remote_ip(sock_fd);

    /* add connection to 'connections' list */
    vec_push(&core->connections, connection);

    logger(LOG_DEBUG, "xps_connection_create()", "created connection");
    return connection;

}

void xps_connection_destroy(xps_connection_t *connection) {

    /* validate params */
    assert(connection != NULL);

    /* set connection to NULL in 'connections' list */
    for(int i = 0; i < connection->core->connections.length; i++) {
        if(connection->core->connections.data[i] == connection) {
            connection->core->connections.data[i] = NULL;
            connection->core->n_null_connections++;
            break;
        }
    }

    /* detach connection from loop */
    xps_loop_detach(connection->core->loop, connection->sock_fd);

    /* close connection socket FD */
    close(connection->sock_fd);

    /* free connection->remote_ip */
    free(connection->remote_ip);

    /* free connection instance */
    free(connection);

    logger(LOG_DEBUG, "xps_connection_destroy()", "destroyed connection");

}

void connection_loop_read_handler(void* ptr) {

    /* validate params */
    assert(ptr != NULL);
    xps_connection_t* connection = ptr;
    u_char buff[DEFAULT_BUFFER_SIZE];
    long read_n = recv(connection->sock_fd, buff, sizeof(buff), 0);

    if (read_n < 0) {
        logger(LOG_ERROR, "xps_connection_read_handler()", "recv() failed");
        perror("Error message");
        xps_connection_destroy(connection);
        return;
    }

    if (read_n == 0) {
        logger(LOG_INFO, "connection_read_handler()", "peer closed connection");
        xps_connection_destroy(connection);
        return;
    }

    buff[read_n] = '\0';

    /* print client message */
    printf("Client message : %s\n", buff);

    /* reverse client message */
    for (long i = 0; i < read_n / 2; i++) {
        u_char temp = buff[i];
        buff[i] = buff[read_n - i - 1];
        buff[read_n - i - 1] = temp;
    }

    // Sending reversed message to client
    long bytes_written = 0;
    long message_len = read_n;
    while (bytes_written < message_len) {
        long write_n = send(connection->sock_fd, buff + bytes_written, message_len - bytes_written, 0);
        if (write_n < 0) {
            logger(LOG_ERROR, "xps_connection_read_handler()", "send() failed");
            perror("Error message");
            xps_connection_destroy(connection);
            return;
        }
        bytes_written += write_n;
    }

}