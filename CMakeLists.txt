cmake_minimum_required(VERSION 3.14)
project(DataWarehouseETL LANGUAGES CXX)

# Set standard C++ version
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# macOS: Homebrew installs libpq as "keg-only" to avoid conflicts with system Postgres.
# We must explicitly add Homebrew paths to the CMake prefix path so find_package can locate it.
if(APPLE)
    list(APPEND CMAKE_PREFIX_PATH 
        "/opt/homebrew/opt/libpq"   # Apple Silicon
        "/usr/local/opt/libpq"      # Intel Mac
    )
endif()

# Locate the PostgreSQL C library (libpq)
find_package(PostgreSQL REQUIRED)

# Define the executable
add_executable(etl_pipeline main.cpp parser.cpp database.cpp)

# Link libpq and include its headers using modern CMake target linking
if(TARGET PostgreSQL::PostgreSQL)
    target_link_libraries(etl_pipeline PRIVATE PostgreSQL::PostgreSQL)
else()
    # Fallback for older CMake versions
    target_include_directories(etl_pipeline PRIVATE ${PostgreSQL_INCLUDE_DIRS})
    target_link_libraries(etl_pipeline PRIVATE ${PostgreSQL_LIBRARIES})
endif()

