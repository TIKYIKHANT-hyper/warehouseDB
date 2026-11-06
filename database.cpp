/*
 * database.cpp - PostgreSQL interfacing layer (libpq)
 * Author: Xavier Suarez (role 3: C++ Database Interfacing)
 *
 * Responsibilities:
 *   1. Open / check / restore the connection (PQconnectdb, PQstatus, PQreset)
 *   2. Verify that the schema created by schema/ scripts exists
 *   3. Validate every InventoryRecord before sending it
 *   4. Convert the struct array into parameterized bulk INSERT statements
 *      (one statement per batch, all inside one transaction)
 *   5. Classify errors by SQLSTATE (network, schema, data, other)
 *   6. Move staging data into the Star Schema
 *
 * Rules: no std::vector, no break/continue/goto, flag-controlled loops,
 * one return per function, dynamic memory with malloc/free.
 */

#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS   /* getenv / snprintf portable usage on MSVC */
#endif

#include "database.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <chrono>
#include <thread>

/* ------------------------------------------------------------------ */
/* Constants                                                          */
/* ------------------------------------------------------------------ */

#define FIELDS_PER_ROW      10
#define NUMBER_TEXT_SIZE    32      /* text buffer for one numeric parameter */
#define ROW_TEXT_MAX        256     /* max length of one "($1::date,...)" group */
#define RETRY_DELAY_MS      1500
#define STOCK_VALUE_TOLERANCE 0.05

static const char* INSERT_HEADER =
    "INSERT INTO staging_inventory (snapshot_date, sku_id, category, warehouse, "
    "current_stock, reorder_point, reorder_qty, unit_cost, stock_value, below_reorder) VALUES ";

static const char* ROW_TEMPLATE =
    "($%d::date, $%d::varchar, $%d::varchar, $%d::varchar, $%d::int, "
    "$%d::int, $%d::int, $%d::numeric, $%d::numeric, $%d::int)";

static const char* EXPECTED_COLUMNS_QUERY =
    "SELECT COUNT(*) FROM information_schema.columns "
    "WHERE table_schema = 'public' AND table_name = 'staging_inventory' "
    "AND column_name IN ('snapshot_date', 'sku_id', 'category', 'warehouse', "
    "'current_stock', 'reorder_point', 'reorder_qty', 'unit_cost', "
    "'stock_value', 'below_reorder')";

static const char* STAR_TABLES_QUERY =
    "SELECT (to_regclass('public.dim_date') IS NOT NULL)::int "
    "+ (to_regclass('public.dim_product') IS NOT NULL)::int "
    "+ (to_regclass('public.dim_warehouse') IS NOT NULL)::int "
    "+ (to_regclass('public.fact_inventory') IS NOT NULL)::int";

/* Buffers reused by every batch (allocated once with malloc) */
struct InsertBuffers {
    char* sql;               /* query text                          */
    int sql_capacity;
    const char** values;     /* pointers passed to PQexecParams      */
    char* numbers;           /* text storage for numeric parameters */
    int* indexes;            /* positions of valid records in batch */
    int capacity;            /* max rows per batch                  */
};

/* ------------------------------------------------------------------ */
/* Small helpers                                                      */
/* ------------------------------------------------------------------ */

