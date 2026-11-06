# C++ ETL Supply Chain Pipeline (warehouseDB)

A high-performance, memory-safe C++ Data Warehouse pipeline that extracts CSV inventory data, validates it in heap memory, and bulk-loads it into a local PostgreSQL Docker instance via `libpq`. 

## Architecture & Constraints
This project adheres to strict low-level memory and control flow constraints:
- **No Standard Template Library:** `std::vector` is prohibited. All dynamic arrays use `malloc` and `realloc`.
- **No Boolean Types:** Standard `int` flags (1 or 0) are used for all logic gates.
- **Restricted Control Flow:** The `break` and `continue` keywords are prohibited. Loop state is managed strictly via integer flags.
- **Star Schema:** Data lands in `staging_inventory` and is transformed into `dim_date`, `dim_product`, `dim_warehouse`, and `fact_inventory` via PostgreSQL stored procedures.

## 1. Prerequisites
Ensure the following dependencies are installed on your system:
- **Docker Desktop** (or Docker Engine) running in the background.
- **CMake** (Version 3.10+)
- **C++ Compiler** (C++17 standard support)
- **PostgreSQL C Library (`libpq`)**:
  - **macOS:** `brew install libpq`
  - **Arch Linux:** `sudo pacman -S postgresql-libs`
  - **Ubuntu/Debian:** `sudo apt-get install libpq-dev`
  - **Windows:** Use the official PostgreSQL installer or `vcpkg`.

## 2. Environment Setup
Clone the repository and boot the Docker container. The initialization script (`schema/01_init.sql`) will automatically execute to build the database tables.

```bash
git clone https://github.com/TIKYIKHANT-hyper/warehouseDB.git
cd warehouseDB

# Boot the PostgreSQL database
docker compose up -d
```

## 3. Build Instructions
Use CMake to generate the build files and compile the executable. 

**For Linux / Windows:**
```bash
cmake -B build
cmake --build build
```

**For macOS (Homebrew Users):**
Homebrew intentionally hides `libpq` to prevent global conflicts. You must explicitly pass the Homebrew path to CMake:
```bash
cmake -B build -DPostgreSQL_ROOT=$(brew --prefix libpq)
cmake --build build
```

## 4. Testing Cases & Expected Outcomes
Run the compiled pipeline using `./build/warehouseDB`. Verify the following test scenarios to ensure your local environment is functioning correctly.

### Test Case 1: Docker Database Binding
- **Action:** Run `docker compose up -d`.
- **Expected:** Container boots successfully. 
- **Troubleshooting:** If you receive a `bind: address already in use` error on port 5432, a local PostgreSQL process is blocking Docker. 
  - On macOS/Linux, find the PID with `sudo lsof -i :5432` and terminate it using `sudo kill -9 <PID>`, then retry.

### Test Case 2: C++ Heap Memory Allocation
- **Action:** Execute the `./build/warehouseDB` pipeline.
- **Expected:** The console outputs `[SUCCESS] Parsed 908 rows into dynamic heap memory.`
- **Troubleshooting:** If parsing fails, ensure `inventory-levels.csv` is located in the correct execution directory and is not locked by another application.

### Test Case 3: Data Validation & Rejection
- **Action:** Review the `LOAD REPORT` in the console output.
- **Expected:** The system must report `records from parser: 908`, `rejected (invalid): 1`, and `inserted: 907`. 
- **Validation:** Row 908 should be explicitly rejected by the C++ validation layer prior to server transmission due to an "invalid warehouse" flag.

### Test Case 4: Star Schema Transformation
- **Action:** Open pgAdmin (or your preferred database IDE) and connect to `127.0.0.1:5432` (User: `postgres`, DB: `MyfirstDW`).
- **Expected:** The `staging -> star schema` log should match the database row counts perfectly:
  - `dim_date`: 341 rows
  - `dim_product`: 284 rows
  - `dim_warehouse`: 5 rows
  - `fact_inventory`: 907 rows

## Resetting the Database Environment
If updates are made to the `schema/01_init.sql` file, you must destroy the old Docker volume so PostgreSQL is forced to rebuild the tables from scratch.

```bash
docker compose down -v
docker compose up -d
```
