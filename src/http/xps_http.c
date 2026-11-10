#include "../xps.h"

bool http_strcmp(u_char *str, const char *method, size_t length){
    /* complete this */
    assert(str != NULL);
    assert(method != NULL);

    return memcmp(str, method, length) == 0;
}

int xps_http_parse_request_line(xps_http_req_t *http_req, xps_buffer_t *buff) {
    assert(http_req != NULL);
    assert(buff != NULL);
    /*get current parser state*/
    xps_http_parser_state_t parser_state = http_req->parser_state;
    u_char *p_ch = buff->pos;//current buffer postion
    u_char *buffer_end = buff->data + buff->len;
    for (; p_ch < buffer_end; p_ch++) {
        char ch = *p_ch;
        switch (parser_state) {
            case RL_START:
                /*assign request_line_start, ignore CR and LF, fail if not upper case, assign method_start*/
                if(ch == CR || ch == LF)
                    break;
                if(ch < 'A' || ch > 'Z')
                    return E_FAIL;
                http_req->request_line_start = p_ch;
                http_req->method_start = p_ch;
                parser_state = RL_METHOD;
                break;

            case RL_METHOD:
                if (ch == ' ') {
                    size_t method_len = p_ch - http_req->method_start;
                    if (method_len == 3 && http_strcmp(http_req->method_start, "GET", 3))
                        http_req->method_n = HTTP_GET;
                    else if (method_len == 4 && http_strcmp(http_req->method_start, "HEAD", 4))
                        http_req->method_n = HTTP_HEAD;
                    else if (method_len == 4 && http_strcmp(http_req->method_start, "POST", 4))
                        http_req->method_n = HTTP_POST;
                    else if (method_len == 3 && http_strcmp(http_req->method_start, "PUT", 3))
                        http_req->method_n = HTTP_PUT;
                    else if (method_len == 6 && http_strcmp(http_req->method_start, "DELETE", 6))
                        http_req->method_n = HTTP_DELETE;
                    else if (method_len == 7 && http_strcmp(http_req->method_start, "OPTIONS", 7))
                        http_req->method_n = HTTP_OPTIONS;
                    else if (method_len == 5 && http_strcmp(http_req->method_start, "TRACE", 5))
                        http_req->method_n = HTTP_TRACE;
                    else if (method_len == 7 && http_strcmp(http_req->method_start, "CONNECT", 7))
                        http_req->method_n = HTTP_CONNECT;
                    else
                        return E_FAIL;
                    http_req->method_end = p_ch;
                    parser_state = RL_SP_AFTER_METHOD;
                } else if (ch < 'A' || ch > 'Z') {
                    return E_FAIL;
                }
                break;

            case RL_SP_AFTER_METHOD:
                if (ch == '/') {
                    /*assign start and end of schema,host,port and start of uri,path,pathname*/
                    /*next state is RL_PATH*/
                    http_req->uri_start = p_ch;
                    http_req->path_start = p_ch;
                    http_req->pathname_start = p_ch;
                    parser_state = RL_PATH;
                } else {
                char c = ch | 0x20; //convert to lower case
                    /*if lower case alphabets, assign start of schema and uri, next state is RL_SCHEMA*/
                    if(c >= 'a' && c <= 'z') {
                        http_req->schema_start = p_ch;
                        http_req->uri_start = p_ch;
                        parser_state = RL_SCHEMA;
                    } else {
                        return E_FAIL;
                    /*if not space(''), fails*/
                    }
                }
                break;

            case RL_SCHEMA:
                /*on lower case alphabets break ie schema can have lower case alphabets*/
                if(ch >= 'a' && ch <= 'z' || ch > 'A' && ch < 'Z') {
                    break;
                } else if(ch == ':') {
                    http_req->schema_end = p_ch;
                    parser_state = RL_SCHEMA_SLASH;
                } else {
                    return E_FAIL;
                }
                break;

            case RL_SCHEMA_SLASH:
                /*on '/' assign next state, fails on all other inputs*/
                if(ch == '/') {
                    parser_state = RL_SCHEMA_SLASH_SLASH;
                } else {
                    return E_FAIL;
                }
                break;

            case RL_SCHEMA_SLASH_SLASH:
                /*on '/' - assign next state, for start of host assign next position, fails on all other inputs*/
                if(ch == '/') {
                    parser_state = RL_HOST_START;
                } else {
                    return E_FAIL;
                }
                break;
            case RL_HOST_START:
                if(ch == '[') {
                    http_req->host_start = p_ch + 1;
                    parser_state = RL_HOST_IP_LITERAL;
                } else if((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')) {
                    http_req->host_start = p_ch;
                    parser_state = RL_HOST;
                } else {
                    return E_FAIL;
                }
                break;
            case RL_HOST:
                /*host can have lower case alphabets, numbers, '-', '.' */
                /*on ':' - host ends, for start of port assign next position, next state is RL_PORT*/
                /*on '/' - host ends, assign start of path,pathname, end of port, next state is RL_PATH*/
                /*on ' ' - host ends, assign end of uri, start and end of port,path,pathname, next state is RL_VERSION_START*/
                /*on all other input, fails*/
                if((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '.') {
                    break;
                } else if(ch == ':') {
                    http_req->host_end = p_ch;
                    http_req->port_start = p_ch + 1;
                    parser_state = RL_PORT;
                } else if(ch == '/') {
                    http_req->host_end = p_ch;
                    http_req->port_end = p_ch;
                    http_req->path_start = p_ch;
                    http_req->pathname_start = p_ch;
                    parser_state = RL_PATH;
                } else if(ch == ' ') {
                    http_req->host_end = p_ch;
                    http_req->uri_end = p_ch;
                    http_req->port_end = p_ch;
                    http_req->path_start = p_ch;
                    http_req->path_end = p_ch;
                    http_req->pathname_start = p_ch;
                    http_req->pathname_end = p_ch;
                    parser_state = RL_VERSION_START;
                } else {
                    return E_FAIL;
                }
                break;
            case RL_HOST_IP_LITERAL:
                if ((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F') || ch == ':' || ch == '.') {
                    break;
                } else if (ch == ']') {
                    http_req->host_end = p_ch;
                    parser_state = RL_HOST_END;
                } else {
                    return E_FAIL;
                }
            break;
            case RL_HOST_END:
                if (ch == ':') {
                    http_req->port_start = p_ch + 1;
                    parser_state = RL_PORT;
                } else if (ch == '/') {
                    http_req->port_start = p_ch;
                    http_req->port_end = p_ch;
                    http_req->path_start = p_ch;
                    http_req->pathname_start = p_ch;
                    parser_state = RL_PATH;
                } else if (ch == ' ') {
                    http_req->uri_end = p_ch;
                    http_req->port_start = p_ch;
                    http_req->port_end = p_ch;
                    http_req->path_start = p_ch;
                    http_req->path_end = p_ch;
                    http_req->pathname_start = p_ch;
                    http_req->pathname_end = p_ch;
                    parser_state = RL_VERSION_START;
                } else {
                    return E_FAIL;
                }
                break;
            case RL_PORT:
                /*port can only have numbers */
                /*on '/' - port ends, assign start of path,pathname, next state is RL_PATH*/
                /*on ' ' - port ends, assign end of uri, start and end of path,pathname, next state is RL_VERSION_START*/
                /*on all other input, fails*/
                if(ch >= '0' && ch <= '9') {
                    break;
                } else if(ch == '/') {
                    if(p_ch == http_req->port_start) {
                        return E_FAIL; // port cannot be empty
                    }
                    http_req->port_end = p_ch;
                    http_req->path_start = p_ch;
                    http_req->pathname_start = p_ch;
                    parser_state = RL_PATH;
                } else if(ch == ' ') {
                    if(p_ch == http_req->port_start) {
                        return E_FAIL; // port cannot be empty
                    }
                    http_req->port_end = p_ch;
                    http_req->uri_end = p_ch;
                    http_req->path_start = p_ch;
                    http_req->path_end = p_ch;
                    http_req->pathname_start = p_ch;
                    http_req->pathname_end = p_ch;
                    parser_state = RL_VERSION_START;
                } else {
                    return E_FAIL;
                }
                break;

            case RL_PATH:
                /*on ' ' - path ends, assign end of path,pathname,uri next state is RL_VERSION_START*/
                /*on '?'or'&'or'='or'#' - assign end of path, next state is RL_PATHNAME*/
                /*on CR or LF, fails*/
                if(ch == ' ') {
                    http_req->path_end = p_ch;
                    http_req->pathname_end = p_ch;
                    http_req->uri_end = p_ch;
                    parser_state = RL_VERSION_START;
                } else if (ch == '?' || ch == '&' || ch == '=' || ch == '#') {
                    http_req->path_end = p_ch;
                    parser_state = RL_PATHNAME;
                } else if (ch == CR || ch == LF || ch < 0x21 || ch > 0x7E) {
                    return E_FAIL;
                }
                break;

            case RL_PATHNAME:
                /*on ' ' - assign end of uri,pathname, next state is RL_VERSION_START*/
                /*on CR or LF, fails*/
                if(ch == ' ') {
                    http_req->pathname_end = p_ch;
                    http_req->uri_end = p_ch;
                    parser_state = RL_VERSION_START;
                } else if (ch == CR || ch == LF || ch < 0x21 || ch > 0x7E) {
                    return E_FAIL;
                }
                break;

            case RL_VERSION_START:
                /*can have space*/
                /*on 'H' - next state is RL_VERSION_H*/
                /*fails on all other input*/
                if(ch == ' ') {
                    break;
                } else if(ch == 'H') {
                    parser_state = RL_VERSION_H;
                } else if(ch != ' ') {
                    return E_FAIL;
                }
                break;

            case RL_VERSION_H:
                /*fill this*/
                if(ch == 'T') {
                    parser_state = RL_VERSION_HT;
                } else {
                    return E_FAIL;
                }
                break;

            case RL_VERSION_HT:
                /*fill this*/
                if(ch == 'T') {
                    parser_state = RL_VERSION_HTT;
                } else {
                    return E_FAIL;
                }
                break;

            case RL_VERSION_HTT:
                /*fill this*/
                if(ch == 'P') {
                    parser_state = RL_VERSION_HTTP;
                } else {
                    return E_FAIL;
                }
                break;

            case RL_VERSION_HTTP:
                /*fill this*/
                if(ch == '/') {
                    parser_state = RL_VERSION_HTTP_SLASH;
                } else {
                    return E_FAIL;
                }
                break;

            case RL_VERSION_HTTP_SLASH:
                /*on '1' - assign major, next state is RL_VERSION_MAJOR, fails on all other inputs*/
                if(ch == '1') {
                    http_req->http_major = p_ch;
                    parser_state = RL_VERSION_MAJOR;
                } else {
                    return E_FAIL;
                }
                break;

            case RL_VERSION_MAJOR:
                /*fill ths*/
                if(ch == '.') {
                    parser_state = RL_VERSION_DOT;
                } else {
                    return E_FAIL;
                }
                break;

            case RL_VERSION_DOT:
                /*on '0' or '1' - assign minor to next position, next state is RL_VERSION_MINOR, fails on other inputs*/
                if(ch == '0' || ch == '1') {
                    http_req->http_minor = p_ch;
                    parser_state = RL_VERSION_MINOR;
                } else {
                    return E_FAIL;
                }
                break;

            case RL_VERSION_MINOR:
                if (ch == CR) {
                    http_req->request_line_end = p_ch;
                    parser_state = RL_CR;
                } else if (ch == LF) {
                    http_req->request_line_end = p_ch;
                    parser_state = RL_LF;
                } else {    
                    return E_FAIL;
                }
                break;

            case RL_CR:
                /*fill this*/
                if (ch == LF) {
                    parser_state = RL_LF;
                } else {
                    return E_FAIL;
                }
                break;

            case RL_LF:
                http_req->parser_state = H_START;
                buff->pos = p_ch;
                return OK;

            default:
                return E_FAIL;
        }
        buff->pos = p_ch + 1;
    
    }
    http_req->parser_state = parser_state;
    return E_AGAIN;
}

int xps_http_parse_header_line(xps_http_req_t *http_req, xps_buffer_t *buff) {
    assert(http_req != NULL);
    assert(buff != NULL);

    u_char *p_ch = buff->pos;
    xps_http_parser_state_t parser_state = http_req->parser_state;
    u_char *buffer_end = buff->data + buff->len;

    for (; p_ch < buffer_end; p_ch++) {
        char ch = *p_ch;

        switch (parser_state) {
            case H_START:
                if(ch == CR) {
                    http_req->header_key_start = NULL;
                    http_req->header_key_end = NULL;
                    http_req->header_val_start = NULL;
                    http_req->header_val_end = NULL;
                    parser_state = H_CR;
                } else if (ch == LF) {
                    http_req->header_key_start = NULL;
                    http_req->header_key_end = NULL;
                    http_req->header_val_start = NULL;
                    http_req->header_val_end = NULL;
                    buff->pos = p_ch + 1;
                    http_req->parser_state = H_START;
                    return OK;
                } else if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') ) {
                    http_req->header_key_start = p_ch;
                    parser_state = H_NAME;
                } else {
                    return E_FAIL;
                }
                break;

            case H_NAME:
                if(ch >= 'A' && ch <= 'Z' || ch >= 'a' && ch <= 'z' || ch >= '0' && ch <= '9' || ch == '-') {
                    break;
                } else if(ch == ':') {
                    http_req->header_key_end = p_ch;
                    parser_state = H_COLON;
                } else {
                    return E_FAIL;
                }
                break;

            case H_COLON:
                if(ch == ' ') {
                    parser_state = H_SP_AFTER_COLON;
                } else if (ch == CR || ch == LF) {
                    http_req->header_val_start = p_ch;
                    http_req->header_val_end = p_ch;
                    parser_state = (ch == CR) ? H_CR : H_LF;
                } else {
                    http_req->header_val_start = p_ch;
                    parser_state = H_VAL;
                }
                break;

            case H_SP_AFTER_COLON:
                if(ch == ' ') {
                    break;
                } else if (ch == CR || ch == LF) {
                    http_req->header_val_start = p_ch;
                    http_req->header_val_end = p_ch;
                    parser_state = (ch == CR) ? H_CR : H_LF;
                } else {
                    http_req->header_val_start = p_ch;
                    parser_state = H_VAL;
                }
                break;

            case H_VAL:
                if (ch == CR || ch == LF) {
                    http_req->header_val_end = p_ch;
                    parser_state = (ch == CR) ? H_CR : H_LF;
                } else if((unsigned char) ch < 0x20 && ch != '\t') {
                    return E_FAIL;
                } else if((unsigned char) ch > 0x7E) {
                    return E_FAIL;
                }
                break;

            case H_CR:
                if(ch == LF) {
                    if (http_req->header_key_start == NULL) {
                        buff->pos = p_ch + 1;
                        http_req->parser_state = H_START;
                        return OK;
                    }
                    parser_state = H_LF;
                } else {
                    return E_FAIL;
                }
                break;

            case H_LF:
                if (ch == LF) {
                    parser_state = H_LF_LF;
                } else if (ch == CR) {
                    parser_state = H_LF_CR;
                } else {
                    http_req->parser_state = H_START;
                    buff->pos = p_ch;   
                    return E_NEXT; // This header is done, repeat for the next
                }
                break;

            case H_LF_LF:
                buff->pos = p_ch + 1;
                http_req->parser_state = H_START;
                return OK; // HTTP complete header section done

            case H_LF_CR:
                if (ch == LF) {
                    parser_state = H_LF_CR_LF;
                } else {
                    return E_FAIL;
                }
                break;
            case H_LF_CR_LF:
                buff->pos = p_ch + 1;
                http_req->parser_state = H_START;
                return OK; // HTTP complete header section done
            default:
                return E_FAIL;
        }
        buff->pos = p_ch + 1;
    }
    if (parser_state == H_LF_LF || parser_state == H_LF_CR_LF) {
        http_req->parser_state = H_START;
        return OK;
    }
    http_req->parser_state = parser_state;
    return E_AGAIN;
}

const char *xps_http_get_header(vec_void_t *headers, const char *key) {
    /*Validate params*/
    assert(headers != NULL);
    assert(key != NULL);
    for (int i = 0; i < headers->length; i++) {
        xps_keyval_t *header = (xps_keyval_t *)headers->data[i];
        const unsigned char *header_key = (const unsigned char *)header->key;
        const unsigned char *lookup_key = (const unsigned char *)key;
        while (*header_key != '\0' && *lookup_key != '\0') {
            unsigned char header_char = *header_key;
            unsigned char lookup_char = *lookup_key;
            if (header_char >= 'A' && header_char <= 'Z')
                header_char += 'a' - 'A';
            if (lookup_char >= 'A' && lookup_char <= 'Z')
                lookup_char += 'a' - 'A';
            if (header_char != lookup_char)
                break;
            header_key++;
            lookup_key++;
        }
        if (*header_key == '\0' && *lookup_key == '\0')
            return header->val;
    }
    return NULL;
}

xps_buffer_t *xps_http_serialize_headers(vec_void_t *headers) {
    assert(headers != NULL);

    xps_buffer_t *buff = xps_buffer_create(DEFAULT_BUFFER_SIZE, 0, NULL);

    if (buff == NULL) {
        logger(LOG_ERROR, "xps_http_serialize_headers()", "xps_buffer_create() failed");
        return NULL;
    }

    for (size_t i = 0; i < headers->length; i++) {
        xps_keyval_t *header = headers->data[i];

        if (header == NULL || header->key == NULL || header->val == NULL) {
            logger(LOG_ERROR, "xps_http_serialize_headers()", "invalid header");
            xps_buffer_destroy(buff);
            return NULL;
        }

        size_t header_str_len = snprintf(NULL, 0, "%s: %s\n", header->key, header->val);

        size_t required_size = buff->len + header_str_len + 1;

        if (buff->size < required_size) {
            size_t new_size = buff->size;

            while (new_size < required_size) {
                if (new_size > SIZE_MAX / 2) {
                    xps_buffer_destroy(buff);
                    return NULL;
                }
                new_size *= 2;
            }

            u_char *new_data = realloc(buff->data, new_size);

            if (new_data == NULL) {
                logger(LOG_ERROR, "xps_http_serialize_headers()", "realloc() failed");
                xps_buffer_destroy(buff);
                return NULL;
            }

            buff->data = new_data;
            buff->size = new_size;
        }

        snprintf((char *)buff->data + buff->len, buff->size - buff->len, "%s: %s\n", header->key, header->val);

        buff->len += header_str_len;
    }

    return buff;
}