static void wait_ms(int milliseconds) {
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

static const char* env_or_default(const char* name, const char* fallback) {
    const char* value = getenv(name);
    const char* result = fallback;
    if (value != NULL && value[0] != '\0') {
        result = value;
    }
    return result;
}

static const char* error_name(int code) {
    const char* name = "UNKNOWN";
    if (code == DB_OK) name = "OK";
    else if (code == DB_ERR_CONNECTION) name = "CONNECTION ERROR";
    else if (code == DB_ERR_SCHEMA) name = "SCHEMA ERROR";
    else if (code == DB_ERR_DATA) name = "DATA ERROR";
    else if (code == DB_ERR_QUERY) name = "QUERY ERROR";
    else if (code == DB_ERR_MEMORY) name = "MEMORY ERROR";
    else if (code == DB_SKIPPED) name = "SKIPPED";
    return name;
}

/* Maps a failed result to one of the DB_ERR_* codes using SQLSTATE
 * and prints a readable message. res may be NULL. */
static int classify_error(PGconn* conn, PGresult* res, const char* context) {
    int code = DB_ERR_QUERY;
    const char* sqlstate = NULL;
    const char* message = PQerrorMessage(conn);

    if (res != NULL) {
        sqlstate = PQresultErrorField(res, PG_DIAG_SQLSTATE);
        if (PQresultErrorMessage(res)[0] != '\0') {
            message = PQresultErrorMessage(res);
        }
    }

    if (PQstatus(conn) == CONNECTION_BAD) {
        code = DB_ERR_CONNECTION;
    } else if (sqlstate != NULL) {
        if (strncmp(sqlstate, "08", 2) == 0) code = DB_ERR_CONNECTION;
        else if (strncmp(sqlstate, "42", 2) == 0) code = DB_ERR_SCHEMA;
        else if (strncmp(sqlstate, "22", 2) == 0 || strncmp(sqlstate, "23", 2) == 0) code = DB_ERR_DATA;
    }

    fprintf(stderr, "[db] %s failed -> %s (SQLSTATE %s): %s",
            context, error_name(code), sqlstate != NULL ? sqlstate : "-----", message);
    return code;
}

/* Executes a command that returns no rows (BEGIN, COMMIT, DDL, INSERT...). */
static int exec_command(PGconn* conn, const char* sql, const char* context) {
    int code = DB_OK;
    PGresult* res = PQexec(conn, sql);
    ExecStatusType status = PQresultStatus(res);

    if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK) {
        code = classify_error(conn, res, context);
    }
    PQclear(res);
    return code;
}

/* Same as exec_command but prints how many rows were affected. */
static int exec_counted(PGconn* conn, const char* sql, const char* context) {
    int code = DB_OK;
    PGresult* res = PQexec(conn, sql);

    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        code = classify_error(conn, res, context);
    } else {
        printf("[db]   %-32s %s rows\n", context, PQcmdTuples(res));
    }
    PQclear(res);
    return code;
}

/* Runs a query that returns exactly one integer (e.g. SELECT COUNT(*)). */
static int query_single_int(PGconn* conn, const char* sql, const char* context, int* out_value) {
    int code = DB_OK;
    PGresult* res = PQexec(conn, sql);
    int valid = (PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) == 1 && PQnfields(res) == 1);

    if (!valid) {
        code = classify_error(conn, res, context);
    } else {
        *out_value = atoi(PQgetvalue(res, 0, 0));
    }
    PQclear(res);
    return code;
}

/* A fatal error makes the rest of the load pointless */
static int is_fatal(int code) {
    return (code == DB_ERR_CONNECTION || code == DB_ERR_SCHEMA || code == DB_ERR_MEMORY);
}

/* ------------------------------------------------------------------ */
/* Connection                                                         */
/* ------------------------------------------------------------------ */

void db_build_conninfo(char* buffer, int buffer_size) {
    snprintf(buffer, (size_t)buffer_size,
             "host='%s' port='%s' dbname='%s' user='%s' password='%s' connect_timeout=5",
             env_or_default("DB_HOST", "127.0.0.1"),
             env_or_default("DB_PORT", "5432"),
             env_or_default("DB_NAME", "MyfirstDW"),
             env_or_default("DB_USER", "postgres"),
             env_or_default("DB_PASSWORD", "your_password"));
}

PGconn* db_connect(const char* conninfo, int max_retries) {
    PGconn* conn = NULL;
    int attempt = 0;
    int connected = 0;
    int retries = (max_retries < 1) ? 1 : max_retries;

    while (!connected && attempt < retries) {
        attempt++;
        if (conn != NULL) {
            PQfinish(conn);
        }
        conn = PQconnectdb(conninfo);
        connected = (conn != NULL && PQstatus(conn) == CONNECTION_OK);

        if (!connected) {
            fprintf(stderr, "[db] connection attempt %d/%d failed: %s",
                    attempt, retries, conn != NULL ? PQerrorMessage(conn) : "out of memory\n");
            if (attempt < retries) {
                wait_ms(RETRY_DELAY_MS);
            }
        }
    }

    if (connected) {
        PQsetClientEncoding(conn, "UTF8");
        printf("[db] connected to %s:%s/%s (server version %d)\n",
               PQhost(conn), PQport(conn), PQdb(conn), PQserverVersion(conn));
    } else if (conn != NULL) {
        fprintf(stderr, "[db] giving up: is the Docker container running? (docker compose up -d)\n");
        PQfinish(conn);
        conn = NULL;
    }
    return conn;
}

