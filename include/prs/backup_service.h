#ifndef PRS_BACKUP_SERVICE_H
#define PRS_BACKUP_SERVICE_H

#include <stdbool.h>
#include <stddef.h>

#include "prs/database.h"
#include "prs/models.h"

bool prs_backup_create_timestamped(PrsDatabase *database,
                                   const PrsUserSession *session,
                                   const char *directory, char *backup_path,
                                   size_t backup_path_size,
                                   char *error_message,
                                   size_t error_message_size);
bool prs_backup_restore(PrsDatabase *database, const PrsUserSession *session,
                        const char *backup_path, char *error_message,
                        size_t error_message_size);

#endif
