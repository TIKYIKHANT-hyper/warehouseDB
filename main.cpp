#include "parser.h"
#include "database.h"
#include <cstdio>
#include <cstdlib>

int main() {
    int record_count = 0;
    const char* csv_filepath = "inventory-levels.csv";

    printf("==================================================\n");
    printf("Starting C++ ETL Pipeline (Supply Chain Inventory)\n");
    printf("==================================================\n");

    // Phase 1: Extract (Parser Developer's Code)
    InventoryRecord* records = parse_csv(csv_filepath, &record_count);
    int extraction_success = (records != nullptr && record_count > 0);

    if (extraction_success) {
        printf("[SUCCESS] Parsed %d rows into dynamic heap memory.\n\n", record_count);
        
        // Phase 2: Connect
        char conninfo[256];
        db_build_conninfo(conninfo, sizeof(conninfo));
        
        PGconn* conn = db_connect(conninfo, DB_DEFAULT_RETRIES);
        int is_connected = (conn != nullptr);

        if (is_connected) {
            
            // Phase 3: Check Schema & Load (Xavier's Code)
            int schema_valid = (db_check_schema(conn) == DB_OK);
            
            if (schema_valid) {
                LoadReport report;
                
                // Set clear_staging to 1 so you can safely run the program multiple times
                int load_status = db_load_records(conn, records, record_count, DB_DEFAULT_BATCH_SIZE, 1, &report);
                
                if (load_status == DB_OK) {
                    db_print_report(&report);
                    
                    // Phase 4: Transform 
                    db_transform_to_star(conn);
                } else {
                    printf("[ERROR] Database load failed. Check error logs.\n");
                }
            }
            
            db_disconnect(conn);
        }
    } else {
        printf("[FAILED] Extraction aborted. Check your CSV filepath.\n");
    }

    // Phase 5: Cleanup
    if (records != nullptr) {
        free(records);
        printf("\n[CLEANUP] Heap memory freed. Pipeline terminated.\n");
    }

    return 0;
}

