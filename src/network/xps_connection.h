#ifndef XPS_CONNECTION_H
#define XPS_CONNECTION_H

#include "../xps.h"

struct xps_connection_s {
  xps_core_t *core;
  int sock_fd;
  xps_listener_t *listener;
  char *remote_ip;
  xps_buffer_list_t *write_buff_list;
  bool read_ready;
  bool write_ready;
  xps_handler_t send_handler;
  xps_handler_t recv_handler;
};

xps_connection_t *xps_connection_create(xps_core_t *core, int sock_fd);
void xps_connection_destroy(xps_connection_t *connection);
void connection_loop_read_handler(void *ptr);
void connection_loop_write_handler(void *ptr);
void connection_loop_close_handler(void *ptr);
void connection_read_handler(void *ptr);
void connection_write_handler(void *ptr);
#endif