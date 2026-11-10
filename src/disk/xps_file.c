#include "xps_file.h"

xps_file_t *xps_file_create(xps_core_t *core, const char *file_path, int *error) {
    /*assert*/
    assert(core);
    assert(file_path);
    assert(error);
    *error = E_FAIL;
    /*check if file is inside the public directory*/
    char *resolved_path = realpath(file_path, NULL);
    char *resolved_public = realpath("public", NULL);

    if (resolved_public == NULL) {
        resolved_public = realpath("../public", NULL);
    }

    if (resolved_path == NULL && resolved_public != NULL &&
        strncmp(file_path, "public/", sizeof("public/") - 1) == 0) {
        size_t public_len = strlen(resolved_public);
        size_t file_name_len = strlen(file_path + sizeof("public/") - 1);
        char *public_file_path = malloc(public_len + file_name_len + 2);
        if (public_file_path == NULL) {
            logger(LOG_ERROR, "xps_file_create()", "malloc() failed");
            free(resolved_public);
            return NULL;
        }

        snprintf(public_file_path, public_len + file_name_len + 2, "%s/%s",
                 resolved_public, file_path + sizeof("public/") - 1);
        resolved_path = realpath(public_file_path, NULL);
        free(public_file_path);
    }

    if (resolved_path == NULL || resolved_public == NULL) {
        logger(LOG_ERROR, "xps_file_create()", "realpath() failed");
        if (resolved_path == NULL && (errno == ENOENT || errno == ENOTDIR))
            *error = E_NOTFOUND;
        free(resolved_path);
        free(resolved_public);
        return NULL;
    }

    size_t public_len = strlen(resolved_public);

    if (strncmp(resolved_path, resolved_public, public_len) != 0 ||
        (resolved_path[public_len] != '\0' && resolved_path[public_len] != '/')) {
        logger(LOG_WARNING, "xps_file_create()", "file requested is outside of public directory");
        *error = E_PERMISSION;
        free(resolved_path);
        free(resolved_public);
        return NULL;
    }

    /*free public path*/
    free(resolved_public);

    /*check if others have read permission*/   
    struct stat file_stat;
    if (stat(resolved_path, &file_stat) != 0) {
        logger(LOG_ERROR, "xps_file_create()", "stat() failed");
        perror("Error message");
        free(resolved_path);
        return NULL;
    }

    if (!S_ISREG(file_stat.st_mode)) {
        logger(LOG_WARNING, "xps_file_create()", "requested path is not a regular file");
        *error = E_PERMISSION;
        free(resolved_path);
        return NULL;
    }

    if (!(file_stat.st_mode & S_IROTH)) {
        logger(LOG_WARNING, "xps_file_create()", "others do not have read permission");
        *error = E_PERMISSION;
        free(resolved_path);
        return NULL;
    }

    // Getting size of file from stat (already called above)
    long temp_size = file_stat.st_size;

    // Opening file
    FILE *file_struct = fopen(resolved_path, "rb");
    /*handle EACCES,ENOENT or any other error*/
    if (file_struct == NULL) {
        logger(LOG_ERROR, "xps_file_create()", "fopen() failed: %s", strerror(errno));
        free(resolved_path);
        return NULL;
    }

    const char *mime_type = xps_get_mime(resolved_path);

    /*Alloc memory for instance of xps_file_t*/
    xps_file_t *file = (xps_file_t*) malloc(sizeof(xps_file_t));
    if (file == NULL) {
        logger(LOG_ERROR, "xps_file_create()", "malloc() failed");
        fclose(file_struct);
        free(resolved_path);
        return NULL;
    }

    /*initialise the fields of file instance*/
    file->core = core;
    file->file_path = resolved_path;
    file->file_struct = file_struct;
    file->size = temp_size;
    file->mime_type = mime_type;

    xps_pipe_source_t *source = xps_pipe_source_create((void *)file, file_source_handler, file_source_close_handler);
    /*if source is null, close file_struct and return*/
    if(source == NULL) {
        logger(LOG_ERROR, "xps_file_create()", "xps_pipe_source_create() failed");
        fclose(file_struct);
        free((void *)file->file_path);
        free(file);
        return NULL;
    }
    source->ready = true;
    file->source = source;

    *error = OK;

    logger(LOG_DEBUG, "xps_file_create()", "created file");

    return file;
}

void xps_file_destroy(xps_file_t *file) {
    // Validate parameter
    assert(file != NULL);

    // Close file
    if(file->file_struct != NULL) {
        fclose(file->file_struct);
        file->file_struct = NULL;
    }

    // Free file path
    if(file->file_path != NULL) {
        free((void *)file->file_path);
        file->file_path = NULL;
    }

    // Free file instance
    free(file);

    logger(LOG_DEBUG, "xps_file_destroy()", "destroyed file struct");
}

void file_source_handler(void *ptr) {
    /*assert*/
    assert(ptr != NULL);
    xps_pipe_source_t *source = ptr;
    /*get file from source ptr*/
    xps_file_t* file = source->ptr;
    assert(file != NULL);

    /*create buffer and handle any error*/
    xps_buffer_t *buff = xps_buffer_create(DEFAULT_BUFFER_SIZE, 0, NULL);
    if(buff == NULL) {
        logger(LOG_ERROR, "file_source_handler()", "xps_buffer_create() failed");
        source->ready = false;
        return;
    }

    // Read from file
    size_t read_n = fread(buff->data, 1, buff->size, file->file_struct);
    buff->len = read_n;

    // Checking for read errors
    if (ferror(file->file_struct)) {
        /*deallocate buff, file_struct and return*/
        logger(LOG_ERROR, "file_source_handler()", "fread() failed");
        xps_buffer_destroy(buff);
        file_source_close_handler(source);
        return;
    }

    // If end of file reached
    if (read_n == 0 && feof(file->file_struct)) {
        /*deallocate buff, file_struct and return*/
        logger(LOG_DEBUG, "file_source_handler()", "reached end of file");
        xps_buffer_destroy(buff);
        file_source_close_handler(source);
        return;
    }

    /*Write to pipe form buff*/
    if(xps_pipe_source_write(source, buff) == E_FAIL) {
        logger(LOG_ERROR, "file_source_handler()", "xps_pipe_source_write() failed");
        xps_buffer_destroy(buff);
        file_source_close_handler(source);
        return;
    }
    /*deallocate buff*/
    xps_buffer_destroy(buff);
}

void file_source_close_handler(void *ptr) {
    /*assert*/
    assert(ptr);
    xps_pipe_source_t *source = ptr;

    /*get file from source ptr*/
    xps_file_t* file = source->ptr;
    assert(file != NULL);

    /*deallocate file object*/
    xps_file_destroy(file);
    source->ptr = NULL;
    xps_pipe_source_destroy(source);
    logger(LOG_DEBUG, "file_source_close_handler()", "closed file source");
}