int db_ensure_connection(PGconn* conn, int max_retries) {
    int code = DB_ERR_CONNECTION;
    int attempt = 0;
    int alive = (conn != NULL && PQstatus(conn) == CONNECTION_OK);

    while (conn != NULL && !alive && attempt < max_retries) {
        attempt++;
        fprintf(stderr, "[db] connection lost, reset attempt %d/%d\n", attempt, max_retries);
        PQreset(conn);
        alive = (PQstatus(conn) == CONNECTION_OK);
        if (!alive && attempt < max_retries) {
            wait_ms(RETRY_DELAY_MS);
        }
    }

    if (alive) {
        code = DB_OK;
    }
    return code;
}

void db_disconnect(PGconn* conn) {
    if (conn != NULL) {
        PQfinish(conn);
        printf("[db] connection closed\n");
    }
}

/* ------------------------------------------------------------------ */
/* Schema check                                                       */
/* ------------------------------------------------------------------ */

int db_check_schema(PGconn* conn) {
    int found_columns = 0;
    int code = db_ensure_connection(conn, DB_DEFAULT_RETRIES);

    if (code == DB_OK) {
        code = query_single_int(conn, EXPECTED_COLUMNS_QUERY, "schema check", &found_columns);
    }

    if (code == DB_OK && found_columns == 0) {
        fprintf(stderr, "[db] table staging_inventory not found.\n"
                        "     The schema/ scripts run only on the FIRST start of the container:\n"
                        "     docker compose down -v && docker compose up -d\n");
        code = DB_ERR_SCHEMA;
    } else if (code == DB_OK && found_columns != FIELDS_PER_ROW) {
        fprintf(stderr, "[db] staging_inventory has only %d of %d expected columns\n",
                found_columns, FIELDS_PER_ROW);
        code = DB_ERR_SCHEMA;
    } else if (code == DB_OK) {
        printf("[db] schema OK: staging_inventory has all %d columns\n", FIELDS_PER_ROW);
    }
    return code;
}

/* ------------------------------------------------------------------ */
/* Record validation (protects the database from parser garbage)      */
/* ------------------------------------------------------------------ */

/* Text must be terminated inside its buffer, non-empty and printable */
static int is_valid_text(const char* text, int buffer_size) {
    int valid = (memchr(text, '\0', (size_t)buffer_size) != NULL) && (text[0] != '\0');
    int i = 0;

    while (valid && text[i] != '\0') {
        valid = (isprint((unsigned char)text[i]) != 0);
        i++;
    }
    return valid;
}

/* Expected format: YYYY-MM-DD */
static int is_valid_date(const char* text, int buffer_size) {
    int valid = is_valid_text(text, buffer_size) && (strlen(text) == 10);
    int i = 0;

    while (valid && i < 10) {
        if (i == 4 || i == 7) {
            valid = (text[i] == '-');
        } else {
            valid = (isdigit((unsigned char)text[i]) != 0);
        }
        i++;
    }

    if (valid) {
        int month = (text[5] - '0') * 10 + (text[6] - '0');
        int day = (text[8] - '0') * 10 + (text[9] - '0');
        valid = (month >= 1 && month <= 12 && day >= 1 && day <= 31);
    }
    return valid;
}

/* Returns NULL if the record is valid, otherwise the reason */
static const char* validate_record(const InventoryRecord* record) {
    const char* reason = NULL;
    double expected_value = record->current_stock * record->unit_cost;

    if (!is_valid_date(record->snapshot_date, (int)sizeof(record->snapshot_date))) {
        reason = "invalid snapshot_date";
    } else if (!is_valid_text(record->sku_id, (int)sizeof(record->sku_id))) {
        reason = "invalid sku_id";
    } else if (!is_valid_text(record->category, (int)sizeof(record->category))) {
        reason = "invalid category";
    } else if (!is_valid_text(record->warehouse, (int)sizeof(record->warehouse))) {
        reason = "invalid warehouse";
    } else if (record->current_stock < 0 || record->reorder_point < 0 || record->reorder_qty < 0) {
        reason = "negative quantity";
    } else if (!isfinite(record->unit_cost) || record->unit_cost < 0.0) {
        reason = "invalid unit_cost";
    } else if (!isfinite(record->stock_value) || record->stock_value < 0.0) {
        reason = "invalid stock_value";
    } else if (fabs(expected_value - record->stock_value) > STOCK_VALUE_TOLERANCE) {
        reason = "stock_value != current_stock * unit_cost (row probably truncated)";
    } else if (record->below_reorder != 0 && record->below_reorder != 1) {
        reason = "below_reorder must be 0 or 1";
    }
    return reason;
}

