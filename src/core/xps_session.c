#include "xps_session.h"

xps_session_t *xps_session_create(xps_core_t *core, xps_connection_t *client) {
    /* validate parameters */
    assert(core != NULL);
    assert(client != NULL);
    // Alloc memory for session instance
    xps_session_t *session = (xps_session_t*) malloc(sizeof(xps_session_t));
    if (session == NULL) {
        logger(LOG_ERROR, "xps_session_create()", "malloc() failed for 'session'");
        return NULL;
    }

    session->client_source = xps_pipe_source_create(session, client_source_handler, client_source_close_handler);
    session->client_sink = xps_pipe_sink_create(session, client_sink_handler, client_sink_close_handler);
    session->upstream_source = xps_pipe_source_create(session, upstream_source_handler, upstream_source_close_handler);
    session->upstream_sink = xps_pipe_sink_create(session, upstream_sink_handler, upstream_sink_close_handler);
    session->file_sink = xps_pipe_sink_create(session, file_sink_handler, file_sink_close_handler);

    if (!(session->client_source && session->client_sink && session->upstream_source && session->upstream_sink && session->file_sink)) {
        logger(LOG_ERROR, "xps_session_create()", "failed to create some sources/sinks");

        if (session->client_source)
        xps_pipe_source_destroy(session->client_source);
        if (session->client_sink)
            xps_pipe_sink_destroy(session->client_sink);
        if (session->upstream_source)
            xps_pipe_source_destroy(session->upstream_source);
        if (session->upstream_sink)
            xps_pipe_sink_destroy(session->upstream_sink);
        if (session->file_sink)
            xps_pipe_sink_destroy(session->file_sink);

        free(session);
        return NULL;
    }

    // Init values
    session->core = core;
    session->client = client;
    session->upstream = NULL;
    session->upstream_connected = false;
    session->upstream_error_res_set = false;
    session->upstream_write_bytes = 0;
    session->file = NULL;
    session->to_client_buff = NULL;
    session->from_client_buff = NULL;
    session->client_sink->ready = true;
    session->upstream_sink->ready = true;
    session->file_sink->ready = true;
    session->http_req = NULL;

    /*NOTE: We will be adding list of sessions as vec_void_t sessions to xps_core_s in xps_core module below*/
    // Add current session to core->sessions
    vec_push(&core->sessions, session);

    // Attach client
    if (xps_pipe_create(core, DEFAULT_PIPE_BUFF_THRESH, client->source, session->client_sink) == NULL ||
        xps_pipe_create(core, DEFAULT_PIPE_BUFF_THRESH, session->client_source, client->sink) == NULL) {

        logger(LOG_ERROR, "xps_session_create()", "failed to create client pipes");

        if (session->client_source)
            xps_pipe_source_destroy(session->client_source);
        if (session->client_sink)
            xps_pipe_sink_destroy(session->client_sink);
        if (session->upstream_source)
            xps_pipe_source_destroy(session->upstream_source);  
        if (session->upstream_sink)
            xps_pipe_sink_destroy(session->upstream_sink);
        if (session->file_sink)
            xps_pipe_sink_destroy(session->file_sink);

        xps_session_destroy(session);
        return NULL;
    }

    logger(LOG_DEBUG, "xps_session_create()", "created session");
/*
    if (client->listener->port == 8001) {
        xps_connection_t *upstream = xps_upstream_create(core, "127.0.0.1", 3000);
        if (upstream == NULL) {
            logger(LOG_ERROR, "xps_session_create()", "xps_upstream_create() failed");
            perror("Error message");
            xps_session_destroy(session);
            return NULL;
        }
        session->upstream = upstream;
        if (xps_pipe_create(core, DEFAULT_PIPE_BUFF_THRESH, upstream->source, session->upstream_sink) == NULL ||
            xps_pipe_create(core, DEFAULT_PIPE_BUFF_THRESH, session->upstream_source, upstream->sink) == NULL) {
            logger(LOG_ERROR, "xps_session_create()", "failed to create upstream pipes");
            xps_session_destroy(session);
            return NULL;
        }
    }
*/ /*
    else if (client->listener->port == 8002) {
        int error;
        xps_file_t *file = xps_file_create(session->core, "public/sample.txt", &error);
        if (file == NULL) {
            logger(LOG_ERROR, "xps_session_create()", "xps_file_create() failed");
            xps_session_destroy(session);
            return NULL;
        }
        session->file = file;
        if (xps_pipe_create(core, DEFAULT_PIPE_BUFF_THRESH, session->file->source, session->file_sink) == NULL) {
            logger(LOG_ERROR, "xps_session_create()", "failed to create file pipe");
            xps_session_destroy(session);
            return NULL;
        }
    } */

    return session;
}

