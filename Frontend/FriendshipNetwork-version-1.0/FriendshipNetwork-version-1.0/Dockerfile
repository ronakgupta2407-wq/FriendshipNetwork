# ─────────────────────────────────────────────────
# BondUp – C++ Backend  |  Dockerfile (Render / Railway)
# ─────────────────────────────────────────────────
FROM ubuntu:22.04 AS builder

RUN apt-get update && apt-get install -y g++ && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY backend/httplib.h backend/json.hpp backend/main.cpp ./backend/
RUN cd backend && g++ -std=c++17 -O2 -o server main.cpp -lpthread

# ── Runtime stage ──
FROM ubuntu:22.04
RUN apt-get update && apt-get install -y libstdc++6 && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY --from=builder /app/backend/server ./backend/server
COPY frontend.html app.js style.css ./

# The server serves static files from "../" relative to backend/
WORKDIR /app/backend
EXPOSE 8080

CMD ["./server"]
