#define _CRT_SECURE_NO_WARNINGS
#include "parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Helper to strip carriage returns and newlines without using break */
static void strip_newline(char* line) {
    if (line != NULL) {
        size_t i = 0;
        int end_reached = 0;
        while (!end_reached) {
            if (line[i] == '\0') {
                end_reached = 1;
            }
            else if (line[i] == '\r' || line[i] == '\n') {
                line[i] = '\0';
                end_reached = 1;
            }
            else {
                i++;
            }
        }
    }
}

/* Custom token extractor that safely ignores commas inside quotation marks */
static void get_next_token(const char* line, size_t* cursor, char* out_buf, size_t max_len) {
    size_t out_idx = 0;
    int token_complete = 0;
    int inside_quotes = 0;

    while (!token_complete) {
        char current_char = line[*cursor];

        if (current_char == '\0') {
            token_complete = 1;
        }
        else if (current_char == '"') {
            inside_quotes = !inside_quotes;
            (*cursor)++;
        }
        else if (current_char == ',' && !inside_quotes) {
            (*cursor)++;
            token_complete = 1;
        }
        else {
            if (out_idx + 1 < max_len) {
                out_buf[out_idx] = current_char;
                out_idx++;
            }
            (*cursor)++;
        }
    }
    out_buf[out_idx] = '\0';
}

InventoryRecord* parse_csv(const char* filepath, int* out_count) {
    *out_count = 0;
    FILE* file = fopen(filepath, "r");
    if (file == NULL) {
        printf("[ERROR] Parser could not open file: %s\n", filepath);
        return NULL;
    }

    size_t capacity = 1024;
    InventoryRecord* records = (InventoryRecord*)malloc(capacity * sizeof(InventoryRecord));
    int allocation_failed = (records == NULL);

    char line_buffer[1024];
    char temp_buffer[64]; /* For numerical conversions (atoi/atof) */
    
    /* Skip the header row */
    char* header_check = fgets(line_buffer, sizeof(line_buffer), file);
    int keep_reading = (header_check != NULL) && (!allocation_failed);

    while (keep_reading) {
        char* read_ptr = fgets(line_buffer, sizeof(line_buffer), file);
        if (read_ptr == NULL) {
            keep_reading = 0;
        }
        else {
            strip_newline(line_buffer);

            if (line_buffer[0] != '\0') {
                /* Dynamically double the array size if we hit capacity */
                if ((size_t)(*out_count) >= capacity) {
                    size_t new_capacity = capacity * 2;
                    InventoryRecord* resized = (InventoryRecord*)realloc(records, new_capacity * sizeof(InventoryRecord));
                    if (resized == NULL) {
                        allocation_failed = 1;
                        keep_reading = 0;
                    }
                    else {
                        records = resized;
                        capacity = new_capacity;
                    }
                }

                if (!allocation_failed) {
                    size_t cursor = 0;
                    InventoryRecord* row = &records[*out_count];

                    /* Extract Strings */
                    get_next_token(line_buffer, &cursor, row->snapshot_date, sizeof(row->snapshot_date));
                    get_next_token(line_buffer, &cursor, row->sku_id, sizeof(row->sku_id));
                    get_next_token(line_buffer, &cursor, row->category, sizeof(row->category));
                    get_next_token(line_buffer, &cursor, row->warehouse, sizeof(row->warehouse));

                    /* Extract and Convert Integers */
                    get_next_token(line_buffer, &cursor, temp_buffer, sizeof(temp_buffer));
                    row->current_stock = atoi(temp_buffer);

                    get_next_token(line_buffer, &cursor, temp_buffer, sizeof(temp_buffer));
                    row->reorder_point = atoi(temp_buffer);

                    get_next_token(line_buffer, &cursor, temp_buffer, sizeof(temp_buffer));
                    row->reorder_qty = atoi(temp_buffer);

                    /* Extract and Convert Doubles */
                    get_next_token(line_buffer, &cursor, temp_buffer, sizeof(temp_buffer));
                    row->unit_cost = atof(temp_buffer);

                    get_next_token(line_buffer, &cursor, temp_buffer, sizeof(temp_buffer));
                    row->stock_value = atof(temp_buffer);

                    /* Final Integer */
                    get_next_token(line_buffer, &cursor, temp_buffer, sizeof(temp_buffer));
                    row->below_reorder = atoi(temp_buffer);

                    (*out_count)++;
                }
            }
        }
    }

    fclose(file);
    
    if (allocation_failed) {
        free(records);
        *out_count = 0;
        return NULL;
    }
    
    return records;
}