/* ------------------------------------------------------------------ */
/* Bulk INSERT                                                        */
/* ------------------------------------------------------------------ */

static int buffers_create(InsertBuffers* buffers, int batch_size) {
    int code = DB_OK;
    int params = batch_size * FIELDS_PER_ROW;

    buffers->capacity = batch_size;
    buffers->sql_capacity = (int)strlen(INSERT_HEADER) + batch_size * ROW_TEXT_MAX + 16;
    buffers->sql = (char*)malloc((size_t)buffers->sql_capacity);
    buffers->values = (const char**)malloc((size_t)params * sizeof(const char*));
    buffers->numbers = (char*)malloc((size_t)params * NUMBER_TEXT_SIZE);
    buffers->indexes = (int*)malloc((size_t)batch_size * sizeof(int));

    if (buffers->sql == NULL || buffers->values == NULL ||
        buffers->numbers == NULL || buffers->indexes == NULL) {
        fprintf(stderr, "[db] could not allocate insert buffers\n");
        code = DB_ERR_MEMORY;
    }
    return code;
}

static void buffers_destroy(InsertBuffers* buffers) {
    free(buffers->sql);
    free(buffers->values);
    free(buffers->numbers);
    free(buffers->indexes);
    buffers->sql = NULL;
    buffers->values = NULL;
    buffers->numbers = NULL;
    buffers->indexes = NULL;
}

/* Converts one struct into 10 text parameters starting at slot */
static void fill_parameters(const InventoryRecord* record, InsertBuffers* buffers, int slot) {
    const char** values = buffers->values + slot;
    char* text = buffers->numbers + (size_t)slot * NUMBER_TEXT_SIZE;

    snprintf(text + 0 * NUMBER_TEXT_SIZE, NUMBER_TEXT_SIZE, "%d", record->current_stock);
    snprintf(text + 1 * NUMBER_TEXT_SIZE, NUMBER_TEXT_SIZE, "%d", record->reorder_point);
    snprintf(text + 2 * NUMBER_TEXT_SIZE, NUMBER_TEXT_SIZE, "%d", record->reorder_qty);
    snprintf(text + 3 * NUMBER_TEXT_SIZE, NUMBER_TEXT_SIZE, "%.2f", record->unit_cost);
    snprintf(text + 4 * NUMBER_TEXT_SIZE, NUMBER_TEXT_SIZE, "%.2f", record->stock_value);
    snprintf(text + 5 * NUMBER_TEXT_SIZE, NUMBER_TEXT_SIZE, "%d", record->below_reorder);

    values[0] = record->snapshot_date;
    values[1] = record->sku_id;
    values[2] = record->category;
    values[3] = record->warehouse;
    values[4] = text + 0 * NUMBER_TEXT_SIZE;
    values[5] = text + 1 * NUMBER_TEXT_SIZE;
    values[6] = text + 2 * NUMBER_TEXT_SIZE;
    values[7] = text + 3 * NUMBER_TEXT_SIZE;
    values[8] = text + 4 * NUMBER_TEXT_SIZE;
    values[9] = text + 5 * NUMBER_TEXT_SIZE;
}

/* Builds and executes ONE multi-row INSERT for row_count records.
 * indexes[] holds the positions of those records in the array. */
static int insert_rows(PGconn* conn, const InventoryRecord* records,
                       const int* indexes, int row_count, InsertBuffers* buffers) {
    int code = DB_OK;
    int length = snprintf(buffers->sql, (size_t)buffers->sql_capacity, "%s", INSERT_HEADER);
    int row = 0;

    while (row < row_count) {
        int p = row * FIELDS_PER_ROW;   /* first parameter index of this row (0-based) */
        fill_parameters(&records[indexes[row]], buffers, p);
        length += snprintf(buffers->sql + length, (size_t)(buffers->sql_capacity - length),
                           "%s", row > 0 ? ", " : "");
        length += snprintf(buffers->sql + length, (size_t)(buffers->sql_capacity - length),
                           ROW_TEMPLATE, p + 1, p + 2, p + 3, p + 4, p + 5,
                           p + 6, p + 7, p + 8, p + 9, p + 10);
        row++;
    }

    PGresult* res = PQexecParams(conn, buffers->sql, row_count * FIELDS_PER_ROW,
                                 NULL, buffers->values, NULL, NULL, 0);
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        code = classify_error(conn, res, "bulk INSERT");
    }
    PQclear(res);
    return code;
}

/* Picks up to batch_size valid records starting at *position.
 * Invalid records are counted and reported, never sent. */
