#include "xps_listener.h"

void listener_connection_handler(void *ptr);

xps_listener_t *xps_listener_create(xps_core_t* core, const char *host, u_int port) {
  assert(host != NULL);
  assert(is_valid_port(port)); // Will be explained later

  // Create socket instance
  int sock_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
  if (sock_fd < 0) {
    logger(LOG_ERROR, "xps_listener_create()", "socket() failed");
    perror("Error message");
    return NULL;
  }

  // Make address reusable
  const int enable = 1;
  if (setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
    logger(LOG_ERROR, "xps_listener_create()", "setsockopt() failed");
    perror("Error message");
    close(sock_fd);
    return NULL;
  }

  // Setup listener address
  struct addrinfo *addr_info = xps_getaddrinfo(host, port); // Will be explained later
  if (addr_info == NULL) {
    logger(LOG_ERROR, "xps_listener_create()", "xps_getaddrinfo() failed");
    close(sock_fd);
    return NULL;
  }

  // Binding to port
  if (bind(sock_fd, addr_info->ai_addr, addr_info->ai_addrlen) < 0) {
    logger(LOG_ERROR, "xps_listener_create()", "failed to bind() to %s:%u", host, port);
    perror("Error message");
    freeaddrinfo(addr_info); // Will be explained later
    close(sock_fd);
    return NULL;
  }
  freeaddrinfo(addr_info); // Will be explained later

  // Listening on port
  if (listen(sock_fd, DEFAULT_BACKLOG) < 0) {
    logger(LOG_ERROR, "xps_listener_create()", "listen() failed");
    perror("Error message");
    close(sock_fd);
    return NULL;
  }


  // Create & allocate memory for a listener instance
  xps_listener_t *listener = malloc(sizeof(xps_listener_t));
  if (listener == NULL) {
    logger(LOG_ERROR, "xps_listener_create()", "malloc() failed for 'listener'");
    close(sock_fd);
    return NULL;
  }

  // Init values
  listener->core = core;
  listener->host = host;
  listener->port = port;
  listener->sock_fd = sock_fd;

  // Attach listener to loop
if (xps_loop_attach(core->loop, sock_fd, EPOLLIN | EPOLLET , listener, listener_connection_handler, NULL, NULL) == E_FAIL) {
    logger(LOG_ERROR, "xps_listener_create()","failed to attach listener to epoll");
    close(sock_fd);
    free(listener);
    return NULL;
}

  // Add listener to global listeners list
  vec_push(&(core->listeners), listener);

  logger(LOG_DEBUG, "xps_listener_create()", "created listener on port %d", port);

  return listener;
}

void xps_listener_destroy(xps_listener_t *listener) {

  // Validate params
  assert(listener != NULL);

  // Detach listener from loop
  xps_loop_detach(listener->core->loop, listener->sock_fd);

  // Set listener to NULL in 'listeners' list
  for (int i = 0; i < listener->core->listeners.length; i++) {
    if (listener->core->listeners.data[i] == listener) {
      listener->core->listeners.data[i] = NULL;
      listener->core->n_null_listeners++;
      break;
    }
  }

  // Close socket
  close(listener->sock_fd);

  logger(LOG_DEBUG, "xps_listener_destroy()", "destroyed listener on port %d", listener->port);

  // Free listener instance
  free(listener);

}

void listener_connection_handler(void* ptr) {
    assert(ptr != NULL);
    xps_listener_t *listener = ptr;
    struct sockaddr conn_addr;

    // Accepting connection
    while(1) {
        socklen_t conn_addr_len = sizeof(conn_addr);
        int conn_sock_fd = accept(listener->sock_fd, (struct sockaddr *)&conn_addr, &conn_addr_len);

        if(conn_sock_fd < 0) {
            if(errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            logger(LOG_ERROR, "xps_listener_connection_handler()", "accept() failed");
            perror("Error message");
            break;
        }

        if(make_socket_non_blocking(conn_sock_fd) == E_FAIL) {
            logger(LOG_ERROR, "listener_connection_handler()", "failed to make socket nonblocking");
            close(conn_sock_fd);
            continue;
        }

        // Creating client connection instance
        xps_connection_t *client = xps_connection_create(listener->core, conn_sock_fd);

        if(client == NULL) {
            logger(LOG_ERROR, "xps_listener_connection_handler()", "xps_connection_create() failed");
            close(conn_sock_fd);
            continue;
        }

        client->listener = listener;

        // Handle connection based on listener port
        if(listener->port == 8001) {

            // Create upstream connection to 127.0.0.1:3000
            xps_connection_t *upstream = xps_upstream_create(listener->core, "127.0.0.1", 3000);

            if(upstream == NULL) {
                logger(LOG_ERROR, "listener_connection_handler()", "xps_upstream_create() failed");
                xps_connection_destroy(client);
                continue;
            }

            // Create pipe from client to upstream
            if(xps_pipe_create(listener->core, DEFAULT_PIPE_BUFF_THRESH, client->source, upstream->sink) == NULL) {
                logger(LOG_ERROR, "listener_connection_handler()", "failed to create client-to-upstream pipe");
                xps_connection_destroy(client);
                xps_connection_destroy(upstream);
                continue;
            }

            // Create pipe from upstream to client
            if(xps_pipe_create(listener->core, DEFAULT_PIPE_BUFF_THRESH, upstream->source, client->sink) == NULL) {
                logger(LOG_ERROR, "listener_connection_handler()", "failed to create upstream-to-client pipe");
                xps_connection_destroy(client);
                xps_connection_destroy(upstream);
                continue;
            }

        } else {

            // Create pipe for direct connection (same as previous stages)
            if(xps_pipe_create(listener->core, DEFAULT_PIPE_BUFF_THRESH, client->source, client->sink) == NULL) {
                logger(LOG_ERROR, "listener_connection_handler()", "xps_pipe_create() failed");
                xps_connection_destroy(client);
                continue;
            }
        }

        logger(LOG_INFO, "listener_connection_handler()", "new connection");
    }
}