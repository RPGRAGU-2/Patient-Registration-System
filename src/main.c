#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "prs/app_config.h"
#include "prs/auth.h"
#include "prs/database.h"
#include "prs/logging.h"
#include "prs/ui.h"

static void secure_clear(void *buffer, size_t length) {
    volatile unsigned char *cursor = buffer;

    while (cursor != NULL && length > 0U) {
        *cursor++ = 0U;
        --length;
    }
}

static bool read_password(const char *prompt, char *password, size_t password_size) {
    struct termios original_settings;
    struct termios hidden_settings;
    bool success = false;

    if (!isatty(STDIN_FILENO) || password == NULL || password_size < 2U ||
        tcgetattr(STDIN_FILENO, &original_settings) != 0) {
        return false;
    }
    hidden_settings = original_settings;
    hidden_settings.c_lflag &= (tcflag_t)~ECHO;
    (void)fputs(prompt, stdout);
    (void)fflush(stdout);
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &hidden_settings) == 0 &&
        fgets(password, (int)password_size, stdin) != NULL) {
        size_t input_length = strnlen(password, password_size);

        if (input_length > 0U && password[input_length - 1U] != '\n') {
            int ignored = 0;
            while ((ignored = getchar()) != '\n' && ignored != EOF) {
            }
            goto cleanup;
        }
        password[strcspn(password, "\n")] = '\0';
        success = password[0] != '\0';
    }

cleanup:
    (void)tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_settings);
    (void)fputc('\n', stdout);
    return success;
}

static int bootstrap_administrator(PrsDatabase *database, const char *username) {
    char password[257] = {0};
    char confirmation[257] = {0};
    char error_message[256] = {0};
    int exit_code = EXIT_FAILURE;

    if (!read_password("Initial administrator password: ", password,
                       sizeof(password)) ||
        !read_password("Confirm administrator password: ", confirmation,
                       sizeof(confirmation))) {
        (void)fprintf(stderr, "Unable to read passwords from a terminal.\n");
        goto cleanup;
    }
    if (strcmp(password, confirmation) != 0) {
        (void)fprintf(stderr, "Passwords do not match.\n");
        goto cleanup;
    }
    if (!prs_auth_bootstrap_administrator(database, username, password, error_message,
                                          sizeof(error_message))) {
        (void)fprintf(stderr, "Administrator setup failed: %s\n", error_message);
        goto cleanup;
    }
    (void)puts("Administrator account created. Start the application and sign in.");
    exit_code = EXIT_SUCCESS;

cleanup:
    secure_clear(password, sizeof(password));
    secure_clear(confirmation, sizeof(confirmation));
    return exit_code;
}

int main(int argc, char **argv) {
    PrsDatabase database = {0};
    char error_message[256] = {0};
    int exit_code = EXIT_FAILURE;

    (void)prs_logger_open(PRS_DEFAULT_CONFIG.log_path);
    prs_log(PRS_LOG_INFO, "main", "Application startup requested.");

    if (!prs_database_open(&database, PRS_DEFAULT_CONFIG.database_path,
                           error_message, sizeof(error_message))) {
        (void)fprintf(stderr, "Unable to start Patient Registration System: %s\n",
                      error_message);
        prs_log(PRS_LOG_ERROR, "main", "Startup failed while opening database.");
        prs_logger_close();
        return EXIT_FAILURE;
    }

    if (argc == 3 && strcmp(argv[1], "--bootstrap-admin") == 0) {
        exit_code = bootstrap_administrator(&database, argv[2]);
    } else if (argc > 1) {
        (void)fprintf(stderr, "Usage: %s [--bootstrap-admin USERNAME]\n", argv[0]);
        exit_code = EXIT_FAILURE;
    } else {
        exit_code = prs_ui_run(argc, argv, &database);
    }
    prs_database_close(&database);
    prs_log(PRS_LOG_INFO, "main", "Application shutdown completed.");
    prs_logger_close();
    return exit_code;
}
