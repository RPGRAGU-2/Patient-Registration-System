#define _DEFAULT_SOURCE

#include "prs/auth.h"

#include <crypt.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "prs/logging.h"

static void set_error(char *error_message, size_t error_message_size,
                      const char *message) {
    if (error_message != NULL && error_message_size > 0U) {
        (void)snprintf(error_message, error_message_size, "%s", message);
    }
}

static void secure_clear(void *buffer, size_t length) {
    volatile unsigned char *cursor = buffer;

    while (cursor != NULL && length > 0U) {
        *cursor++ = 0U;
        --length;
    }
}

static bool valid_username(const char *username) {
    size_t index = 0U;
    size_t length = 0U;

    if (username == NULL) {
        return false;
    }
    length = strlen(username);
    if (length < 3U || length >= PRS_USERNAME_CAPACITY) {
        return false;
    }
    for (index = 0U; index < length; ++index) {
        const unsigned char character = (unsigned char)username[index];

        if (!(isalnum(character) || character == '.' || character == '_' ||
              character == '-')) {
            return false;
        }
    }
    return true;
}

static bool valid_password(const char *password) {
    size_t length = 0U;
    size_t index = 0U;
    bool has_lowercase = false;
    bool has_uppercase = false;
    bool has_digit = false;
    bool has_symbol = false;

    if (password == NULL) {
        return false;
    }
    length = strlen(password);
    if (length < 12U || length > 256U) {
        return false;
    }
    for (index = 0U; index < length; ++index) {
        const unsigned char character = (unsigned char)password[index];

        if (islower(character)) {
            has_lowercase = true;
        } else if (isupper(character)) {
            has_uppercase = true;
        } else if (isdigit(character)) {
            has_digit = true;
        } else if (character != ' ') {
            has_symbol = true;
        }
    }
    return (has_lowercase && has_uppercase) ||
           (has_lowercase && has_digit) ||
           (has_uppercase && has_digit) ||
           (has_symbol && (has_lowercase || has_uppercase || has_digit));
}

static char *password_hash(const char *password) {
    char *setting = NULL;
    char *hashed = NULL;
    char *result = NULL;
    void *work_buffer = NULL;
    int work_buffer_size = 0;
    size_t result_length = 0U;

    setting = crypt_gensalt_ra("$y$", 0UL, NULL, 0);
    if (setting == NULL) {
        return NULL;
    }
    hashed = crypt_ra(password, setting, &work_buffer, &work_buffer_size);
    if (hashed != NULL) {
        result_length = strlen(hashed);
        result = malloc(result_length + 1U);
        if (result != NULL) {
            (void)memcpy(result, hashed, result_length + 1U);
        }
    }
    secure_clear(setting, strlen(setting));
    free(setting);
    if (work_buffer != NULL) {
        secure_clear(work_buffer, (size_t)work_buffer_size);
        free(work_buffer);
    }
    return result;
}

static bool constant_time_equal(const char *left, const char *right) {
    size_t left_length = left != NULL ? strlen(left) : 0U;
    size_t right_length = right != NULL ? strlen(right) : 0U;
    size_t index = 0U;
    size_t difference = left_length ^ right_length;

    if (left == NULL || right == NULL) {
        return false;
    }
    for (index = 0U; index < left_length && index < right_length; ++index) {
        difference |= (size_t)(unsigned char)(left[index] ^ right[index]);
    }
    return difference == 0U;
}

static bool role_from_text(const char *role_text, PrsUserRole *role) {
    if (strcmp(role_text, "RECEPTIONIST") == 0) {
        *role = PRS_ROLE_RECEPTIONIST;
    } else if (strcmp(role_text, "CLINICIAN") == 0) {
        *role = PRS_ROLE_CLINICIAN;
    } else if (strcmp(role_text, "ADMINISTRATOR") == 0) {
        *role = PRS_ROLE_ADMINISTRATOR;
    } else {
        return false;
    }
    return true;
}

static const char *role_text(PrsUserRole role) {
    switch (role) {
        case PRS_ROLE_RECEPTIONIST:
            return "RECEPTIONIST";
        case PRS_ROLE_CLINICIAN:
            return "CLINICIAN";
        case PRS_ROLE_ADMINISTRATOR:
            return "ADMINISTRATOR";
        default:
            return NULL;
    }
}

