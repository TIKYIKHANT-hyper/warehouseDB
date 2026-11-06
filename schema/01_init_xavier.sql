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

-- 4. ETL TRANSFORMATION PROCEDURE
-- This procedure safely moves data from the staging table into the Star Schema.
CREATE OR REPLACE PROCEDURE etl_transform_star_schema()
LANGUAGE plpgsql
AS $$
BEGIN
    -- 1. Populate dim_date
    INSERT INTO dim_date (date_key, full_date, year, quarter, month, month_name, day, day_of_week, day_name, is_weekend)
    SELECT DISTINCT 
        TO_CHAR(snapshot_date, 'YYYYMMDD')::int, 
        snapshot_date, 
        EXTRACT(YEAR FROM snapshot_date)::int, 
        EXTRACT(QUARTER FROM snapshot_date)::int, 
        EXTRACT(MONTH FROM snapshot_date)::int, 
        TRIM(TO_CHAR(snapshot_date, 'Month')), 
        EXTRACT(DAY FROM snapshot_date)::int, 
        EXTRACT(ISODOW FROM snapshot_date)::int, 
        TRIM(TO_CHAR(snapshot_date, 'Day')), 
        EXTRACT(ISODOW FROM snapshot_date) IN (6, 7)
    FROM staging_inventory 
    WHERE snapshot_date IS NOT NULL
    ON CONFLICT (date_key) DO NOTHING;

    -- 2. Populate dim_product
    INSERT INTO dim_product (sku_id, category)
    SELECT DISTINCT sku_id, category 
    FROM staging_inventory
    ON CONFLICT (sku_id) DO UPDATE SET category = EXCLUDED.category;

    -- 3. Populate dim_warehouse
    INSERT INTO dim_warehouse (warehouse_code, region)
    SELECT DISTINCT warehouse, REPLACE(warehouse, 'WH-', '') 
    FROM staging_inventory
    WHERE warehouse IS NOT NULL
    ON CONFLICT (warehouse_code) DO NOTHING;

    -- 4. Clear old fact table
    TRUNCATE TABLE fact_inventory;

    -- 5. Populate fact_inventory by joining staging with dimensions
    INSERT INTO fact_inventory (date_key, product_key, warehouse_key, current_stock, reorder_point, reorder_qty, unit_cost, stock_value, below_reorder)
    SELECT 
        TO_CHAR(s.snapshot_date, 'YYYYMMDD')::int,
        p.product_key,
        w.warehouse_key,
        s.current_stock,
        s.reorder_point,
        s.reorder_qty,
        s.unit_cost,
        s.stock_value,
        (s.below_reorder = 1)
    FROM staging_inventory s
    JOIN dim_product p ON p.sku_id = s.sku_id
    JOIN dim_warehouse w ON w.warehouse_code = s.warehouse;
END;
$$;

-- 4. ETL TRANSFORMATION PROCEDURE
-- This procedure safely moves data from the staging table into the Star Schema.
CREATE OR REPLACE PROCEDURE etl_transform_star_schema()
LANGUAGE plpgsql
AS $$
BEGIN
    -- 1. Populate dim_date
    INSERT INTO dim_date (date_key, full_date, year, quarter, month, month_name, day, day_of_week, day_name, is_weekend)
    SELECT DISTINCT 
        TO_CHAR(snapshot_date, 'YYYYMMDD')::int, 
        snapshot_date, 
        EXTRACT(YEAR FROM snapshot_date)::int, 
        EXTRACT(QUARTER FROM snapshot_date)::int, 
        EXTRACT(MONTH FROM snapshot_date)::int, 
        TRIM(TO_CHAR(snapshot_date, 'Month')), 
        EXTRACT(DAY FROM snapshot_date)::int, 
        EXTRACT(ISODOW FROM snapshot_date)::int, 
        TRIM(TO_CHAR(snapshot_date, 'Day')), 
        EXTRACT(ISODOW FROM snapshot_date) IN (6, 7)
    FROM staging_inventory 
    WHERE snapshot_date IS NOT NULL
    ON CONFLICT (date_key) DO NOTHING;

    -- 2. Populate dim_product
    INSERT INTO dim_product (sku_id, category)
    SELECT DISTINCT sku_id, category 
    FROM staging_inventory
    ON CONFLICT (sku_id) DO UPDATE SET category = EXCLUDED.category;

    -- 3. Populate dim_warehouse
    INSERT INTO dim_warehouse (warehouse_code, region)
    SELECT DISTINCT warehouse, REPLACE(warehouse, 'WH-', '') 
    FROM staging_inventory
    WHERE warehouse IS NOT NULL
    ON CONFLICT (warehouse_code) DO NOTHING;

    -- 4. Clear old fact table
    TRUNCATE TABLE fact_inventory;

    -- 5. Populate fact_inventory by joining staging with dimensions
    INSERT INTO fact_inventory (date_key, product_key, warehouse_key, current_stock, reorder_point, reorder_qty, unit_cost, stock_value, below_reorder)
    SELECT 
        TO_CHAR(s.snapshot_date, 'YYYYMMDD')::int,
        p.product_key,
        w.warehouse_key,
        s.current_stock,
        s.reorder_point,
        s.reorder_qty,
        s.unit_cost,
        s.stock_value,
        (s.below_reorder = 1)
    FROM staging_inventory s
    JOIN dim_product p ON p.sku_id = s.sku_id
    JOIN dim_warehouse w ON w.warehouse_code = s.warehouse;
END;
$$;

