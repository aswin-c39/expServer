#include "../xps.h"

int http_process_request_line(xps_http_req_t *http_req, xps_buffer_t *buff) {
    assert(http_req != NULL);
    assert(buff != NULL);

    int error = xps_http_parse_request_line(http_req, buff);
    if (error != OK) {
        return error;
    }

    http_req->request_line = str_from_ptrs((const char *)http_req->request_line_start, (const char *)http_req->request_line_end);
    if (http_req->request_line == NULL) return E_FAIL;

    http_req->method = str_from_ptrs((const char *)http_req->method_start, (const char *)http_req->method_end);
    if (http_req->method == NULL) return E_FAIL;

    http_req->uri = str_from_ptrs((const char *)http_req->uri_start, (const char *)http_req->uri_end);
    if (http_req->uri == NULL) return E_FAIL;

    if (http_req->schema_start != NULL && http_req->schema_end != NULL) {
        http_req->schema = str_from_ptrs((const char *)http_req->schema_start, (const char *)http_req->schema_end);
        if (http_req->schema == NULL) return E_FAIL;
    }

    if (http_req->host_start != NULL && http_req->host_end != NULL) {
        http_req->host = str_from_ptrs((const char *)http_req->host_start, (const char *)http_req->host_end);
        if (http_req->host == NULL) return E_FAIL;
    }

    http_req->path = str_from_ptrs((const char *)http_req->path_start, (const char *)http_req->path_end);
    if (http_req->path == NULL) return E_FAIL;

    http_req->pathname = str_from_ptrs((const char *)http_req->pathname_start, (const char *)http_req->pathname_end);
    if (http_req->pathname == NULL) return E_FAIL;

    /* Construct the HTTP version string, e.g. "1.1". */
    http_req->http_version = malloc(4);
    if (http_req->http_version == NULL) {
        logger(LOG_ERROR, "http_process_request_line()", "malloc() failed for http_version");
        return E_FAIL;
    }

    snprintf(http_req->http_version, 4, "%c.%c", *http_req->http_major, *http_req->http_minor);

    /* Determine the port number. */
    http_req->port = -1;

    if (http_req->port_start != NULL && http_req->port_end != NULL && http_req->port_start < http_req->port_end) {
        char *port_str = str_from_ptrs((const char *)http_req->port_start, (const char *)http_req->port_end);
        if (port_str == NULL) return E_FAIL;

        http_req->port = atoi(port_str);
        free(port_str);
    } else if (http_req->schema == NULL) {
        http_req->port = 80;
    } else {
        if (strcmp(http_req->schema, "http") == 0) {
            http_req->port = 80;
        } else if (strcmp(http_req->schema, "https") == 0) {
            http_req->port = 443;
        } else {
            logger(LOG_ERROR, "http_process_request_line()", "unknown URI schema");
            return E_FAIL;
        }
    }
    printf("REQUEST LINE: %s\n", http_req->request_line);
    printf("METHOD: %s\n", http_req->method);
    printf("URI: %s\n", http_req->uri);
    printf("SCHEMA: %s\n", http_req->schema);
    printf("HOST: %s\n", http_req->host);
    printf("PATH: %s\n", http_req->path);
    printf("PATHNAME: %s\n", http_req->pathname);
    printf("HTTP VERSION: %s\n", http_req->http_version);
    printf("PORT: %d\n", http_req->port);

    return OK;
}

int http_process_headers(xps_http_req_t *http_req, xps_buffer_t *buff) {
    assert(http_req != NULL);
    assert(buff != NULL);

    vec_init(&http_req->headers);

    int error;

    while (1) {
        error = xps_http_parse_header_line(http_req, buff);

        if (error == E_AGAIN) {
            return E_AGAIN;
        }

        if (error == E_FAIL) {
            goto error_cleanup;
        }

        if (error == E_NEXT || error == OK) {
            /* A header is available only if its key pointers are set. */
            if (http_req->header_key_start != NULL && http_req->header_key_end != NULL) {
                xps_keyval_t *header = malloc(sizeof(xps_keyval_t));
                if (header == NULL) goto error_cleanup;

                header->key = str_from_ptrs((const char *)http_req->header_key_start, (const char *)http_req->header_key_end);
                header->val = str_from_ptrs((const char *)http_req->header_val_start, (const char *)http_req->header_val_end);

                if (header->key == NULL || header->val == NULL) {
                    free(header->key);
                    free(header->val);
                    free(header);
                    goto error_cleanup;
                }

                if (vec_push(&http_req->headers, header) != 0) {
                    free(header->key);
                    free(header->val);
                    free(header);
                    goto error_cleanup;
                }
            }
            printf("HEADERS\n");
                for (int i = 0; i < http_req->headers.length; i++) {
                    xps_keyval_t *header = http_req->headers.data[i];
                    /*print key and val*/
                    printf("  %s: %s\n", header->key, header->val);
                }

            if (error == OK) {
                return OK;
            }
        }
    }

error_cleanup:
    for (int i = 0; i < http_req->headers.length; i++) {
        xps_keyval_t *header = http_req->headers.data[i];
        if (header != NULL) {
            free(header->key);
            free(header->val);
            free(header);
        }
    }

    vec_deinit(&http_req->headers);
    return E_FAIL;
}