static bool audit_user_change(sqlite3 *connection, long long actor_user_id,
                              const char *action, const char *username,
                              const char *summary) {
    sqlite3_stmt *statement = NULL;
    int status = sqlite3_prepare_v2(
        connection,
        "INSERT INTO audit_logs(actor_user_id, action, entity_type, entity_id, "
        "new_value_summary) VALUES (?, ?, 'USER', ?, ?);",
        -1, &statement, NULL);

    if (status == SQLITE_OK) {
        (void)sqlite3_bind_int64(statement, 1, actor_user_id);
        (void)sqlite3_bind_text(statement, 2, action, -1, SQLITE_STATIC);
        (void)sqlite3_bind_text(statement, 3, username, -1, SQLITE_TRANSIENT);
        (void)sqlite3_bind_text(statement, 4, summary, -1, SQLITE_STATIC);
        status = sqlite3_step(statement);
    }
    if (statement != NULL) {
        (void)sqlite3_finalize(statement);
    }
    return status == SQLITE_DONE;
}

void prs_session_clear(PrsUserSession *session) {
    if (session != NULL) {
        secure_clear(session, sizeof(*session));
    }
}

bool prs_session_has_role(const PrsUserSession *session, PrsUserRole role) {
    return session != NULL && session->authenticated && session->role == role;
}

bool prs_auth_bootstrap_administrator(PrsDatabase *database, const char *username,
                                      const char *password, char *error_message,
                                      size_t error_message_size) {
    sqlite3_stmt *query = NULL;
    sqlite3_stmt *insert = NULL;
    char *hash = NULL;
    int status = SQLITE_ERROR;
    bool transaction_started = false;
    bool success = false;

    if (database == NULL || database->connection == NULL || !valid_username(username) ||
        !valid_password(password)) {
        set_error(error_message, error_message_size,
                  "Use a 3–64 character username and a 12–256 character password.");
        return false;
    }
    if (sqlite3_exec(database->connection, "BEGIN IMMEDIATE;", NULL, NULL, NULL) !=
        SQLITE_OK) {
        set_error(error_message, error_message_size,
                  "Unable to start secure administrator setup.");
        return false;
    }
    transaction_started = true;
    status = sqlite3_prepare_v2(
        database->connection,
        "SELECT COUNT(*) FROM users WHERE role = 'ADMINISTRATOR';", -1, &query,
        NULL);
    if (status != SQLITE_OK || sqlite3_step(query) != SQLITE_ROW) {
        set_error(error_message, error_message_size,
                  "Unable to check administrator setup status.");
        goto cleanup;
    }
    if (sqlite3_column_int(query, 0) != 0) {
        set_error(error_message, error_message_size,
                  "An administrator already exists; use administrator account management.");
        goto cleanup;
    }
    (void)sqlite3_finalize(query);
    query = NULL;
    hash = password_hash(password);
    if (hash == NULL) {
        set_error(error_message, error_message_size,
                  "Password hashing is unavailable on this system.");
        goto cleanup;
    }
    status = sqlite3_prepare_v2(
        database->connection,
        "INSERT INTO users(username, password_hash, role) VALUES (?, ?, 'ADMINISTRATOR');",
        -1, &insert, NULL);
    if (status != SQLITE_OK) {
        set_error(error_message, error_message_size,
                  "Unable to create the administrator account.");
        goto cleanup;
    }
    (void)sqlite3_bind_text(insert, 1, username, -1, SQLITE_TRANSIENT);
    (void)sqlite3_bind_text(insert, 2, hash, -1, SQLITE_TRANSIENT);
    if (sqlite3_step(insert) != SQLITE_DONE ||
        sqlite3_exec(database->connection, "COMMIT;", NULL, NULL, NULL) != SQLITE_OK) {
        set_error(error_message, error_message_size,
                  "Unable to complete administrator setup.");
        goto cleanup;
    }
    transaction_started = false;
    success = true;
    prs_log(PRS_LOG_INFO, "auth", "Initial administrator account created.");

cleanup:
    if (query != NULL) {
        (void)sqlite3_finalize(query);
    }
    if (insert != NULL) {
        (void)sqlite3_finalize(insert);
    }
    if (hash != NULL) {
        secure_clear(hash, strlen(hash));
        free(hash);
    }
    if (transaction_started) {
        (void)sqlite3_exec(database->connection, "ROLLBACK;", NULL, NULL, NULL);
    }
    return success;
}

