#include <iostream>
#include <cstdlib>
#include <libpq-fe.h>
#include "parser.h"
#include "database.h"

int main() {
    // Ensure this matches your Docker setup credentials
    const char* conninfo = "dbname=MyfirstDW user=postgres password=your_password host=127.0.0.1 port=5432";
    
    PGconn* conn = PQconnectdb(conninfo);
    int is_connected = (PQstatus(conn) == CONNECTION_OK);
    
    if (!is_connected) {
        std::cerr << "Connection to database failed: " << PQerrorMessage(conn) << "\n";
    } else {
        std::cout << "Successfully connected to the database.\n";
        
        int record_count = 0;
        
        // Extract: Parse the CSV file into a dynamically allocated array
        InventoryRecord* records = parse_csv("inventory-levels.csv", &record_count);
        
        int has_records = (records != NULL && record_count > 0);
        
        if (has_records) {
            std::cout << "Successfully parsed " << record_count << " rows from CSV.\n";
            
            // Load: Insert the records into PostgreSQL
            load_to_db(conn, records, record_count);
            
            // Free the contiguous memory block
            free(records);
        }
    }
    
    PQfinish(conn);
    return 0;
}