xps_buffer_t *xps_http_req_serialize(xps_http_req_t *http_req) {
    assert(http_req != NULL);

    xps_buffer_t *headers_str = xps_http_serialize_headers(&http_req->headers);
    if (headers_str == NULL) {
        logger(LOG_ERROR, "xps_http_req_serialize()", "failed to serialize headers");
        return NULL;
    }

    size_t request_line_len = strlen(http_req->request_line);
    size_t final_len = request_line_len + 1 + headers_str->len + 1;

    xps_buffer_t *buff = xps_buffer_create(final_len, 0, NULL);
    if (buff == NULL) {
        logger(LOG_ERROR, "xps_http_req_serialize()", "failed to create buffer");
        xps_buffer_destroy(headers_str);
        return NULL;
    }

    memcpy(buff->pos, http_req->request_line, request_line_len);
    buff->pos += request_line_len;
    buff->len += request_line_len;

    memcpy(buff->pos, "\n", 1);
    buff->pos += 1;
    buff->len += 1;

    memcpy(buff->pos, headers_str->data, headers_str->len);
    buff->pos += headers_str->len;
    buff->len += headers_str->len;

    memcpy(buff->pos, "\n", 1);
    buff->pos += 1;
    buff->len += 1;

    xps_buffer_destroy(headers_str);

    return buff;
}

xps_http_req_t *xps_http_req_create(xps_core_t *core, xps_buffer_t *buff, int *error) {
    /*assert*/
    assert(core != NULL);
    assert(buff != NULL);
    *error = E_FAIL;
    /* Alloc memory for http_req instance*/
    xps_http_req_t *http_req = (xps_http_req_t *)malloc(sizeof(xps_http_req_t));
    if (http_req == NULL) {
        logger(LOG_ERROR, "xps_http_req_create()", "malloc() failed for http_req");
        return NULL;
    }

    memset(http_req, 0, sizeof(xps_http_req_t));

    /*Set initial parser state*/
    http_req->parser_state = RL_START;
    /*Process request line and handle possible errors*/
    int error_code = http_process_request_line(http_req, buff);
    if(error_code != OK) {
        logger(LOG_ERROR, "xps_http_req_create()", "http_process_request_line() failed");
        xps_http_req_destroy(core, http_req);
        *error = error_code;
        return NULL;
    }

    /*Process headers and handle possible errors*/
    error_code = http_process_headers(http_req, buff);
    if(error_code != OK) {
        logger(LOG_ERROR, "xps_http_req_create()", "http_process_headers() failed");
        xps_http_req_destroy(core, http_req);
        *error = error_code;
        return NULL;
    }

    if (strcmp(http_req->http_version, "1.1") == 0 &&
        xps_http_get_header(&http_req->headers, "Host") == NULL) {
        logger(LOG_ERROR, "xps_http_req_create()", "missing Host header");
        xps_http_req_destroy(core, http_req);
        return NULL;
    }

    // Header length
    http_req->header_len = (size_t)(buff->pos - buff->data);
    // Body length is retrieved from header Content-Length
    http_req->body_len = 0;
    const char *body_len_str = xps_http_get_header(&http_req->headers, "Content-Length");
    /*assign body_len*/
    if (body_len_str != NULL) {
        char *end_ptr;
        unsigned long long body_len = strtoull(body_len_str, &end_ptr, 10);

        if (body_len_str[0] == '\0' || *end_ptr != '\0' || body_len > SIZE_MAX) {
            logger(LOG_ERROR, "xps_http_req_create()", "invalid Content-Length header");
            xps_http_req_destroy(core, http_req);
            return NULL;
        }

        http_req->body_len = (size_t)body_len;
    }

    *error = OK;
    return http_req;
}

void xps_http_req_destroy(xps_core_t *core, xps_http_req_t *http_req) {
    assert(http_req != NULL);
    /*Frees memory allocated for various components of the HTTP request line(request line, method,etc)*/
    free(http_req->request_line);
    free(http_req->method);
    free(http_req->uri);
    free(http_req->schema);
    free(http_req->host);
    free(http_req->path);
    free(http_req->pathname);
    free(http_req->http_version);   
    /*iterate through the headers list of http_req and free the memory*/
    for (int i = 0; i < http_req->headers.length; i++) {
        xps_keyval_t *header = http_req->headers.data[i];
        if (header != NULL) {
            free(header->key);
            free(header->val);
            free(header);
        }
    }

    /*de-intialize the headers list*/
    vec_deinit(&http_req->headers);
    /*free http_req*/
    free(http_req);
}