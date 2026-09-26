#include "prs/app_config.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

const PrsAppConfig PRS_DEFAULT_CONFIG = {
    .database_path = "data/prs.db",
    .log_path = "logs/prs.log",
    .idle_lock_minutes = 15U,
};

static void set_error(char *error_message, size_t error_message_size,
                      const char *message) {
    if (error_message != NULL && error_message_size > 0U) {
        (void)snprintf(error_message, error_message_size, "%s", message);
    }
}

bool prs_ensure_parent_directory(const char *path, char *error_message,
                                 size_t error_message_size) {
    char directory[PRS_PATH_CAPACITY];
    char *cursor = NULL;
    size_t length = 0U;

    if (path == NULL) {
        set_error(error_message, error_message_size, "Path is required.");
        return false;
    }

    length = strlen(path);
    if (length == 0U || length >= sizeof(directory)) {
        set_error(error_message, error_message_size,
                  "Path is empty or exceeds the supported length.");
        return false;
    }

    (void)snprintf(directory, sizeof(directory), "%s", path);
    cursor = strrchr(directory, '/');
    if (cursor == NULL) {
        return true;
    }
    *cursor = '\0';
    if (directory[0] == '\0') {
        return true;
    }

    for (cursor = directory + 1; *cursor != '\0'; ++cursor) {
        if (*cursor == '/') {
            *cursor = '\0';
            if (mkdir(directory, 0700) != 0 && errno != EEXIST) {
                set_error(error_message, error_message_size,
                          "Unable to create an application directory.");
                return false;
            }
            *cursor = '/';
        }
    }

    if (mkdir(directory, 0700) != 0 && errno != EEXIST) {
        set_error(error_message, error_message_size,
                  "Unable to create an application directory.");
        return false;
    }
    return true;
}