bool prs_authenticate(PrsDatabase *database, const char *username,
                      const char *password, PrsUserSession *session,
                      char *error_message, size_t error_message_size) {
    sqlite3_stmt *statement = NULL;
    const char *stored_hash = NULL;
    const char *role_text = NULL;
    char *candidate_hash = NULL;
    void *work_buffer = NULL;
    int work_buffer_size = 0;
    int status = SQLITE_ERROR;
    bool authenticated = false;

    prs_session_clear(session);
    if (database == NULL || database->connection == NULL || !valid_username(username) ||
        !valid_password(password) || session == NULL) {
        set_error(error_message, error_message_size, "Invalid username or password.");
        return false;
    }
    status = sqlite3_prepare_v2(
        database->connection,
        "SELECT user_id, username, password_hash, role FROM users "
        "WHERE username = ? AND active = 1;",
        -1, &statement, NULL);
    if (status != SQLITE_OK) {
        set_error(error_message, error_message_size, "Authentication service unavailable.");
        return false;
    }
    (void)sqlite3_bind_text(statement, 1, username, -1, SQLITE_TRANSIENT);
    if (sqlite3_step(statement) != SQLITE_ROW) {
        set_error(error_message, error_message_size, "Invalid username or password.");
        prs_log(PRS_LOG_WARNING, "auth", "Authentication failed.");
        goto cleanup;
    }
    stored_hash = (const char *)sqlite3_column_text(statement, 2);
    role_text = (const char *)sqlite3_column_text(statement, 3);
    if (stored_hash == NULL || role_text == NULL) {
        set_error(error_message, error_message_size, "Authentication service unavailable.");
        goto cleanup;
    }
    candidate_hash = crypt_ra(password, stored_hash, &work_buffer, &work_buffer_size);
    if (candidate_hash == NULL || !constant_time_equal(candidate_hash, stored_hash) ||
        !role_from_text(role_text, &session->role)) {
        prs_session_clear(session);
        set_error(error_message, error_message_size, "Invalid username or password.");
        prs_log(PRS_LOG_WARNING, "auth", "Authentication failed.");
        goto cleanup;
    }
    session->user_id = sqlite3_column_int64(statement, 0);
    (void)snprintf(session->username, sizeof(session->username), "%s",
                   (const char *)sqlite3_column_text(statement, 1));
    session->authenticated = true;
    authenticated = true;
    prs_log(PRS_LOG_INFO, "auth", "Authentication succeeded.");

cleanup:
    if (work_buffer != NULL) {
        secure_clear(work_buffer, (size_t)work_buffer_size);
        free(work_buffer);
    }
    if (statement != NULL) {
        (void)sqlite3_finalize(statement);
    }
    return authenticated;
}

