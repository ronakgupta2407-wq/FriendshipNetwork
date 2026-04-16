# ============================================================
# BondUp – Friendship Network  |  Dockerfile (Railway)
# OPTIMIZED: gcc base (no apt-get needed) + alpine runtime
# ============================================================

# Stage 1: Build (gcc image already has g++ — no install needed!)
FROM gcc:12 AS builder

WORKDIR /build

# Copy ONLY the backend source files needed for compilation
COPY backend/main.cpp ./
COPY backend/json.hpp ./

# Compile (Linux uses BSD sockets + pthreads)
RUN g++ -std=c++14 -O2 -o server main.cpp -lpthread

# Stage 2: Lightweight runtime (alpine = ~5MB base)
FROM debian:bookworm-slim

WORKDIR /app

# Copy compiled binary from builder
COPY --from=builder /build/server ./backend/server

# Copy frontend files
COPY frontend.html ./
COPY style.css ./
COPY app.js ./

# Make server executable
RUN chmod +x backend/server

# Railway sets PORT automatically
ENV PORT=8080
EXPOSE 8080

# Run from backend dir so "../" resolves to /app for static files
WORKDIR /app/backend
CMD ["./server"]
