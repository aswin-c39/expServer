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
    connection->source = NULL;
    connection->sink = NULL;

    if (connection->remote_ip == NULL) {
        logger(LOG_ERROR, "xps_connection_create()",
               "get_remote_ip() failed");
        free(connection);
        return NULL;
    }

    connection->source = xps_pipe_source_create(connection, connection_source_handler,  connection_source_close_handler);

    if (connection->source == NULL) {
        logger(LOG_ERROR, "xps_connection_create()", "xps_pipe_source_create() failed");
        free(connection->remote_ip);
        free(connection);
        return NULL;
    }

    connection->sink = xps_pipe_sink_create(connection, connection_sink_handler, connection_sink_close_handler);
    if (connection->sink == NULL) {
        logger(LOG_ERROR, "xps_connection_create()", "xps_pipe_sink_create() failed");
        xps_pipe_source_destroy(connection->source);
        free(connection->remote_ip);
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

        logger(LOG_ERROR, "xps_connection_create()", "failed to attach connection to epoll");
        xps_pipe_source_destroy(connection->source);
        xps_pipe_sink_destroy(connection->sink);
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

    // Destroy source and sink
    if (connection->source != NULL) {
        xps_pipe_source_destroy(connection->source);
    }

    if (connection->sink != NULL) {
        xps_pipe_sink_destroy(connection->sink);
    }

    /* close connection socket FD */
    close(connection->sock_fd);

    /* free connection->remote_ip */
    free(connection->remote_ip);

    /* free connection instance */
    free(connection);

    logger(LOG_DEBUG, "xps_connection_destroy()", "destroyed connection");

}



void connection_loop_close_handler(void *ptr) {
    assert(ptr != NULL);

    xps_connection_t *connection = ptr;

    logger(LOG_DEBUG, "connection_loop_close_handler()", "closing connection");

    connection_close(connection, true);
}


void connection_loop_read_handler(void *ptr) {
    assert(ptr != NULL);
    logger(LOG_DEBUG, "connection_loop_read_handler()",
           "socket is readable");
    xps_connection_t *connection = ptr;
    connection->source->ready = true;
}

void connection_loop_write_handler(void *ptr) {
    assert(ptr != NULL);
    logger(LOG_DEBUG, "connection_loop_write_handler()",
           "socket is writable");
    xps_connection_t *connection = ptr;
    connection->sink->ready = true;
}

void connection_source_handler(void *ptr) {
    /*assert ptr not null*/
    assert(ptr != NULL);
    logger(LOG_DEBUG, "connection_source_handler()",
       "source handler invoked");
    xps_pipe_source_t *source = ptr;
    xps_connection_t *connection = source->ptr;
    assert(connection != NULL);
    assert(source->pipe != NULL);

    xps_buffer_t *buff = xps_buffer_create(DEFAULT_BUFFER_SIZE, 0, NULL);
    if (buff == NULL) {
        logger(LOG_DEBUG, "connection_source_handler()", "xps_buffer_create() failed");
        return;
    }

    size_t remaining = source->pipe->buff_thresh - source->pipe->buff_list->len;
    size_t read_size = buff->size < remaining ? buff->size : remaining;

    /*Read from socket using recv()*/
    int read_n = recv(connection->sock_fd, buff->data, read_size, 0);  
    buff->len = read_n > 0 ? (size_t)read_n : 0;
    logger(LOG_INFO, "connection_source_handler()", "received message: %.*s", (int)read_n, buff->data);
    logger(LOG_DEBUG, "connection_source_handler()",
       "recv returned %zd, errno=%d",
       read_n, errno);
    // Socket would block
    if (read_n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        xps_buffer_destroy(buff);
        source->ready = false;
        return;
    }

    // Socket error
    if (read_n < 0) {
        /*destroy buff*/
        xps_buffer_destroy(buff);
        logger(LOG_ERROR, "connection_source_handler()", "recv() failed");
        connection_close(connection, false);
        return;
    }

    // Peer closed connection
    if (read_n == 0) {
        /*destroy buff*/
        xps_buffer_destroy(buff);
        /*close connection*/
        connection_close(connection, true);
        return;
    }

    if (xps_pipe_source_write(source, buff) != OK) {
        logger(LOG_ERROR, "connection_source_handler()", "xps_pipe_source_write() failed");
        /*destroy buff*/
        xps_buffer_destroy(buff);
        /*close connection*/
        connection_close(connection, false);
        return;
    }
    logger(LOG_DEBUG, "connection_source_handler()",
       "wrote data to pipe, pipe buffer length=%zu",
       source->pipe->buff_list->len);  

    xps_buffer_destroy(buff);
}

void connection_source_close_handler(void *ptr) {
    /*assert*/
    assert(ptr != NULL);
    xps_pipe_source_t *source = ptr;
    xps_connection_t *connection = source->ptr;

    if (!source->active && !connection->sink->active) {
        connection_close(connection, true);
    }
    /*close connection*/
}

void connection_sink_handler(void *ptr) {
    /*assert*/
    logger(LOG_DEBUG, "connection_sink_handler()",
       "sink handler invoked");
    logger(LOG_DEBUG, "connection_sink_handler()",
       "sink handler invoked");
    assert(ptr != NULL);
    xps_pipe_sink_t *sink = ptr;
    xps_connection_t *connection = sink->ptr;

    assert(connection != NULL);
    assert(sink->pipe != NULL);

    size_t available = sink->pipe->buff_list->len;

    xps_buffer_t *buff = xps_pipe_sink_read(sink, available);
    if (buff == NULL) {
        logger(LOG_ERROR, "connection_sink_handler()", "xps_pipe_sink_read() failed");
        return;
    }

    // Write to socket
    int write_n = send(connection->sock_fd, buff->data, buff->len, MSG_NOSIGNAL);
    logger(LOG_DEBUG, "connection_sink_handler()",
       "send returned %zd, errno=%d",
       write_n, errno);
    /*destroy buff*/
    xps_buffer_destroy(buff);
    // Socket would block
    if (write_n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        /*sink made not ready*/
        sink->ready = false;
        return;
    }

    // Socket error
    if (write_n < 0) {
        logger(LOG_ERROR, "connection_sink_handler()", "send() failed");
        /*close connection*/
        connection_close(connection, false);
        return;
    }

    if (write_n == 0)
        return;

    if (xps_pipe_sink_clear(sink, write_n) != OK)
    logger(LOG_ERROR, "connection_sink_handler()", "failed to clear %d bytes from sink", write_n);
}

void connection_sink_close_handler(void *ptr) {
    /*assert*/
    assert(ptr != NULL);
    xps_pipe_sink_t *sink = ptr;
    xps_connection_t *connection = sink->ptr;

    if (!connection->source->active && !sink->active) {
        connection_close(connection, true);
    }
}

void connection_close(xps_connection_t *connection, bool peer_closed) {
    /*assert*/
    assert(connection != NULL);
    logger(LOG_INFO, "connection_close()", peer_closed ? "peer closed connection" : "closing connection");
    /*destroy connection*/
    xps_connection_destroy(connection);
}