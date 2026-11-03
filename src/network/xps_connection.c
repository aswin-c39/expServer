#include "xps_connection.h"

xps_connection_t *xps_connection_create(xps_core_t *core, int sock_fd) {

    xps_connection_t *connection = malloc(sizeof(xps_connection_t));

    if (connection == NULL) {
        logger(LOG_ERROR, "xps_connection_create()", "malloc() failed for 'connection'");
        return NULL;
    }

    /* Initialize connection fields */
    connection->core = core;
    connection->sock_fd = sock_fd;
    connection->listener = NULL;
    connection->remote_ip = get_remote_ip(sock_fd);
    connection->write_buff_list = NULL;
    connection->read_ready = false;
    connection->write_ready = false;
    connection->send_handler = connection_write_handler;
    connection->recv_handler = connection_read_handler;

    /* Create the write buffer list */
    connection->write_buff_list = xps_buffer_list_create();

    if (connection->write_buff_list == NULL) {
        logger(LOG_ERROR, "xps_connection_create()",
            "failed to create write buffer list");
        free(connection);
        return NULL;
    }

    /* Attach socket to epoll */
    if (xps_loop_attach(
        core->loop,
        sock_fd,
        EPOLLIN | EPOLLOUT | EPOLLET,
        connection,
        connection_loop_read_handler,
        connection_loop_write_handler,
        connection_loop_close_handler
    ) == E_FAIL) {

        logger(LOG_ERROR, "xps_connection_create()",
            "failed to attach connection to epoll");

        xps_buffer_list_destroy(connection->write_buff_list);
        free(connection);
        return NULL;
    }

    vec_push(&core->connections, connection);

    logger(LOG_DEBUG, "xps_connection_create()",
        "created connection");

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

    /* free buffer list */
    xps_buffer_list_destroy(connection->write_buff_list);

    /* free connection instance */
    free(connection);

    logger(LOG_DEBUG, "xps_connection_destroy()", "destroyed connection");

}

void connection_read_handler(void *ptr) {
    assert(ptr != NULL);

    xps_connection_t *connection = ptr;
    char buffer[DEFAULT_BUFFER_SIZE];

    ssize_t read_n = recv(connection->sock_fd, buffer, sizeof(buffer), 0);

    if (read_n > 0) {
        char msg[512];
        snprintf(msg, sizeof(msg), "Received %zd bytes from client", read_n);
        logger(LOG_INFO, "connection_read_handler()", msg);
        // Print the actual received message
        fwrite(buffer, 1, read_n, stdout);
        printf("\n");
    }

    if (read_n == 0) {
        // Client disconnected gracefully
        xps_connection_destroy(connection);
        return;
    }

    if (read_n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            connection->read_ready = false;
            return;
        }
        logger(LOG_ERROR, "connection_read_handler()", "recv() failed");

        xps_connection_destroy(connection);
        return;
    }

    // Allocate memory for the reversed data
    u_char *reversed = malloc((size_t)read_n);

    if (reversed == NULL) {
        logger(LOG_ERROR, "connection_read_handler()", "malloc() failed");

        xps_connection_destroy(connection);
        return;
    }

    // Reverse the received bytes
    for (ssize_t i = 0; i < read_n; i++) {
        reversed[i] = (u_char)buffer[read_n - 1 - i];
    }

    // Create a buffer that owns the reversed data
    xps_buffer_t *buff = xps_buffer_create( (size_t)read_n, (size_t)read_n, reversed);

    if (buff == NULL) {
        free(reversed);
        logger(LOG_ERROR, "connection_read_handler()", "xps_buffer_create() failed");
        xps_connection_destroy(connection);
        return;
    }

    // Queue the reversed data for writing
    xps_buffer_list_append(connection->write_buff_list, buff);
}

void connection_write_handler(void *ptr) {
    assert(ptr != NULL);

    xps_connection_t *connection = ptr;
    xps_buffer_list_t *write_buff_list = connection->write_buff_list;

    // Nothing to send
    if (write_buff_list->len == 0)
        return;

    // Copy all pending data into a temporary buffer
    xps_buffer_t *buff = xps_buffer_list_read(write_buff_list, write_buff_list->len);

    if (buff == NULL) {
        logger(LOG_ERROR, "connection_write_handler()", "failed to read write buffer list");
        xps_connection_destroy(connection);
        return;
    }

    ssize_t write_n = send(connection->sock_fd, buff->data, buff->len, 0);

    if (write_n > 0) {
        // Remove only the bytes actually sent
        char msg[128];
        snprintf(msg, sizeof(msg), "Sent %zd bytes to client", write_n);

        logger(LOG_INFO, "connection_write_handler()", msg);
        xps_buffer_list_clear(write_buff_list, (size_t)write_n);
        xps_buffer_destroy(buff);
        return;
    }

    if (write_n < 0) {
        if((errno == EAGAIN || errno == EWOULDBLOCK)) {
            connection->write_ready = false;
            return;
        }
        // Socket's send buffer is full; retry on a later write event
        logger(LOG_ERROR, "connection_write_handler()", "send() failed");
        xps_connection_destroy(connection);
        return;
    }

    // A zero return for a nonempty send, or another error
    xps_buffer_destroy(buff);

    logger(LOG_ERROR, "connection_write_handler()", "send() failed");

    xps_connection_destroy(connection);
}

void connection_loop_close_handler(void *ptr) {
    assert(ptr != NULL);

    xps_connection_t *connection = ptr;

    logger(LOG_DEBUG, "connection_loop_close_handler()", "closing connection");

    xps_connection_destroy(connection);
}


void connection_loop_read_handler(void *ptr) {
    assert(ptr != NULL);

    xps_connection_t *connection = ptr;
    connection->read_ready = true;
}

void connection_loop_write_handler(void *ptr) {
    assert(ptr != NULL);

    xps_connection_t *connection = ptr;
    connection->write_ready = true;
}

