#include "xps_core.h"

xps_core_t *xps_core_create() {

    xps_core_t *core = (xps_core_t *)malloc(sizeof(xps_core_t));
    if(core == NULL) {
        logger(LOG_ERROR, "xps_core_create()", "malloc() failed for 'core'");
        return NULL;
    }    

    xps_loop_t *loop = xps_loop_create(core);
    if(loop == NULL) {
        logger(LOG_ERROR, "xps_core_create()", "loop creation failed");
        return NULL;
    }

    // Init values
    core->loop = loop;
    vec_init(&(core->listeners));
    vec_init(&(core->connections));
    vec_init(&(core->pipes));
    vec_init(&(core->sessions));
    core->n_null_sessions = 0;
    core->n_null_pipes = 0;
    core->n_null_listeners = 0;
    core->n_null_connections = 0;

    logger(LOG_DEBUG, "xps_core_create()", "created core");

    return core;
}

void xps_core_destroy(xps_core_t *core) {
    assert(core != NULL);

    // Destroy connections
    for (int i = 0; i < core->connections.length; i++) {
        xps_connection_t *connection = core->connections.data[i];

        if (connection != NULL)
            xps_connection_destroy(connection);
    }
    vec_deinit(&core->connections);

    // Destroy listeners
    for (int i = 0; i < core->listeners.length; i++) {
        xps_listener_t *listener = core->listeners.data[i];

        if (listener != NULL)
            xps_listener_destroy(listener);
    }
    vec_deinit(&core->listeners);

    // Destroy pipes
    for (int i = 0; i < core->pipes.length; i++) {
        xps_pipe_t *pipe = core->pipes.data[i];

        if (pipe == NULL)
            continue;

        // Connections should have detached their endpoints.
        if (pipe->source != NULL || pipe->sink != NULL) {
            logger(LOG_ERROR, "xps_core_destroy()", "pipe still has attached endpoints");
        }
        xps_pipe_destroy(pipe);
    }
    vec_deinit(&core->pipes);

    for (int i = 0; i < core->sessions.length; i++) {
        xps_session_t *session = core->sessions.data[i];

        if (session != NULL)
            xps_session_destroy(session);
    }
    vec_deinit(&core->sessions);

    // Destroy loop
    xps_loop_destroy(core->loop);

    // Free core
    free(core);

    logger(LOG_DEBUG, "xps_core_destroy()", "destroyed core");
}

void xps_core_start(xps_core_t *core) {

    /* validate params */
    assert(core != NULL);

    logger(LOG_DEBUG, "xps_start()", "starting core");

    /* create listeners from port 8001 to 8004 */
    for(int port = 8001; port <= 8004; port++) {
        xps_listener_t* listener = xps_listener_create(core, "0.0.0.0", port);

        if(listener == NULL) {
            logger(LOG_ERROR, "xps_core_start()", "listener creation failed");
            return;
        }
        char msg[128];
        snprintf(msg, sizeof(msg), "Server listening on port %d",port);
        logger(LOG_INFO, "xps_listener_create()", msg);
    }

    /* run loop instance using xps_loop_run() */
    xps_loop_run(core->loop);

}