void client_source_handler(void *ptr) {
    /* validate parameters */
    assert(ptr != NULL);  

    xps_pipe_source_t *source = ptr;
    xps_session_t *session = source->ptr;

    // write to session->to_client_buff
    if (xps_pipe_source_write(source, session->to_client_buff) != OK) {
        logger(LOG_ERROR, "client_source_handler()", "xps_pipe_source_write() failed");
        return;
    }
    //xps_buffer_destroy(session->to_client_buff);

    set_to_client_buff(session, NULL);
    session_check_destroy(session);
}

void client_source_close_handler(void *ptr) {
    assert(ptr != NULL);

    xps_pipe_source_t *source = ptr;
    xps_session_t *session = source->ptr;

    session_check_destroy(session);
}

void client_sink_handler(void *ptr) {
    assert(ptr != NULL);

    xps_pipe_sink_t *sink = ptr;
    xps_session_t *session = sink->ptr;

    xps_buffer_t *buff = xps_pipe_sink_read(sink, sink->pipe->buff_list->len);
    if (buff == NULL) {
        logger(LOG_ERROR, "client_sink_handler()", "xps_pipe_sink_read() failed");
        return;
    }
    size_t len = buff->len;

    if (session->http_req == NULL) { // http request not received till now
        int error;
        xps_http_req_t *http_req = xps_http_req_create(session->core, buff, &error);
        xps_buffer_destroy(buff);

        if (error == E_AGAIN) {
            // incomplete head: leave data in the pipe and wait for more
            return;
        }

        xps_pipe_sink_clear(sink, len);

        if (error != OK) { // E_FAIL -> http_req is NULL, process_request sends 400
            session_process_request(session);
            return;
        }

        session->http_req = http_req;

        // serialize http_req into buffer http_req_buff
        xps_buffer_t *http_req_buff = xps_http_req_serialize(http_req);
        if (http_req_buff == NULL) {
            logger(LOG_ERROR, "client_sink_handler()", "xps_http_req_serialize() failed");
            return;
        }

        // set http_req_buff to from_client_buff
        set_from_client_buff(session, http_req_buff);

        // process the session
        session_process_request(session);
    }
    else {
        set_from_client_buff(session, buff);
        xps_pipe_sink_clear(sink, len);
    }
}

void client_sink_close_handler(void *ptr) {

    assert(ptr != NULL);

    xps_pipe_sink_t *sink = ptr;
    xps_session_t *session = sink->ptr;
    assert(session != NULL);

    session_check_destroy(session);
}

void upstream_source_handler(void *ptr) {
    /*assert*/
    assert(ptr != NULL);
        
    /*set ptr as source*/
    /*set session as source->ptr*/
    xps_pipe_source_t *source = ptr;
    xps_session_t *session = source->ptr;
    assert(session != NULL);

    if (xps_pipe_source_write(source, session->from_client_buff) != OK) {
        logger(LOG_ERROR, "upstream_source_handler()", "xps_pipe_source_write() failed");
        return;
    }

    // Checking if upstream is connected
    if (session->upstream_connected == false) {
        session->upstream_write_bytes += session->from_client_buff->len;
        if (session->upstream_write_bytes > session->upstream_source->pipe->buff_list->len)
            session->upstream_connected = true;
    }

    xps_buffer_destroy(session->from_client_buff);

    set_from_client_buff(session, NULL);
    session_check_destroy(session);
}

