# BondUp – Friendship Network

A full-stack social networking application built with a **C++ backend** and **vanilla JavaScript frontend**. Demonstrates real-world use of Data Structures and Algorithms.

## 🚀 Features

| Feature | DSA Used | Status |
|---------|----------|--------|
| User Authentication | Hash Table (unordered_map) | ✅ |
| Friend Suggestions | BFS (Breadth-First Search) | ✅ |
| Friend Requests | Graph (Adjacency List) | ✅ |
| Network Map | Force-Directed Graph | ✅ |
| Explore Groups | DFS (Depth-First Search) | ✅ |
| Activity Log | Stack (LIFO) | ✅ |
| Post Feed | Vector + Set | ✅ |
| Messaging | In-memory chat | ✅ |
| Birthday Manager | Sorted Map | ✅ |
| Events | CRUD + Attendance | ✅ |
| Photo Gallery | Aggregation | ✅ |
| Saved Posts | Bookmarking (Set) | ✅ |

## 📁 Project Structure

```
Friendship/
├── frontend.html      ← Main HTML (SPA)
├── app.js             ← Client-side JavaScript
├── style.css          ← Premium dark theme CSS
├── backend/
│   ├── main.cpp       ← C++ HTTP server
│   ├── httplib.h      ← cpp-httplib (header-only)
│   ├── json.hpp       ← nlohmann/json (header-only)
│   └── build.bat      ← Windows build script
├── Dockerfile         ← Multi-stage Docker build
├── railway.json       ← Railway deployment config
└── README.md          ← This file
```

## 🛠️ Local Setup

### Prerequisites
- **Windows**: Visual Studio (MSVC) or MinGW (g++)
- **Linux/Mac**: g++ with C++17 support

### Build & Run

**Windows (MinGW):**
```bash
cd backend
g++ -std=c++17 -O2 main.cpp -lws2_32 -o server.exe
server.exe
```

**Windows (MSVC – Developer Command Prompt):**
```bash
cd backend
cl /std:c++17 /EHsc /O2 /Fe:server.exe main.cpp ws2_32.lib
server.exe
```

**Linux/Mac:**
```bash
cd backend
g++ -std=c++17 -O2 main.cpp -lpthread -o server
./server
```

Then open **http://localhost:8080/** in your browser.

## 🔑 Default Accounts

| Username | Password | Name |
|----------|----------|------|
| ronak_gupta | password123 | Ronak Gupta |
| amit_shukla | password123 | Amit Shukla |
| mansi_gupta | password123 | Mansi Gupta |
| prateek_singh | password123 | Prateek Singh |
| riya_sharma | password123 | Riya Sharma |
| karan_mehta | password123 | Karan Mehta |

All accounts use password: `password123`

## 🚂 Railway Deployment

1. Push this folder to a GitHub repository
2. Go to [railway.app](https://railway.app/) and create a new project
3. Select "Deploy from GitHub repo"
4. Railway will detect the `Dockerfile` and `railway.json` automatically
5. The app will be live at your Railway URL

## 📊 Data Structures Used

- **Hash Table** (`unordered_map`) — O(1) user/session lookups
- **Graph** (Adjacency List) — Friendship network
- **BFS** — Friend suggestions & shortest path
- **DFS** — Connected component discovery (groups)
- **Stack** — Activity log (LIFO order)
- **Set** — Friend deduplication, like tracking
- **Vector** — Ordered collections (posts, events)
- **Map** — Sorted birthday storage

## 🎨 Design

- Dark theme with glassmorphism
- Gradient accents and micro-animations
- Fully responsive layout
- Canvas-based network visualization
- Premium look and feel

## 📝 License

MIT License — Built for MITS-DU DSA Project
