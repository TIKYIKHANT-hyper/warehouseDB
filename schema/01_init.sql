CREATE TABLE staging_inventory (
    record_id SERIAL PRIMARY KEY,
    snapshot_date DATE,
    sku_id VARCHAR(50),
    category VARCHAR(50),
    warehouse VARCHAR(50),
    current_stock INT,
    reorder_point INT,
    reorder_qty INT,
    unit_cost NUMERIC(10, 2),
    stock_value NUMERIC(15, 2),
    below_reorder INT
);

