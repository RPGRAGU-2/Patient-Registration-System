#ifndef PRS_LOGGING_H
#define PRS_LOGGING_H

#include <stdbool.h>

typedef enum {
    PRS_LOG_DEBUG = 0,
    PRS_LOG_INFO,
    PRS_LOG_WARNING,
    PRS_LOG_ERROR
} PrsLogLevel;

bool prs_logger_open(const char *path);
void prs_logger_close(void);
void prs_log(PrsLogLevel level, const char *module, const char *message);

#endif