static int collect_batch(const InventoryRecord* records, int count, int* position,
                         InsertBuffers* buffers, LoadReport* report) {
    int collected = 0;

    while (*position < count && collected < buffers->capacity) {
        const char* reason = validate_record(&records[*position]);
        if (reason != NULL) {
            fprintf(stderr, "[db] row %d rejected before sending: %s\n", *position + 1, reason);
            report->invalid++;
        } else {
            buffers->indexes[collected] = *position;
            collected++;
        }
        (*position)++;
    }
    return collected;
}

/* Fallback when a whole batch fails: insert row by row (each inside
 * its own SAVEPOINT) so one bad row does not discard 199 good ones. */
static int insert_one_by_one(PGconn* conn, const InventoryRecord* records, int row_count,
                             InsertBuffers* buffers, LoadReport* report) {
    int code = DB_OK;
    int row = 0;

    while (row < row_count && !is_fatal(code)) {
        code = exec_command(conn, "SAVEPOINT row_sp", "SAVEPOINT row");
        if (code == DB_OK) {
            code = insert_rows(conn, records, &buffers->indexes[row], 1, buffers);
            if (code == DB_OK) {
                exec_command(conn, "RELEASE SAVEPOINT row_sp", "RELEASE row");
                report->inserted++;
            } else if (!is_fatal(code)) {
                fprintf(stderr, "[db] row %d rejected by server\n", buffers->indexes[row] + 1);
                exec_command(conn, "ROLLBACK TO SAVEPOINT row_sp", "ROLLBACK row");
                report->failed++;
                code = DB_OK;
            }
        }
        row++;
    }
    return code;
}

/* Inserts one batch protected by a SAVEPOINT */
static int load_batch(PGconn* conn, const InventoryRecord* records, int row_count,
                      InsertBuffers* buffers, LoadReport* report) {
    int code = exec_command(conn, "SAVEPOINT batch_sp", "SAVEPOINT batch");

    if (code == DB_OK) {
        code = insert_rows(conn, records, buffers->indexes, row_count, buffers);
        report->batches++;
        if (code == DB_OK) {
            exec_command(conn, "RELEASE SAVEPOINT batch_sp", "RELEASE batch");
            report->inserted += row_count;
        } else if (!is_fatal(code)) {
            fprintf(stderr, "[db] batch failed, retrying its %d rows one by one\n", row_count);
            code = exec_command(conn, "ROLLBACK TO SAVEPOINT batch_sp", "ROLLBACK batch");
            if (code == DB_OK) {
                code = insert_one_by_one(conn, records, row_count, buffers, report);
            }
        }
    }
    return code;
}

/* Finishes the transaction: COMMIT on success, ROLLBACK otherwise */
static int finish_transaction(PGconn* conn, int code, LoadReport* report) {
    int result = code;

    if (code == DB_OK) {
        result = exec_command(conn, "COMMIT", "COMMIT");
    }
    if (result != DB_OK) {
        if (PQstatus(conn) == CONNECTION_OK) {
            exec_command(conn, "ROLLBACK", "ROLLBACK");
        }
        fprintf(stderr, "[db] transaction rolled back: nothing was saved\n");
        report->inserted = 0;
    }
    return result;
}

int db_load_records(PGconn* conn, const InventoryRecord* records, int count,
                    int batch_size, int clear_staging, LoadReport* report) {
    InsertBuffers buffers;
    int position = 0;
    int size = (batch_size < 1) ? DB_DEFAULT_BATCH_SIZE : batch_size;
    int code = DB_OK;

    memset(report, 0, sizeof(*report));
    memset(&buffers, 0, sizeof(buffers));
    report->total = count;

    if (records == NULL || count <= 0) {
        fprintf(stderr, "[db] nothing to load\n");
        code = DB_ERR_DATA;
    }
    if (code == DB_OK) code = db_ensure_connection(conn, DB_DEFAULT_RETRIES);
    if (code == DB_OK) code = buffers_create(&buffers, size);
    if (code == DB_OK) code = exec_command(conn, "BEGIN", "BEGIN");

    int transaction_open = (code == DB_OK);

    if (code == DB_OK && clear_staging) {
        code = exec_command(conn, "TRUNCATE staging_inventory RESTART IDENTITY", "TRUNCATE staging");
    }

    while (code == DB_OK && position < count) {
        int row_count = collect_batch(records, count, &position, &buffers, report);
        if (row_count > 0) {
            code = load_batch(conn, records, row_count, &buffers, report);
        }
    }

    if (transaction_open) {
        code = finish_transaction(conn, code, report);
    }
    buffers_destroy(&buffers);
    return code;
}

