# ============================================================
# BondUp – Friendship Network  |  Dockerfile (Railway)
# Multi-stage build: compile C++ backend → lightweight runtime
# ============================================================

# Stage 1: Build
FROM ubuntu:22.04 AS builder

RUN apt-get update && apt-get install -y \
    g++ \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy backend source and json header
COPY backend/main.cpp backend/json.hpp ./backend/

# Compile (Linux uses BSD sockets, no winsock needed)
RUN cd backend && g++ -std=c++14 -O2 main.cpp -lpthread -o server

# Stage 2: Runtime
FROM ubuntu:22.04

RUN apt-get update && apt-get install -y \
    libstdc++6 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy compiled binary
COPY --from=builder /app/backend/server ./backend/server

# Copy frontend files
COPY frontend.html style.css app.js ./

# Make server executable
RUN chmod +x backend/server

# Railway sets PORT env var automatically
ENV PORT=8080

EXPOSE 8080

# Start from backend directory so static file paths "../" resolve to /app
WORKDIR /app/backend

CMD ["./server"]
