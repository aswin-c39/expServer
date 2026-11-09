#ifndef XPS_MIME_H
#define XPS_MIME_H

#include "../xps.h"
struct xps_keyval_s {
  char *key;
  char *val;
};
typedef struct xps_keyval_s xps_keyval_t;
const char *xps_get_mime(const char *file_path);

#endif