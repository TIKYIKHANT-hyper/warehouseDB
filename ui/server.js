const express = require('express');
const { Pool } = require('pg');
const path = require('path');

const app = express();
const PORT = 3000;

// Matches your Docker PostgreSQL configuration
const pool = new Pool({
  host: '127.0.0.1',
  port: 5432,
  database: 'MyfirstDW',
  user: 'postgres',
  password: 'your_password', // Update if you changed it
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

// 3. Joined Star Schema Table (supports ?alertsOnly=true)
app.get('/api/inventory', async (req, res) => {
  const alertsOnly = req.query.alertsOnly === 'true';
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
      ${alertsOnly ? 'WHERE f.below_reorder = true' : ''}
      ORDER BY d.full_date DESC, f.stock_value DESC
      LIMIT 100;
    `;
    const result = await pool.query(query);
    res.json(result.rows);
  } catch (err) {
    res.status(500).json({ error: err.message });
  }
});

app.listen(PORT, () => {
  console.log(`Dashboard running at http://localhost:${PORT}`);
});

