#ifndef PARSER_H
#define PARSER_H

struct InventoryRecord {
    char snapshot_date[15];
    char sku_id[20];
    char category[20];
    char warehouse[30];
    int current_stock;
    int reorder_point;
    int reorder_qty;
    double unit_cost;
    double stock_value;
    int below_reorder;
};

// Returns a dynamically allocated array of records and outputs the total count
InventoryRecord* parse_csv(const char* filename, int* out_count);

#endif

