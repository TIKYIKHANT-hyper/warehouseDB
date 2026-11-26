#include "parser.h"
#include "database.h"
#include <cstdio>
#include <cstdlib>

int main(int argc, char* argv[]) {
    int record_count = 0;
    int exit_code = 1;
    const char* csv_filepath = (argc > 1) ? argv[1] : "inventory-levels.csv";

    printf("Starting C++ ETL Pipeline (Supply Chain Inventory)\n");
    printf("Source CSV: %s\n", csv_filepath);

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
                
                // clear_staging is 1, so uploading a new CSV cleanly replaces old staging data
                int load_status = db_load_records(conn, records, record_count, DB_DEFAULT_BATCH_SIZE, 1, &report);
                
                if (load_status == DB_OK) {
                    db_print_report(&report);
                    
                    // Phase 4: Transform 
                    db_transform_to_star(conn);
                    exit_code = 0;
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

    return exit_code;
}

