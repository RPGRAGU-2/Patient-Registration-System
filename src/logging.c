#include "prs/logging.h"

#include <stdio.h>
#include <sys/stat.h>
#include <time.h>

#include "prs/app_config.h"

static FILE *log_file = NULL;

static const char *level_name(PrsLogLevel level) {
    switch (level) {
        case PRS_LOG_DEBUG:
            return "DEBUG";
        case PRS_LOG_INFO:
            return "INFO";
        case PRS_LOG_WARNING:
            return "WARN";
        case PRS_LOG_ERROR:
            return "ERROR";
        default:
            return "UNKNOWN";
    }
}

bool prs_logger_open(const char *path) {
    char error_message[160];

    if (!prs_ensure_parent_directory(path, error_message, sizeof(error_message))) {
        (void)fprintf(stderr, "Logging unavailable: %s\n", error_message);
        return false;
    }

    log_file = fopen(path, "a");
    if (log_file == NULL) {
        (void)fprintf(stderr, "Logging unavailable: unable to open log file.\n");
        return false;
    }
    if (chmod(path, S_IRUSR | S_IWUSR) != 0) {
        (void)fclose(log_file);
        log_file = NULL;
        (void)fprintf(stderr, "Logging unavailable: unable to protect log file.\n");
        return false;
    }
    return true;
}

void prs_logger_close(void) {
    if (log_file != NULL) {
        (void)fclose(log_file);
        log_file = NULL;
    }
}

void prs_log(PrsLogLevel level, const char *module, const char *message) {
    time_t now;
    struct tm *local_time = NULL;
    char timestamp[32];
    FILE *destination = log_file != NULL ? log_file : stderr;

    now = time(NULL);
    local_time = localtime(&now);
    if (local_time != NULL) {
        (void)strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S",
                       local_time);
    } else {
        (void)snprintf(timestamp, sizeof(timestamp), "time-unavailable");
    }

    (void)fprintf(destination, "%s %-5s [%s] %s\n", timestamp,
                  level_name(level), module != NULL ? module : "app",
                  message != NULL ? message : "");
    (void)fflush(destination);
}
