#include "xps_loop.h"

loop_event_t *loop_event_create(u_int fd, void *ptr, xps_handler_t read_cb, xps_handler_t write_cb, xps_handler_t close_cb) {
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
  event->write_cb = write_cb;
  event->close_cb = close_cb;
  
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

int xps_loop_attach(xps_loop_t *loop, u_int fd, int event_flags, void *ptr, xps_handler_t read_cb, xps_handler_t write_cb, xps_handler_t close_cb) {
    assert(loop != NULL);
    assert(ptr != NULL);

    loop_event_t *event = loop_event_create(fd, ptr, read_cb, write_cb, close_cb);
    if (event == NULL) {
        logger(LOG_ERROR, "xps_loop_attach()", "event creation failed");
        return E_FAIL;
    }

    struct epoll_event ev = {0};
    ev.events = event_flags;
    ev.data.ptr = event;

    if(epoll_ctl(loop->epoll_fd, EPOLL_CTL_ADD, fd, &ev) == -1) {
        logger(LOG_ERROR, "loop_attach()", "epoll ctl failed");
        loop_event_destroy(event);
        return E_FAIL;
    }

    if(vec_push(&loop->events, event) != 0) {
        logger(LOG_ERROR, "xps_loop_attach()", "vec_push() failed");
        epoll_ctl(loop->epoll_fd, EPOLL_CTL_DEL, fd, NULL);
        loop_event_destroy(event);
        return E_FAIL;
    }

    logger(LOG_DEBUG, "xps_loop_attach()", "attached event");

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
    assert(loop != NULL);

    logger(LOG_DEBUG, "xps_loop_run()", "starting to run loop");

    while (1) {
        logger(LOG_DEBUG, "xps_loop_run()", "loop top");

        // Handle pipes
        bool has_ready_pipes = handle_pipes(loop);

        // Don't block if there are ready pipes.
        int timeout = has_ready_pipes ? 0 : -1;

        logger(LOG_DEBUG, "xps_loop_run()", "epoll waiting");

        int n_events = epoll_wait(loop->epoll_fd, loop->epoll_events, MAX_EPOLL_EVENTS, timeout);

        logger(LOG_DEBUG, "xps_loop_run()", "epoll wait over");

        if (n_events < 0) {
            if (errno == EINTR) {
                // Interrupted by a signal; retry the event loop.
                continue;
            }

            logger(LOG_ERROR, "xps_loop_run()", "epoll_wait() error");
            break;
        }

        // Handle epoll events
        if (n_events > 0)
            handle_epoll_events(loop, n_events);

        // Filter NULLs from vec lists
        filter_nulls(loop->core);
    }
}
bool handle_pipes(xps_loop_t *loop) {
    assert(loop != NULL);
    for (int i = 0; i < loop->core->pipes.length; i++) {
        xps_pipe_t *pipe = loop->core->pipes.data[i];
        
        if (pipe == NULL)
            continue;
        logger(LOG_DEBUG, "handle_pipes()",
       "pipe buffer length=%zu, source ready=%d, sink ready=%d",
       pipe->buff_list->len,
       pipe->source ? pipe->source->ready : -1,
       pipe->sink ? pipe->sink->ready : -1);
        /*Destroy the pipe if it has no source and sink and continue*/
        if (pipe->source == NULL && pipe->sink == NULL) {
            xps_pipe_destroy(pipe);
            logger(LOG_ERROR, "handle_pipes()", "pipe has no  source and sink.");
            continue;
        }
        /* Source is ready and pipe has space. */
        if (pipe->source != NULL && pipe->source->ready && xps_pipe_is_writable(pipe)){
            pipe->source->handler_cb(pipe->source);//call connection_source_handler to write into  pipe
        }
        /*Pipe has sink AND sink is ready AND pipe is readable*/
        if (pipe->sink != NULL && pipe->sink->ready && xps_pipe_is_readable(pipe)) {
            logger(LOG_DEBUG, "handle_pipes()",
       "invoking sink handler");
            pipe->sink->handler_cb(pipe->sink);//call connection_sink_handler to read from pipe
        }
        /*Pipe has source and no sink*/
        if (pipe->source != NULL && pipe->sink == NULL) {
            pipe->source->active = false;
            pipe->source->close_cb(pipe->source);
        }
        /*Pipe has sink and no source and pipe is not readable*/
        if (pipe->sink != NULL && pipe->source == NULL && !xps_pipe_is_readable(pipe)) {
            pipe->sink->active = false;
            pipe->sink->close_cb(pipe->sink);
        }

    }

    for (int i = 0; i < loop->core->pipes.length; i++) {
        xps_pipe_t *pipe = loop->core->pipes.data[i];
        if (pipe == NULL){
            logger(LOG_DEBUG, "handle_pipes", "pipe is null");
            continue;
        }
        /*Pipe has source AND source is ready AND pipe is writable*/
        if (pipe->source != NULL && pipe->source->ready && xps_pipe_is_writable(pipe)){
            return true;
        }
        /*Pipe has sink AND sink is ready AND pipe is readable*/
        if (pipe->sink != NULL && pipe->sink->ready && xps_pipe_is_readable(pipe)) {
            return true;
        }
        /*Pipe has source and no sink*/
        if (pipe->source != NULL && pipe->sink == NULL) {
            return true;
        }
        /*Pipe has sink and no source and pipe is not readable*/
        if (pipe->sink != NULL && pipe->source == NULL && !xps_pipe_is_readable(pipe)) {
            return true;
        }
    }
    return false;
}

void filter_nulls(xps_core_t *core) {
    assert(core != NULL);

    /* Filter NULL entries from loop events. */
    if (core->loop->n_null_events > DEFAULT_NULLS_THRESH) {
        vec_filter_null(&core->loop->events);
        core->loop->n_null_events = 0;

        logger(LOG_DEBUG, "filter_nulls()", "filtered NULL events");
    }

    /* Filter NULL entries from listeners. */
    if (core->n_null_listeners > DEFAULT_NULLS_THRESH) {
        vec_filter_null(&core->listeners);
        core->n_null_listeners = 0;

        logger(LOG_DEBUG, "filter_nulls()", "filtered NULL listeners");
    }

    /* Filter NULL entries from connections. */
    if (core->n_null_connections > DEFAULT_NULLS_THRESH) {
        vec_filter_null(&core->connections);
        core->n_null_connections = 0;

        logger(LOG_DEBUG, "filter_nulls()", "filtered NULL connections");
    }

    /* Filter NULL entries from pipes. */
    if (core->n_null_pipes > DEFAULT_NULLS_THRESH) {
        vec_filter_null(&core->pipes);
        core->n_null_pipes = 0;

        logger(LOG_DEBUG, "filter_nulls()", "filtered NULL pipes");
    }
}

void handle_epoll_events(xps_loop_t *loop, int n_events) {
    assert(loop != NULL);

    logger(LOG_DEBUG, "handle_epoll_events()", "handling %d events", n_events);

    for (int i = 0; i < n_events; i++) {
        logger(LOG_DEBUG, "handle_epoll_events()", "handling event no. %d", i + 1);

        struct epoll_event *event = &loop->epoll_events[i];
        logger(LOG_DEBUG, "handle_epoll_events()",
       "fd=%d, event flags=0x%x",
       ((loop_event_t *)event->data.ptr)->fd,
       event->events);

        loop_event_t *loop_event = event->data.ptr;

        if (loop_event == NULL)
            continue;

        /* Handle read event. */
        if (event->events & EPOLLIN) {
            if (loop_event->read_cb != NULL)
                loop_event->read_cb(loop_event->ptr);
        }

        /* Handle write event. */
        if (event->events & EPOLLOUT) {
            if (loop_event->write_cb != NULL)
                loop_event->write_cb(loop_event->ptr);
        }

        /* Handle connection errors or closure. */
        if (event->events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
            if (loop_event->close_cb != NULL)
                loop_event->close_cb(loop_event->ptr);
        }
    }
}