cmake_minimum_required(VERSION 3.10)
project(warehouseDB)

# Enforce standard C++
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Locate the PostgreSQL libpq library on your system
find_package(PostgreSQL REQUIRED)

# Define the executable and all of its source files
add_executable(warehouseDB 
    main.cpp 
    parser.cpp 
    database.cpp
)

# Tell the compiler where the libpq header files (like libpq-fe.h) are located
target_include_directories(warehouseDB PRIVATE ${PostgreSQL_INCLUDE_DIRS})

# Link the actual libpq binary to your executable
target_link_libraries(warehouseDB PRIVATE ${PostgreSQL_LIBRARIES})

