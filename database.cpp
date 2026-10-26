#include "database.h"
#include <stdio.h>

void load_to_db(PGconn* conn, InventoryRecord* records, int count) {
    int i = 0;
    int keep_loading = (i < count);
    int successful_inserts = 0;

    while (keep_loading) {
        char query[2048];
        
        // Format the SQL insert string with the struct data
        snprintf(query, sizeof(query),
            "INSERT INTO staging_inventory (snapshot_date, sku_id, category, warehouse, current_stock, reorder_point, reorder_qty, unit_cost, stock_value, below_reorder) "
            "VALUES ('%s', '%s', '%s', '%s', %d, %d, %d, %f, %f, %d);",
            records[i].snapshot_date, records[i].sku_id, records[i].category, records[i].warehouse,
            records[i].current_stock, records[i].reorder_point, records[i].reorder_qty,
            records[i].unit_cost, records[i].stock_value, records[i].below_reorder);

        // Execute the query
        PGresult* res = PQexec(conn, query);
        int success = (PQresultStatus(res) == PGRES_COMMAND_OK);

        if (!success) {
            printf("Insert failed at row %d: %s\n", i, PQerrorMessage(conn));
        } else {
            successful_inserts++;
        }

        PQclear(res);
        i++;
        keep_loading = (i < count);
    }
    
    printf("Successfully loaded %d out of %d records into PostgreSQL.\n", successful_inserts, count);
}

