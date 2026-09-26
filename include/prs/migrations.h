#ifndef PRS_MIGRATIONS_H
#define PRS_MIGRATIONS_H

#include <stdbool.h>
#include <stddef.h>

#include <sqlite3.h>

bool prs_migrations_apply(sqlite3 *connection, char *error_message,
                          size_t error_message_size);

#endif