void db_print_report(const LoadReport* report) {
    printf("\n========== LOAD REPORT ==========\n");
    printf(" records from parser : %d\n", report->total);
    printf(" rejected (invalid)  : %d\n", report->invalid);
    printf(" rejected (server)   : %d\n", report->failed);
    printf(" inserted            : %d\n", report->inserted);
    printf(" bulk INSERT batches : %d\n", report->batches);
    printf("=================================\n\n");
}

/* ------------------------------------------------------------------ */
/* Staging -> Star Schema                                             */
/* ------------------------------------------------------------------ */

struct SqlStep {
    const char* name;
    const char* sql;
};

static const SqlStep STAR_STEPS[] = {
    { "dim_date",
      "INSERT INTO dim_date (date_key, full_date, year, quarter, month, month_name, "
      "day, day_of_week, day_name, is_weekend) "
      "SELECT DISTINCT TO_CHAR(snapshot_date, 'YYYYMMDD')::int, snapshot_date, "
      "EXTRACT(YEAR FROM snapshot_date)::int, EXTRACT(QUARTER FROM snapshot_date)::int, "
      "EXTRACT(MONTH FROM snapshot_date)::int, TRIM(TO_CHAR(snapshot_date, 'Month')), "
      "EXTRACT(DAY FROM snapshot_date)::int, EXTRACT(ISODOW FROM snapshot_date)::int, "
      "TRIM(TO_CHAR(snapshot_date, 'Day')), EXTRACT(ISODOW FROM snapshot_date) IN (6, 7) "
      "FROM staging_inventory WHERE snapshot_date IS NOT NULL "
      "ON CONFLICT (date_key) DO NOTHING" },

    { "dim_product",
      "INSERT INTO dim_product (sku_id, category) "
      "SELECT sku_id, MAX(category) FROM staging_inventory GROUP BY sku_id "
      "ON CONFLICT (sku_id) DO UPDATE SET category = EXCLUDED.category" },

    { "dim_warehouse",
      "INSERT INTO dim_warehouse (warehouse_code, region) "
      "SELECT DISTINCT warehouse, REPLACE(warehouse, 'WH-', '') FROM staging_inventory "
      "ON CONFLICT (warehouse_code) DO NOTHING" },

    { "fact_inventory (clear)",
      "DELETE FROM fact_inventory" },

    { "fact_inventory",
      "INSERT INTO fact_inventory (date_key, product_key, warehouse_key, current_stock, "
      "reorder_point, reorder_qty, unit_cost, stock_value, below_reorder) "
      "SELECT d.date_key, p.product_key, w.warehouse_key, s.current_stock, s.reorder_point, "
      "s.reorder_qty, s.unit_cost, s.stock_value, (s.below_reorder = 1) "
      "FROM staging_inventory s "
      "JOIN dim_date d ON d.full_date = s.snapshot_date "
      "JOIN dim_product p ON p.sku_id = s.sku_id "
      "JOIN dim_warehouse w ON w.warehouse_code = s.warehouse" }
};

int db_transform_to_star(PGconn* conn) {
    int star_tables = 0;
    int step = 0;
    int step_count = (int)(sizeof(STAR_STEPS) / sizeof(STAR_STEPS[0]));
    int code = db_ensure_connection(conn, DB_DEFAULT_RETRIES);

    if (code == DB_OK) {
        code = query_single_int(conn, STAR_TABLES_QUERY, "star schema check", &star_tables);
    }

    if (code == DB_OK && star_tables != 4) {
        printf("[db] star schema not found (%d/4 tables): skipping transform\n", star_tables);
        code = DB_SKIPPED;
    } else if (code == DB_OK) {
        printf("[db] staging -> star schema:\n");
        code = exec_command(conn, "BEGIN", "BEGIN");
        int transaction_open = (code == DB_OK);

        while (code == DB_OK && step < step_count) {
            code = exec_counted(conn, STAR_STEPS[step].sql, STAR_STEPS[step].name);
            step++;
        }

        if (transaction_open && code == DB_OK) {
            code = exec_command(conn, "COMMIT", "COMMIT");
        } else if (transaction_open && PQstatus(conn) == CONNECTION_OK) {
            exec_command(conn, "ROLLBACK", "ROLLBACK");
            fprintf(stderr, "[db] star transform rolled back\n");
        }
    }
    return code;
}

