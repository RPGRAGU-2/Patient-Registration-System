#ifndef PRS_DATABASE_H
#define PRS_DATABASE_H

#include <stdbool.h>
#include <stddef.h>

#include <sqlite3.h>

typedef struct {
    sqlite3 *connection;
} PrsDatabase;

bool prs_database_open(PrsDatabase *database, const char *path,
                       char *error_message, size_t error_message_size);
void prs_database_close(PrsDatabase *database);

#endif
