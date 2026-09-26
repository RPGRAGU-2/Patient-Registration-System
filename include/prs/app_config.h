#ifndef PRS_APP_CONFIG_H
#define PRS_APP_CONFIG_H

#include <stdbool.h>
#include <stddef.h>

#define PRS_PATH_CAPACITY 512U

typedef struct {
    const char *database_path;
    const char *log_path;
    unsigned int idle_lock_minutes;
} PrsAppConfig;

extern const PrsAppConfig PRS_DEFAULT_CONFIG;

bool prs_ensure_parent_directory(const char *path, char *error_message,
                                 size_t error_message_size);

#endif