void upstream_source_close_handler(void *ptr) {

    assert(ptr != NULL);
    xps_pipe_source_t *source = ptr;
    xps_session_t *session = source->ptr;
    assert(session != NULL);

    if (!session->upstream_connected && !session->upstream_error_res_set) {
        upstream_error_res(session);
    }

    session_check_destroy(session);
}

void upstream_sink_handler(void *ptr) {
    
    assert(ptr != NULL);

    xps_pipe_sink_t *sink = ptr;
    xps_session_t *session = sink->ptr;

    assert(session != NULL);

    session->upstream_connected = true;

    xps_buffer_t *buff = xps_pipe_sink_read(sink, sink->pipe->buff_list->len);
    if (buff == NULL) {
        logger(LOG_ERROR, "upstream_sink_handler()", "xps_pipe_sink_read() failed");
        return;
    }

    set_to_client_buff(session, buff);
    xps_pipe_sink_clear(sink, buff->len);
}

void upstream_sink_close_handler(void *ptr) {
    assert(ptr != NULL);
    xps_pipe_sink_t *sink = ptr;
    xps_session_t *session = sink->ptr;
    assert(session != NULL);

    if (!session->upstream_connected && !session->upstream_error_res_set) {
        upstream_error_res(session);
    }

    session_check_destroy(session);
}

void upstream_error_res(xps_session_t *session) {
    assert(session != NULL);

    session->upstream_error_res_set = true;
}

void file_sink_handler(void *ptr) {
    assert(ptr != NULL);
    xps_pipe_sink_t *sink = ptr;
    xps_session_t *session = sink->ptr;
    assert(session != NULL);

    xps_buffer_t *buff = xps_pipe_sink_read(sink, session->file_sink->pipe->buff_list->len);
    if (buff == NULL) {
        logger(LOG_ERROR, "file_sink_handler()", "xps_pipe_sink_read() failed");
        return;
    }

    set_to_client_buff(session, buff);
    xps_pipe_sink_clear(sink, buff->len);
}

void file_sink_close_handler(void *ptr) {

    assert(ptr != NULL);
    xps_pipe_sink_t *sink = ptr;
    xps_session_t *session = sink->ptr;
    assert(session != NULL);

    session_check_destroy(session);

}

void set_to_client_buff(xps_session_t *session, xps_buffer_t *buff) {
    /* validate parameters */
    assert(session != NULL);
    session->to_client_buff = buff;

    if (buff == NULL) {
        session->client_source->ready = false;
        session->upstream_sink->ready = true;
        session->file_sink->ready = true;
    } else {
        session->client_source->ready = true;
        session->upstream_sink->ready = false;
        session->file_sink->ready = false;
    }
}

void set_from_client_buff(xps_session_t *session, xps_buffer_t *buff) {
    /* validate parameters */
    assert(session != NULL);
    session->from_client_buff = buff;

    if (buff == NULL) {
        session->client_sink->ready = true;
        session->upstream_source->ready = false;
    } else {
        session->client_sink->ready = false;
        session->upstream_source->ready = true;
    }
}

void session_check_destroy(xps_session_t *session) {
    /* validate parameters */
    assert(session != NULL);
    bool c2u_flow = session->upstream_source->active && (session->client_sink->active || session->from_client_buff);

    bool u2c_flow = session->client_source->active && (session->upstream_sink->active || session->to_client_buff);

    bool f2c_flow = session->file_sink->active && (session->client_source->active || session->to_client_buff);

    bool flowing = c2u_flow || u2c_flow || f2c_flow;

    if (!flowing)
        xps_session_destroy(session);
}

