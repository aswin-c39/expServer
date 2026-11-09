#ifndef XPS_CONNECTION_H
#define XPS_CONNECTION_H

#include "../xps.h"

typedef struct xps_pipe_source_s xps_pipe_source_t;
typedef struct xps_pipe_sink_s xps_pipe_sink_t;

struct xps_connection_s {
  xps_core_t *core;
  int sock_fd;
  xps_listener_t *listener;
  char *remote_ip;
  xps_pipe_source_t *source;
  xps_pipe_sink_t *sink;
};

xps_connection_t *xps_connection_create(xps_core_t *core, int sock_fd);
void xps_connection_destroy(xps_connection_t *connection);
void connection_loop_read_handler(void *ptr);
void connection_loop_write_handler(void *ptr);
void connection_loop_close_handler(void *ptr);
void connection_source_handler(void *ptr);
void connection_source_close_handler(void *ptr);
void connection_sink_handler(void *ptr);
void connection_sink_close_handler(void *ptr);
void connection_close(xps_connection_t *connection, bool peer_closed);
#endif