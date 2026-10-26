#ifndef DATABASE_H
#define DATABASE_H

#include <libpq-fe.h>
#include "parser.h"

// Iterates through the dynamically allocated array and inserts records into PostgreSQL
void load_to_db(PGconn* conn, InventoryRecord* records, int count);

#endif