bool prs_auth_create_user(PrsDatabase *database, const PrsUserSession *actor,
                          const char *username, const char *password,
                          PrsUserRole role, char *error_message,
                          size_t error_message_size) {
    sqlite3_stmt *statement = NULL;
    const char *role_name = role_text(role);
    char *hash = NULL;
    int status = SQLITE_ERROR;

    if (!prs_session_has_role(actor, PRS_ROLE_ADMINISTRATOR)) {
        set_error(error_message, error_message_size,
                  "Only administrators may create user accounts.");
        return false;
    }
    if (database == NULL || database->connection == NULL || !valid_username(username) ||
        !valid_password(password) || role_name == NULL) {
        set_error(error_message, error_message_size,
                  "Use a valid username, role, and 12–256 character password.");
        return false;
    }
    hash = password_hash(password);
    if (hash == NULL || sqlite3_exec(database->connection, "BEGIN IMMEDIATE;", NULL,
                                     NULL, NULL) != SQLITE_OK) {
        if (hash != NULL) {
            secure_clear(hash, strlen(hash));
            free(hash);
        }
        set_error(error_message, error_message_size, "Unable to create the user account.");
        return false;
    }

    {
        sqlite3_stmt *exists = NULL;
        int exists_status = sqlite3_prepare_v2(
            database->connection,
            "SELECT 1 FROM users WHERE username = ? COLLATE NOCASE LIMIT 1;",
            -1, &exists, NULL);
        if (exists_status == SQLITE_OK) {
            (void)sqlite3_bind_text(exists, 1, username, -1, SQLITE_TRANSIENT);
            if (sqlite3_step(exists) == SQLITE_ROW) {
                (void)sqlite3_finalize(exists);
                (void)sqlite3_exec(database->connection, "ROLLBACK;", NULL, NULL, NULL);
                if (hash != NULL) {
                    secure_clear(hash, strlen(hash));
                    free(hash);
                }
                set_error(error_message, error_message_size,
                          "A user account with that username already exists.");
                return false;
            }
            (void)sqlite3_finalize(exists);
        }
    }

    status = sqlite3_prepare_v2(
        database->connection,
        "INSERT INTO users(username, password_hash, role) VALUES (?, ?, ?);", -1,
        &statement, NULL);
    if (status == SQLITE_OK) {
        (void)sqlite3_bind_text(statement, 1, username, -1, SQLITE_TRANSIENT);
        (void)sqlite3_bind_text(statement, 2, hash, -1, SQLITE_TRANSIENT);
        (void)sqlite3_bind_text(statement, 3, role_name, -1, SQLITE_STATIC);
        status = sqlite3_step(statement);
    }
    if (statement != NULL) {
        (void)sqlite3_finalize(statement);
    }
    if (hash != NULL) {
        secure_clear(hash, strlen(hash));
        free(hash);
    }
    if (status != SQLITE_DONE ||
        !audit_user_change(database->connection, actor->user_id, "USER_CREATED", username,
                           "User account created.") ||
        sqlite3_exec(database->connection, "COMMIT;", NULL, NULL, NULL) != SQLITE_OK) {
        (void)sqlite3_exec(database->connection, "ROLLBACK;", NULL, NULL, NULL);
        set_error(error_message, error_message_size,
                  "Unable to complete user account creation.");
        return false;
    }
    prs_log(PRS_LOG_INFO, "auth", "User account created.");
    return true;
}

bool prs_auth_set_user_active(PrsDatabase *database, const PrsUserSession *actor,
                              const char *username, bool active,
                              char *error_message, size_t error_message_size) {
    sqlite3_stmt *statement = NULL;
    int status = SQLITE_ERROR;

    if (!prs_session_has_role(actor, PRS_ROLE_ADMINISTRATOR) || database == NULL ||
        database->connection == NULL || !valid_username(username) ||
        strcmp(username, actor->username) == 0) {
        set_error(error_message, error_message_size,
                  "Only administrators may change another valid user account.");
        return false;
    }
    if (sqlite3_exec(database->connection, "BEGIN IMMEDIATE;", NULL, NULL, NULL) !=
        SQLITE_OK) {
        set_error(error_message, error_message_size, "Unable to update the user account.");
        return false;
    }
    status = sqlite3_prepare_v2(
        database->connection,
        "UPDATE users SET active = ?, updated_at = CURRENT_TIMESTAMP "
        "WHERE username = ? AND active <> ?;",
        -1, &statement, NULL);
    if (status == SQLITE_OK) {
        (void)sqlite3_bind_int(statement, 1, active ? 1 : 0);
        (void)sqlite3_bind_text(statement, 2, username, -1, SQLITE_TRANSIENT);
        (void)sqlite3_bind_int(statement, 3, active ? 1 : 0);
        status = sqlite3_step(statement);
    }
    if (statement != NULL) {
        (void)sqlite3_finalize(statement);
    }
    if (status != SQLITE_DONE || sqlite3_changes(database->connection) != 1 ||
        !audit_user_change(database->connection, actor->user_id,
                           active ? "USER_REACTIVATED" : "USER_DEACTIVATED", username,
                           active ? "User account activated." : "User account deactivated.") ||
        sqlite3_exec(database->connection, "COMMIT;", NULL, NULL, NULL) != SQLITE_OK) {
        (void)sqlite3_exec(database->connection, "ROLLBACK;", NULL, NULL, NULL);
        set_error(error_message, error_message_size,
                  "User account was not found or already has that status.");
        return false;
    }
    prs_log(PRS_LOG_INFO, "auth", active ? "User account activated."
                                          : "User account deactivated.");
    return true;
}
