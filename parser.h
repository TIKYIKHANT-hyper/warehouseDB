#ifndef PARSER_H
#define PARSER_H

/*
 * parser.h - CSV Extraction Layer
 * Pipeline position: File I/O -> Parser -> main.cpp -> database.cpp
 */

struct InventoryRecord {
    char snapshot_date[16];
    char sku_id[64];
    char category[64];
    char warehouse[64];
    int current_stock;
    int reorder_point;
    int reorder_qty;
    double unit_cost;
    double stock_value;
    int below_reorder;
};

/* Returns a dynamically allocated array of records. out_count is updated with the total parsed. */
InventoryRecord* parse_csv(const char* filepath, int* out_count);

#endif

