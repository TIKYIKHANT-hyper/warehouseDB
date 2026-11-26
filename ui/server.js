const path = require('path');
require('dotenv').config({ path: path.join(__dirname, '..', '.env') });

const { execFile } = require('child_process');
const express = require('express');
const { Pool } = require('pg');
const multer = require('multer');
const fs = require('fs');

const app = express();
const PORT = process.env.PORT || 3000;
const upload = multer({ dest: path.join(__dirname, 'uploads') });

const pool = new Pool({
  host: process.env.DB_HOST || '127.0.0.1',
  port: Number(process.env.DB_PORT) || 5432,
  database: process.env.DB_NAME || 'MyfirstDW',
  user: process.env.DB_USER || 'postgres',
  password: process.env.DB_PASSWORD,
});

app.use(express.static(path.join(__dirname, 'public')));

// 1. KPI Summary
app.get('/api/summary', async (req, res) => {
  try {
    const result = await pool.query(`
      SELECT
        COUNT(*) AS total_records,
        COUNT(DISTINCT product_key) AS total_products,
        COUNT(DISTINCT warehouse_key) AS total_warehouses,
        ROUND(SUM(stock_value), 2) AS total_stock_value,
        SUM(CASE WHEN below_reorder THEN 1 ELSE 0 END) AS below_reorder_count
      FROM fact_inventory;
    `);
    res.json(result.rows[0]);
  } catch (err) {
    res.status(500).json({ error: err.message });
  }
});

// 2. Stock Value by Warehouse (for the chart)
app.get('/api/warehouses', async (req, res) => {
  try {
    const result = await pool.query(`
      SELECT
        w.warehouse_code,
        ROUND(SUM(f.stock_value), 2) AS total_value,
        SUM(f.current_stock) AS total_units
      FROM fact_inventory f
      JOIN dim_warehouse w ON f.warehouse_key = w.warehouse_key
      GROUP BY w.warehouse_code
      ORDER BY total_value DESC;
    `);
    res.json(result.rows);
  } catch (err) {
    res.status(500).json({ error: err.message });
  }
});

// 3a. Dynamic Filter Options (Categories & Warehouses)
app.get('/api/filters', async (req, res) => {
  try {
    const [categories, warehouses] = await Promise.all([
      pool.query('SELECT DISTINCT category FROM dim_product WHERE category IS NOT NULL ORDER BY category'),
      pool.query('SELECT DISTINCT warehouse_code FROM dim_warehouse WHERE warehouse_code IS NOT NULL ORDER BY warehouse_code'),
    ]);
    res.json({
      categories: categories.rows.map(r => r.category),
      warehouses: warehouses.rows.map(r => r.warehouse_code),
    });
  } catch (err) {
    res.status(500).json({ error: err.message });
  }
});

// 3b. Joined Star Schema Table with Multi-Column Filtering & Sorting
app.get('/api/inventory', async (req, res) => {
  const {
    alertsOnly, sku, category, warehouse,
    dateFrom, dateTo, minStock, maxStock,
    minReorder, minValue, sortBy, sortDir
  } = req.query;

  const conditions = [];
  const params = [];

  if (alertsOnly === 'true') {
    conditions.push('f.below_reorder = true');
  }
  if (sku) {
    params.push(`%${sku.trim()}%`);
    conditions.push(`p.sku_id ILIKE $${params.length}`);
  }
  if (category) {
    params.push(category);
    conditions.push(`p.category = $${params.length}`);
  }
  if (warehouse) {
    params.push(warehouse);
    conditions.push(`w.warehouse_code = $${params.length}`);
  }
  if (dateFrom) {
    params.push(dateFrom);
    conditions.push(`d.full_date >= $${params.length}`);
  }
  if (dateTo) {
    params.push(dateTo);
    conditions.push(`d.full_date <= $${params.length}`);
  }
  if (minStock !== undefined && minStock !== '') {
    params.push(Number(minStock));
    conditions.push(`f.current_stock >= $${params.length}`);
  }
  if (maxStock !== undefined && maxStock !== '') {
    params.push(Number(maxStock));
    conditions.push(`f.current_stock <= $${params.length}`);
  }
  if (minReorder !== undefined && minReorder !== '') {
    params.push(Number(minReorder));
    conditions.push(`f.reorder_point >= $${params.length}`);
  }
  if (minValue !== undefined && minValue !== '') {
    params.push(Number(minValue));
    conditions.push(`f.stock_value >= $${params.length}`);
  }

  const sortColumns = {
    date: 'd.full_date',
    sku: 'p.sku_id',
    stock: 'f.current_stock',
    reorder: 'f.reorder_point',
    value: 'f.stock_value',
  };
  const orderCol = sortColumns[sortBy] || 'd.full_date';
  const orderDir = sortDir === 'asc' ? 'ASC' : 'DESC';
  const whereClause = conditions.length ? `WHERE ${conditions.join(' AND ')}` : '';

  try {
    const query = `
      SELECT
        TO_CHAR(d.full_date, 'YYYY-MM-DD') AS date,
        p.sku_id,
        p.category,
        w.warehouse_code,
        f.current_stock,
        f.reorder_point,
        f.stock_value,
        f.below_reorder
      FROM fact_inventory f
      JOIN dim_date d ON f.date_key = d.date_key
      JOIN dim_product p ON f.product_key = p.product_key
      JOIN dim_warehouse w ON f.warehouse_key = w.warehouse_key
      ${whereClause}
      ORDER BY ${orderCol} ${orderDir}, f.stock_value DESC
      LIMIT 200;
    `;
    const result = await pool.query(query, params);
    res.json(result.rows);
  } catch (err) {
    res.status(500).json({ error: err.message });
  }
});

// 4. Trigger Default C++ ETL Pipeline from the UI
app.post('/api/run-etl', (req, res) => {
  const projectRoot = path.join(__dirname, '..');
  const executable = path.join(projectRoot, 'build', 'warehouseDB');

  execFile(executable, { cwd: projectRoot }, (err, stdout, stderr) => {
    if (err) {
      return res.status(500).json({
        success: false,
        output: stderr || err.message,
      });
    }
    res.json({
      success: true,
      output: stdout + (stderr ? `\n${stderr}` : ''),
    });
  });
});

// 5. Upload CSV and Trigger C++ ETL Pipeline
app.post('/api/upload-etl', upload.single('csvFile'), (req, res) => {
  if (!req.file) {
    return res.status(400).json({ success: false, output: 'No CSV file uploaded.' });
  }

  const projectRoot = path.join(__dirname, '..');
  const executable = path.join(projectRoot, 'build', 'warehouseDB');
  const uploadedCsvPath = req.file.path;

  execFile(executable, [uploadedCsvPath], { cwd: projectRoot }, (err, stdout, stderr) => {
    fs.unlink(uploadedCsvPath, () => {});

    if (err) {
      return res.status(500).json({
        success: false,
        output: stderr || err.message,
      });
    }
    res.json({
      success: true,
      output: stdout + (stderr ? `\n${stderr}` : ''),
    });
  });
});

app.listen(PORT, () => {
  console.log(`Dashboard running at http://localhost:${PORT}`);
});

