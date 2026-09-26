#ifndef PRS_AUTH_H
#define PRS_AUTH_H

#include <stdbool.h>
#include <stddef.h>

#include "prs/database.h"
#include "prs/models.h"

bool prs_auth_bootstrap_administrator(PrsDatabase *database, const char *username,
                                      const char *password, char *error_message,
                                      size_t error_message_size);
bool prs_authenticate(PrsDatabase *database, const char *username,
                      const char *password, PrsUserSession *session,
                      char *error_message, size_t error_message_size);
bool prs_auth_create_user(PrsDatabase *database, const PrsUserSession *actor,
                          const char *username, const char *password,
                          PrsUserRole role, char *error_message,
                          size_t error_message_size);
bool prs_auth_set_user_active(PrsDatabase *database, const PrsUserSession *actor,
                              const char *username, bool active,
                              char *error_message, size_t error_message_size);
bool prs_session_has_role(const PrsUserSession *session, PrsUserRole role);
void prs_session_clear(PrsUserSession *session);

#endif
