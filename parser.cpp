#include "parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

InventoryRecord* parse_csv(const char* filename, int* out_count) {
    FILE* file = fopen(filename, "r");
    int is_open = (file != NULL);
    
    if (!is_open) {
        printf("Error: Could not open file %s\n", filename);
        *out_count = 0;
        return NULL;
    }

    int line_count = 0;
    char buffer[512];
    
    // First pass: Count total lines to size the memory block
    int reading = (fgets(buffer, sizeof(buffer), file) != NULL);
    while (reading) {
        line_count++;
        reading = (fgets(buffer, sizeof(buffer), file) != NULL);
    }
    
    int data_rows = line_count - 1; // Exclude the header row
    InventoryRecord* records = (InventoryRecord*)malloc(data_rows * sizeof(InventoryRecord));
    
    int memory_allocated = (records != NULL);
    if (!memory_allocated) {
        printf("Memory allocation failed.\n");
        fclose(file);
        *out_count = 0;
        return NULL;
    }

    // Reset file pointer for the second pass
    rewind(file);
    fgets(buffer, sizeof(buffer), file); // Skip the header row
    
    int current_row = 0;
    int keep_parsing = (current_row < data_rows) && (fgets(buffer, sizeof(buffer), file) != NULL);
    
    while (keep_parsing) {
        char* token = strtok(buffer, ",");
        int token_valid = (token != NULL);
        
        if (token_valid) {
            strncpy(records[current_row].snapshot_date, token, 14);
            records[current_row].snapshot_date[14] = '\0';
        }
        
        if (token_valid) { token = strtok(NULL, ","); token_valid = (token != NULL); }
        if (token_valid) {
            strncpy(records[current_row].sku_id, token, 19);
            records[current_row].sku_id[19] = '\0';
        }
        
        if (token_valid) { token = strtok(NULL, ","); token_valid = (token != NULL); }
        if (token_valid) {
            strncpy(records[current_row].category, token, 19);
            records[current_row].category[19] = '\0';
        }
        
        if (token_valid) { token = strtok(NULL, ","); token_valid = (token != NULL); }
        if (token_valid) {
            strncpy(records[current_row].warehouse, token, 29);
            records[current_row].warehouse[29] = '\0';
        }
        
        if (token_valid) { token = strtok(NULL, ","); token_valid = (token != NULL); }
        if (token_valid) records[current_row].current_stock = atoi(token);
        
        if (token_valid) { token = strtok(NULL, ","); token_valid = (token != NULL); }
        if (token_valid) records[current_row].reorder_point = atoi(token);
        
        if (token_valid) { token = strtok(NULL, ","); token_valid = (token != NULL); }
        if (token_valid) records[current_row].reorder_qty = atoi(token);
        
        if (token_valid) { token = strtok(NULL, ","); token_valid = (token != NULL); }
        if (token_valid) records[current_row].unit_cost = atof(token);
        
        if (token_valid) { token = strtok(NULL, ","); token_valid = (token != NULL); }
        if (token_valid) records[current_row].stock_value = atof(token);
        
        if (token_valid) { token = strtok(NULL, ","); token_valid = (token != NULL); }
        if (token_valid) records[current_row].below_reorder = atoi(token);

        current_row++;
        keep_parsing = (current_row < data_rows) && (fgets(buffer, sizeof(buffer), file) != NULL);
    }
    
    fclose(file);
    *out_count = current_row;
    return records;
}

