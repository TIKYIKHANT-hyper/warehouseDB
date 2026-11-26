# warehouseDB — Supply Chain Inventory Data Warehouse & Analytics Dashboard

A full-stack data warehousing and supply chain analytics application built with a **C++ (`libpq`) ETL pipeline**, a **PostgreSQL Star Schema**, and a **Node.js / Express / Chart.js** web dashboard—fully containerized with Docker.

---

## Features

- **C++ ETL Pipeline:** Parses supply chain CSV datasets, validates rows, bulk-inserts into `staging_inventory`, and idempotently populates Star Schema dimensions (`dim_date`, `dim_product`, `dim_warehouse`) and fact tables (`fact_inventory`).
- **Dynamic CSV Upload:** Upload any compatible 10-column CSV directly from the browser to trigger the C++ ETL binary and refresh the warehouse on the fly.
- **Interactive Analytics Dashboard:**
  - Live KPI cards (Fact Records, Unique Products, Warehouses, Total Stock Value, Below Reorder Alerts).
  - Stock Value by Warehouse bar chart (Chart.js).
  - Multi-column filtering and sorting by SKU, Category, Warehouse, Date range, Stock range, Reorder Point, Stock Value, and Reorder status.
  - Live terminal log output from the C++ ETL execution directly in the UI.
- **Production-Ready Infrastructure:** Environment variable management via `.env` and one-command deployment using Docker Compose.

---

## Project Structure

```text
warehouseDB/
├── CMakeLists.txt          # CMake build configuration for the C++ ETL binary
├── main.cpp                # ETL entry point (accepts optional CSV path argument)
├── parser.cpp / parser.h   # CSV extraction and heap-allocated record parser
├── database.cpp / .h       # libpq connection, bulk staging load, and Star Schema transform
├── schema/                 # PostgreSQL DDL scripts (auto-loaded on container init)
├── datasets/               # Additional supply chain datasets
├── inventory-levels.csv    # Default CSV dataset
├── ui/
│   ├── server.js           # Express API server, multer CSV upload, and C++ process runner
│   ├── package.json        # Node.js dependencies
│   └── public/
│       └── index.html      # Dashboard UI (HTML/CSS/JS + Chart.js)
├── Dockerfile              # Multi-runtime container (C++ build tools + Node.js)
├── docker-compose.yml      # Orchestrates PostgreSQL (db) and Dashboard/ETL (app)
├── .env.example            # Template for environment variables
└── .dockerignore
```

---

## Quick Start (Docker — Recommended)

1. **Clone the repository:**
   ```bash
   git clone https://github.com/TIKYIKHANT-hyper/warehouseDB.git
   cd warehouseDB
   ```

2. **Configure environment variables:**
   Copy the example file and set your PostgreSQL password:
   ```bash
   cp .env.example .env
   ```
   Edit `.env`:
   ```env
   DB_HOST=127.0.0.1
   DB_PORT=5432
   DB_NAME=MyfirstDW
   DB_USER=postgres
   DB_PASSWORD=your_secure_password
   ```

3. **Build and start the stack:**
   ```bash
   docker compose up --build -d
   ```

4. **Open the dashboard:**
   Navigate to **`http://localhost:3000`** in your browser and click **▶ Run Default ETL** (or upload a custom CSV) to populate the warehouse.

To view application logs:
```bash
docker compose logs -f app
```

---

## Local Development (Manual Build)

If you prefer running the C++ binary and Node.js server natively while keeping PostgreSQL in Docker:

1. **Start PostgreSQL only:**
   ```bash
   docker compose up -d db
   ```

2. **Compile the C++ ETL pipeline:**
   ```bash
   cmake -B build
   cmake --build build
   ```
   > **macOS note:** If CMake cannot locate `libpq`, specify your PostgreSQL installation path:
   > ```bash
   > cmake -B build -DPostgreSQL_ROOT=/Library/PostgreSQL/18
   > cmake --build build
   > ```

3. **Run the C++ ETL from the terminal (optional):**
   ```bash
   ./build/warehouseDB                        # Uses inventory-levels.csv
   ./build/warehouseDB path/to/custom.csv     # Uses a custom CSV file
   ```

4. **Start the Web UI:**
   ```bash
   cd ui
   npm install
   node server.js
   ```
   Open **`http://localhost:3000`**.

---

## API Endpoints

| Method | Endpoint | Description |
| :--- | :--- | :--- |
| `GET` | `/api/summary` | Returns KPI totals (records, products, warehouses, stock value, reorder alerts). |
| `GET` | `/api/warehouses` | Returns aggregated stock value and units grouped by warehouse. |
| `GET` | `/api/filters` | Returns distinct categories and warehouse codes for UI dropdowns. |
| `GET` | `/api/inventory` | Returns filtered and sorted joined Star Schema records (`sku`, `category`, `warehouse`, `dateFrom`, `dateTo`, `minStock`, `maxStock`, `minReorder`, `minValue`, `alertsOnly`, `sortBy`, `sortDir`). |
| `POST` | `/api/run-etl` | Executes `./build/warehouseDB` on the default `inventory-levels.csv` and returns stdout/stderr. |
| `POST` | `/api/upload-etl` | Accepts a multipart `csvFile` upload, runs `./build/warehouseDB <file>`, and cleans up the temp file. |
