#include "xps_loop.h"

loop_event_t *loop_event_create(u_int fd, void *ptr, xps_handler_t read_cb) {
  assert(ptr != NULL);

  // Alloc memory for 'event' instance
  loop_event_t *event = malloc(sizeof(loop_event_t));
  if (event == NULL) {
    logger(LOG_ERROR, "event_create()", "malloc() failed for 'event'");
    return NULL;
  }

  /* set fd, ptr, read_cb fields of event */
  event->fd = fd;
  event->ptr = ptr;
  event->read_cb = read_cb;
  
  logger(LOG_DEBUG, "event_create()", "created event");

  return event;
}

void loop_event_destroy(loop_event_t *event) {
  assert(event != NULL);

  free(event);

  logger(LOG_DEBUG, "event_destroy()", "destroyed event");  
}

xps_loop_t *xps_loop_create(xps_core_t *core) {
    assert(core != NULL);

    int epoll_fd = epoll_create1(0);
    if(epoll_fd == -1) {
        logger(LOG_ERROR, "loop_create()", "epoll create failed");
        return NULL;
    }

    xps_loop_t *loop = (xps_loop_t *) malloc(sizeof(xps_loop_t));
    if(loop == NULL) {
        logger(LOG_ERROR, "loop_create()", "malloc failed for loop");
        return NULL;
    }

    loop->core = core;
    loop->epoll_fd = epoll_fd;
    loop->n_null_events = 0;

    vec_init(&loop->events);

    return loop;
}

void xps_loop_destroy(xps_loop_t *loop) {
    assert(loop != NULL);

    for(int i = 0; i < loop->events.length; i++) {
        loop_event_t *event = loop->events.data[i];
        if(event != NULL) {
            free(event);
        }
    }

    vec_deinit(&loop->events);

    close(loop->epoll_fd);

    free(loop);
}

int xps_loop_attach(xps_loop_t *loop, u_int fd, int event_flags, void *ptr, xps_handler_t read_cb) {
    assert(loop != NULL);
    assert(ptr != NULL);

    loop_event_t *event = loop_event_create(fd, ptr, read_cb);
    if(event == NULL) {
        return E_FAIL;
    }

    struct epoll_event ev = {0};
    ev.events = event_flags;
    ev.data.ptr = event;

    if(epoll_ctl(loop->epoll_fd, EPOLL_CTL_ADD, fd, &ev) == -1) {
        logger(LOG_ERROR, "loop_attach()", "epoll ctl failed");
        free(event);
        return E_FAIL;
    }

    if(vec_push(&loop->events, event) != 0) {
        epoll_ctl(loop->epoll_fd, EPOLL_CTL_DEL, fd, NULL);
        free(event);
        return E_FAIL;
    }

    return OK;
}   

int xps_loop_detach(xps_loop_t *loop, u_int fd) {
    assert(loop != NULL);

    for (int i = 0; i < loop->events.length; i++) {
        loop_event_t *event = loop->events.data[i];

        if (event == NULL)
            continue;

        if (event->fd == fd) {
            if (epoll_ctl(loop->epoll_fd, EPOLL_CTL_DEL, fd, NULL) == -1) {
                logger(LOG_ERROR, "xps_loop_detach()", "epoll_ctl() failed for fd %u", fd);
                return E_FAIL;
            }

            loop_event_destroy(event);
            loop->events.data[i] = NULL;
            loop->n_null_events++;

            logger(LOG_DEBUG, "xps_loop_detach()", "detached fd %u", fd);
            return OK;
        }
    }

    logger(LOG_ERROR, "xps_loop_detach()", "fd %u not found", fd);
    return E_FAIL;
}

void xps_loop_run(xps_loop_t *loop) {
    /* Validate params */
    assert(loop != NULL);
    while (1) {
        logger(LOG_DEBUG, "xps_loop_run()", "epoll wait");
        int n_events = epoll_wait(loop->epoll_fd, loop->epoll_events, MAX_EPOLL_EVENTS, -1);
        logger(LOG_DEBUG, "xps_loop_run()", "epoll wait over");

        if (n_events == -1) {
            if (errno == EINTR)
                continue;
            logger(LOG_ERROR, "xps_loop_run()", "epoll_wait() failed");
            break;
        }

        logger(LOG_DEBUG, "xps_loop_run()", "handling %d events", n_events);

        // Handle events
        for (int i = 0; i < n_events; i++) {
            logger(LOG_DEBUG, "xps_loop_run()", "handling event no. %d", i + 1);

            struct epoll_event curr_epoll_event = loop->epoll_events[i];
            loop_event_t *curr_event = curr_epoll_event.data.ptr;

            // Check if event still exists. Could have been destroyed due to prev event
            int curr_event_idx = -1;
            for(int j = 0; j < loop->events.length; j++) {
                if(loop->events.data[j] == curr_event) {
                    curr_event_idx = j;
                    break;
                }
            }
            // 🟡 Above can be optimized using an RB tree
            if (curr_event_idx == -1) {
                logger(LOG_DEBUG, "handle_epoll_events()", "event not found. skipping");
                continue;
            }

            // Read event
            if (curr_epoll_event.events & EPOLLIN) {
                logger(LOG_DEBUG, "handle_epoll_events()", "EVENT / read");
                if (curr_event->read_cb != NULL)
                    curr_event->read_cb(curr_event->ptr);
            }
        }
    }
}