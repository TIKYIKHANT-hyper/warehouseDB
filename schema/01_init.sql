-- 1. STAGING TABLE (The Landing Zone)
-- This receives the raw bulk inserts from C++
CREATE TABLE staging_inventory (
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

-- 2. STAR SCHEMA DIMENSIONS
-- These tables hold descriptive attributes. 
-- UNIQUE constraints are required for the C++ "ON CONFLICT DO NOTHING" logic.
CREATE TABLE dim_date (
    date_key INT PRIMARY KEY,
    full_date DATE UNIQUE,
    year INT,
    quarter INT,
    month INT,
    month_name VARCHAR(20),
    day INT,
    day_of_week INT,
    day_name VARCHAR(20),
    is_weekend BOOLEAN
);

CREATE TABLE dim_product (
    product_key SERIAL PRIMARY KEY,
    sku_id VARCHAR(50) UNIQUE,
    category VARCHAR(50)
);

CREATE TABLE dim_warehouse (
    warehouse_key SERIAL PRIMARY KEY,
    warehouse_code VARCHAR(50) UNIQUE,
    region VARCHAR(50)
);

-- 3. STAR SCHEMA FACT TABLE
-- The central quantitative table linked via Foreign Keys.
CREATE TABLE fact_inventory (
    fact_id SERIAL PRIMARY KEY,
    date_key INT REFERENCES dim_date(date_key),
    product_key INT REFERENCES dim_product(product_key),
    warehouse_key INT REFERENCES dim_warehouse(warehouse_key),
    current_stock INT,
    reorder_point INT,
    reorder_qty INT,
    unit_cost NUMERIC(10, 2),
    stock_value NUMERIC(15, 2),
    below_reorder BOOLEAN
);

