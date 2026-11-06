#ifndef DATABASE_H
#define DATABASE_H

/*
 * database.h - PostgreSQL interfacing layer (libpq)
 * Author: Xavier Suarez (role 3: C++ Database Interfacing)
 *
 * Pipeline position:
 *   parser.cpp (InventoryRecord array) -> database.cpp -> PostgreSQL
 *
 * Rules followed: no std::vector, no break/continue/goto,
 * flag-controlled loops, one return per function, malloc/free.
 */

#include <libpq-fe.h>
#include "parser.h"

/* Result codes returned by every db_* function */
#define DB_OK              0
#define DB_ERR_CONNECTION  1   /* network / server unreachable (SQLSTATE 08xxx) */
#define DB_ERR_SCHEMA      2   /* missing table or column (SQLSTATE 42xxx)      */
#define DB_ERR_DATA        3   /* invalid value / constraint (SQLSTATE 22, 23)  */
#define DB_ERR_QUERY       4   /* any other SQL error                           */
#define DB_ERR_MEMORY      5   /* malloc failed                                 */
#define DB_SKIPPED         6   /* optional step not executed                    */

#define DB_DEFAULT_BATCH_SIZE 200
#define DB_DEFAULT_RETRIES    3

/* Statistics filled by db_load_records */
struct LoadReport {
    int total;          /* records received from the parser          */
    int invalid;        /* rejected by local validation (never sent) */
    int inserted;       /* rows committed to staging_inventory       */
    int failed;         /* rows rejected by the server               */
    int batches;        /* bulk INSERT statements executed           */
};

/* Builds a libpq connection string from environment variables
 * (DB_HOST, DB_PORT, DB_NAME, DB_USER, DB_PASSWORD) with defaults
 * matching docker-compose.yml. */
void db_build_conninfo(char* buffer, int buffer_size);

/* Opens a connection, retrying up to max_retries times.
 * Returns NULL if the connection could not be established. */
PGconn* db_connect(const char* conninfo, int max_retries);

/* Checks the connection and tries PQreset if it was lost. */
int db_ensure_connection(PGconn* conn, int max_retries);

/* Verifies that staging_inventory exists with the expected columns. */
int db_check_schema(PGconn* conn);

/* Validates and bulk-inserts the records inside one transaction.
 * If clear_staging != 0 the staging table is truncated first,
 * so running the program twice does not duplicate data.
 * A failing batch is retried row by row to isolate bad rows. */
int db_load_records(PGconn* conn, const InventoryRecord* records, int count,
                    int batch_size, int clear_staging, LoadReport* report);

/* Moves data from staging into the Star Schema (dimensions first,
 * then the fact table). Returns DB_SKIPPED if the star tables
 * do not exist yet. */
int db_transform_to_star(PGconn* conn);

/* Prints the content of a LoadReport. */
void db_print_report(const LoadReport* report);

/* Closes the connection (safe to call with NULL). */
void db_disconnect(PGconn* conn);

#endif