void xps_session_destroy(xps_session_t *session) {
    /* validate parameters */
    assert(session != NULL);
    /* destroy client_source, client_sink, upstream_source, upstream_sink and file_sink attached to session */
    if(session->client_source != NULL)
        xps_pipe_source_destroy(session->client_source);
    if(session->client_sink != NULL)
        xps_pipe_sink_destroy(session->client_sink);
    if(session->upstream_source != NULL)
        xps_pipe_source_destroy(session->upstream_source);
    if(session->upstream_sink != NULL)
        xps_pipe_sink_destroy(session->upstream_sink);
    if(session->file_sink != NULL)
        xps_pipe_sink_destroy(session->file_sink);
    if (session->http_req != NULL)
        xps_http_req_destroy(session->core, session->http_req);

    if (session->to_client_buff != NULL)
        xps_buffer_destroy(session->to_client_buff);
    if (session->from_client_buff != NULL)
        xps_buffer_destroy(session->from_client_buff);

    // Set NULL in core's list of sessions
    for (int i = 0; i < session->core->sessions.length; i++) {
        if (session->core->sessions.data[i] == session) {
            session->core->sessions.data[i] = NULL;
            session->core->n_null_sessions++;
            break;
        }
    }

    free(session);

    logger(LOG_DEBUG, "xps_session_destroy()", "destroyed session");
}

static bool session_set_response(xps_session_t *session, const char *response) {
    size_t response_len = strlen(response);
    xps_buffer_t *buff = xps_buffer_create(response_len, response_len, NULL);
    if (buff == NULL) {
        logger(LOG_ERROR, "session_process_request()", "failed to allocate response buffer");
        return false;
    }

    memcpy(buff->data, response, response_len);
    set_to_client_buff(session, buff);
    return true;
}

void session_process_request(xps_session_t *session) {
    assert(session != NULL);

    char reply[1024];
    xps_http_req_t *http_req = session->http_req;

    if (http_req == NULL) {
        session_set_response(session, "HTTP/1.1 400 Bad Request\r\nServer: eXpServer\r\nContent-Length: 0\r\n\r\n");
        return;
    }

    if (http_req->path == NULL) {
        session_set_response(session, "HTTP/1.1 400 Bad Request\r\nServer: eXpServer\r\nContent-Length: 0\r\n\r\n");
        return;
    }

    char file_path[DEFAULT_BUFFER_SIZE];
    int path_len;
    if (strncmp(http_req->path, "/public/", sizeof("/public/") - 1) == 0) {
        path_len = snprintf(file_path, sizeof(file_path), "%s", http_req->path + 1);
    } else {
        path_len = snprintf(file_path, sizeof(file_path), "public%s", http_req->path);
    }
    if (path_len < 0 || (size_t)path_len >= sizeof(file_path)) {
        session_set_response(session, "HTTP/1.1 400 Bad Request\r\nServer: eXpServer\r\nContent-Length: 0\r\n\r\n");
        return;
    }

    int error;
    xps_file_t *file = xps_file_create(session->core, file_path, &error);
    logger(LOG_DEBUG, "session_process_request()", "file_path: %s", file_path);

    if (file == NULL) {
        if (error == E_PERMISSION)
            snprintf(reply, sizeof(reply), "HTTP/1.1 403 Forbidden\r\nServer: eXpServer\r\nContent-Length: 0\r\n\r\n");
        else if (error == E_NOTFOUND)
            snprintf(reply, sizeof(reply), "HTTP/1.1 404 Not Found\r\nServer: eXpServer\r\nContent-Length: 0\r\n\r\n");
        else
            snprintf(reply, sizeof(reply), "HTTP/1.1 500 Internal Server Error\r\nServer: eXpServer\r\nContent-Length: 0\r\n\r\n");
        session_set_response(session, reply);
        return;
    }

    session->file = file;
    if (xps_pipe_create(session->core, DEFAULT_PIPE_BUFF_THRESH, file->source, session->file_sink) == NULL) {
        logger(LOG_ERROR, "session_process_request()", "xps_pipe_create() failed");
        xps_pipe_source_destroy(file->source);
        file->source = NULL;
        xps_file_destroy(file);
        session->file = NULL;
        session_set_response(session, "HTTP/1.1 500 Internal Server Error\r\nServer: eXpServer\r\nContent-Length: 0\r\n\r\n");
        return;
    }

    snprintf(reply, sizeof(reply),
             "HTTP/1.1 200 OK\r\nServer: eXpServer\r\n"
             "Access-Control-Allow-Origin: *\r\nContent-Length: %zu\r\n"
             "Content-Type: %s\r\n\r\n",
             file->size, file->mime_type ? file->mime_type : "application/octet-stream");
    session_set_response(session, reply);
}