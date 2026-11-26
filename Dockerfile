FROM node:20-bookworm-slim

# Install C++ build tools and PostgreSQL client library (libpq)
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    libpq-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy C++ source and build the ETL binary
COPY CMakeLists.txt main.cpp database.cpp database.h parser.cpp parser.h inventory-levels.csv ./
RUN cmake -B build && cmake --build build

# Install Node.js UI dependencies
COPY ui/package*.json ./ui/
RUN cd ui && npm install --omit=dev

# Copy the rest of the UI
COPY ui/ ./ui/

EXPOSE 3000
CMD ["node", "ui/server.js"]

