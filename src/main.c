#include "xps.h"
// Global variables
int epoll_fd;
struct epoll_event events[MAX_EPOLL_EVENTS];
vec_void_t listeners;
vec_void_t connections;

int main() {

    epoll_fd = xps_loop_create();

    // Init lists
    vec_init(&listeners);
    vec_init(&connections);

    // Create listeners on ports 8001, 8002, 8003
    for (int port = 8001; port <= 8003; port++) {
        /* create listener instance using xps_listener_create() */
        xps_listener_create(epoll_fd,"0.0.0.0", port);
        logger(LOG_INFO, "main()", "Server listening on port %u", port);
    }

    /* run the event loop using xps_loop_run() */
    xps_loop_run(epoll_fd);
}

int xps_loop_create() {
    /* create a loop instance and return epoll FD */
    int fd = epoll_create1(0);
    if (fd == -1) {
        logger(LOG_ERROR, "xps_loop_create()", "epoll_create1() failed");
        perror("Error message");
        exit(EXIT_FAILURE);
    }
    return fd;
}

void xps_loop_attach(int epoll_fd, int fd, int events) {
    /* attach fd to epoll */
    struct epoll_event event;
    memset(&event, 0, sizeof(event));

    event.events = events;
    event.data.fd = fd;

    if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event) == -1) {
        logger(LOG_ERROR, "xps_loop_attach()", "epoll_ctl() failed");
        perror("Error message");
        exit(EXIT_FAILURE);
    }
}

void xps_loop_detach(int epoll_fd, int fd) {
    /* detach fd from epoll */
    if(epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL) == -1) {
        logger(LOG_ERROR, "xps_loop_detach()", "epoll_ctl() failed");
        perror("Error message");
    }
}

void xps_loop_run(int epoll_fd) {
    /* run the event loop */
    while(1) {

        int n_ready_fds = epoll_wait(epoll_fd, events, MAX_EPOLL_EVENTS, -1);
        if(n_ready_fds == -1) {
            logger(LOG_ERROR, "xps_loop_run()", "epoll_wait() failed");
            perror("Error message");
            continue;
        }
        
        for(int i = 0; i < n_ready_fds; i++) {
            int curr_fd = events[i].data.fd;

            xps_listener_t *listener = NULL;

            for(int j = 0; j < listeners.length; j++) {
                xps_listener_t *curr_listener = listeners.data[j];
                if(curr_listener != NULL && curr_listener->sock_fd == curr_fd) {
                    listener = curr_listener;
                    break;
                }
            }

            if(listener != NULL) {
                xps_listener_connection_handler(listener);
                continue;
            }

            xps_connection_t *connection = NULL;

            for(int j = 0; j < connections.length; j++) {
                xps_connection_t *curr_connection = connections.data[j];

                if(curr_connection != NULL && curr_connection->sock_fd == curr_fd) {
                    connection = curr_connection;
                    break;
                }
            }

            if(connection != NULL) {
                xps_connection_read_handler(connection);
                continue;
            }

            logger(LOG_ERROR, "xps_loop_run()", "unknown file descriptor: %d", curr_fd);
        }
    }
}