/* ============================================================
   BondUp – Friendship Network  |  C++ Backend Server
   File: main.cpp
   
   Uses simple embedded HTTP server (raw WinSock2 / BSD sockets)
   No external dependencies for HTTP - fully self-contained!

   Compile (g++ MinGW):
     g++ -std=c++14 -O2 main.cpp -lws2_32 -o server.exe

   Compile (g++ Linux):
     g++ -std=c++14 -O2 main.cpp -lpthread -o server

   Run:
     server.exe   (or ./server on Linux)
     Open http://localhost:8080/ in your browser.

   Data Structures Used:
     - std::unordered_map  – O(1) user/session lookups (Hash Table)
     - std::vector         – ordered collections (posts, comments, events)
     - Adjacency List      – friendship graph (undirected)
     - std::set            – friend-request dedup, like tracking, saved posts
     - std::queue          – BFS for friend suggestions & shortest path
     - std::stack          – Activity log (LIFO)
     - DFS (iterative)     – Connected component discovery
   ============================================================ */

#ifdef _WIN32
  #ifndef _WIN32_WINNT
    #define _WIN32_WINNT 0x0501
  #endif
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
  typedef int socklen_t;
  #define CLOSESOCKET closesocket
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  #define CLOSESOCKET close
  typedef int SOCKET;
  #define INVALID_SOCKET (-1)
#endif

#include "json.hpp"

#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <map>
#include <set>
#include <queue>
#include <stack>
#include <algorithm>
#include <chrono>
#include <ctime>
#include <sstream>
#include <random>
#include <functional>
#include <iomanip>
#include <fstream>
#include <cstdlib>
#include <cstring>

using json = nlohmann::json;

/* ════════════════════════════════════════════════════════════
   SIMPLE HTTP SERVER (embedded, no external library)
════════════════════════════════════════════════════════════ */
struct HttpRequest {
    std::string method;
    std::string path;
    std::string body;
    std::unordered_map<std::string, std::string> headers;
    std::vector<std::string> path_parts; // for regex-like matching
};

struct HttpResponse {
    int status = 200;
    std::string content_type = "application/json";
    std::string body;
    std::unordered_map<std::string, std::string> headers;
    
    void set_content(const std::string& b, const std::string& ct) {
        body = b; content_type = ct;
    }
};

typedef std::function<void(const HttpRequest&, HttpResponse&)> RouteHandler;

struct Route {
    std::string method;
    std::string pattern;  // e.g. "/api/posts" or "/api/posts/{id}"
    RouteHandler handler;
    bool has_param;       // does path have {id} parameter?
};

class SimpleHttpServer {
public:
    void Get(const std::string& path, RouteHandler h) { routes_.push_back({"GET", path, h, path.find("{") != std::string::npos}); }
    void Post(const std::string& path, RouteHandler h) { routes_.push_back({"POST", path, h, path.find("{") != std::string::npos}); }
    void Delete(const std::string& path, RouteHandler h) { routes_.push_back({"DELETE", path, h, path.find("{") != std::string::npos}); }

    bool listen(const std::string& host, int port) {
        #ifdef _WIN32
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) { std::cerr << "WSAStartup failed\n"; return false; }
        #endif

        SOCKET server_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd == INVALID_SOCKET) { std::cerr << "Socket creation failed\n"; return false; }

        int opt = 1;
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port);

        if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            std::cerr << "Bind failed on port " << port << "\n";
            CLOSESOCKET(server_fd);
            return false;
        }

        if (::listen(server_fd, 10) < 0) {
            std::cerr << "Listen failed\n";
            CLOSESOCKET(server_fd);
            return false;
        }

        std::cout << "  Listening on http://localhost:" << port << "/\n";

        while (true) {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            SOCKET client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
            if (client_fd == INVALID_SOCKET) continue;
            handleClient(client_fd);
        }

        CLOSESOCKET(server_fd);
        #ifdef _WIN32
        WSACleanup();
        #endif
        return true;
    }

private:
    std::vector<Route> routes_;

    void handleClient(SOCKET fd) {
        // Read request
        std::string raw;
        char buf[8192];
        // Read headers first
        while (true) {
            int n = recv(fd, buf, sizeof(buf)-1, 0);
            if (n <= 0) break;
            buf[n] = '\0';
            raw += buf;
            if (raw.find("\r\n\r\n") != std::string::npos) break;
        }

        if (raw.empty()) { CLOSESOCKET(fd); return; }

        // Parse request line
        HttpRequest req;
        size_t line_end = raw.find("\r\n");
        if (line_end == std::string::npos) { CLOSESOCKET(fd); return; }
        std::string request_line = raw.substr(0, line_end);
        
        size_t sp1 = request_line.find(' ');
        size_t sp2 = request_line.find(' ', sp1+1);
        if (sp1 == std::string::npos || sp2 == std::string::npos) { CLOSESOCKET(fd); return; }
        
        req.method = request_line.substr(0, sp1);
        req.path = request_line.substr(sp1+1, sp2-sp1-1);

        // Strip query string
        size_t qpos = req.path.find('?');
        if (qpos != std::string::npos) req.path = req.path.substr(0, qpos);

        // Parse headers
        size_t hdr_start = line_end + 2;
        size_t hdr_end = raw.find("\r\n\r\n");
        if (hdr_end != std::string::npos) {
            std::string headers_str = raw.substr(hdr_start, hdr_end - hdr_start);
            std::istringstream hss(headers_str);
            std::string hline;
            while (std::getline(hss, hline)) {
                if (!hline.empty() && hline.back() == '\r') hline.pop_back();
                size_t colon = hline.find(':');
                if (colon != std::string::npos) {
                    std::string key = hline.substr(0, colon);
                    std::string val = hline.substr(colon+1);
                    while (!val.empty() && val[0] == ' ') val.erase(0, 1);
                    // Lowercase key for matching
                    std::string lkey = key;
                    std::transform(lkey.begin(), lkey.end(), lkey.begin(), ::tolower);
                    req.headers[lkey] = val;
                }
            }
        }

        // Read body if Content-Length present
        if (req.headers.count("content-length")) {
            int content_len = std::stoi(req.headers["content-length"]);
            size_t body_start = hdr_end + 4;
            req.body = raw.substr(body_start);
            // Read remaining body data
            while ((int)req.body.size() < content_len) {
                int n = recv(fd, buf, std::min((int)sizeof(buf)-1, content_len - (int)req.body.size()), 0);
                if (n <= 0) break;
                buf[n] = '\0';
                req.body += std::string(buf, n);
            }
        }

        // Handle OPTIONS (CORS preflight)
        if (req.method == "OPTIONS") {
            HttpResponse res;
            res.status = 204;
            sendCorsResponse(fd, res);
            CLOSESOCKET(fd);
            return;
        }

        // Serve static files for non-API routes
        if (req.path == "/" || req.path == "/index.html") {
            serveFile(fd, "../frontend.html", "text/html");
            return;
        }
        if (req.path == "/style.css") {
            serveFile(fd, "../style.css", "text/css");
            return;
        }
        if (req.path == "/app.js") {
            serveFile(fd, "../app.js", "application/javascript");
            return;
        }

        // Route matching
        HttpResponse res;
        bool matched = false;

        for (auto& route : routes_) {
            if (route.method != req.method) continue;
            
            if (route.has_param) {
                // Pattern matching with {id} parameter
                std::string pattern = route.pattern;
                // Split pattern and path
                auto patParts = split(pattern, '/');
                auto reqParts = split(req.path, '/');
                
                if (patParts.size() != reqParts.size()) continue;
                
                bool match = true;
                req.path_parts.clear();
                for (size_t i = 0; i < patParts.size(); i++) {
                    if (patParts[i].size() >= 2 && patParts[i][0] == '{' && patParts[i].back() == '}') {
                        req.path_parts.push_back(reqParts[i]); // capture param
                    } else if (patParts[i] != reqParts[i]) {
                        match = false; break;
                    }
                }
                if (!match) continue;
                
                route.handler(req, res);
                matched = true;
                break;
            } else {
                if (route.pattern == req.path) {
                    route.handler(req, res);
                    matched = true;
                    break;
                }
            }
        }

        if (!matched) {
            res.status = 404;
            res.set_content("{\"error\":\"Not found\"}", "application/json");
        }

        sendCorsResponse(fd, res);
        CLOSESOCKET(fd);
    }

    void sendCorsResponse(SOCKET fd, HttpResponse& res) {
        std::ostringstream oss;
        oss << "HTTP/1.1 " << res.status << " " << statusText(res.status) << "\r\n";
        oss << "Content-Type: " << res.content_type << "\r\n";
        oss << "Content-Length: " << res.body.size() << "\r\n";
        oss << "Access-Control-Allow-Origin: *\r\n";
        oss << "Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS\r\n";
        oss << "Access-Control-Allow-Headers: Content-Type, Authorization\r\n";
        oss << "Connection: close\r\n";
        oss << "\r\n";
        oss << res.body;
        std::string response = oss.str();
        send(fd, response.c_str(), (int)response.size(), 0);
    }

    void serveFile(SOCKET fd, const std::string& filepath, const std::string& content_type) {
        std::ifstream f(filepath, std::ios::binary);
        if (!f) {
            std::string resp = "HTTP/1.1 404 Not Found\r\nContent-Length: 9\r\nConnection: close\r\n\r\nNot Found";
            send(fd, resp.c_str(), (int)resp.size(), 0);
            CLOSESOCKET(fd);
            return;
        }
        std::string body((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        std::ostringstream oss;
        oss << "HTTP/1.1 200 OK\r\n";
        oss << "Content-Type: " << content_type << "; charset=utf-8\r\n";
        oss << "Content-Length: " << body.size() << "\r\n";
        oss << "Connection: close\r\n";
        oss << "\r\n";
        oss << body;
        std::string response = oss.str();
        send(fd, response.c_str(), (int)response.size(), 0);
        CLOSESOCKET(fd);
    }

    static std::vector<std::string> split(const std::string& s, char delim) {
        std::vector<std::string> parts;
        std::istringstream iss(s);
        std::string part;
        while (std::getline(iss, part, delim)) {
            if (!part.empty()) parts.push_back(part);
        }
        return parts;
    }

    static std::string statusText(int code) {
        switch (code) {
            case 200: return "OK";
            case 201: return "Created";
            case 204: return "No Content";
            case 400: return "Bad Request";
            case 401: return "Unauthorized";
            case 404: return "Not Found";
            case 409: return "Conflict";
            case 500: return "Internal Server Error";
            default: return "OK";
        }
    }
};

/* ════════════════════════════════════════════════════════════
   UTILITY FUNCTIONS
════════════════════════════════════════════════════════════ */
static std::string now_iso() {
    auto now = std::chrono::system_clock::now();
    auto t   = std::chrono::system_clock::to_time_t(now);
    std::tm* tm_ptr = localtime(&t);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", tm_ptr);
    return std::string(buf);
}

static std::string make_token() {
    static std::mt19937 rng(static_cast<unsigned>(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    static const char chars[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    std::string tok;
    tok.reserve(32);
    for (int i = 0; i < 32; ++i)
        tok += chars[rng() % (sizeof(chars) - 1)];
    return tok;
}

/* ════════════════════════════════════════════════════════════
   MODEL STRUCTS
════════════════════════════════════════════════════════════ */
struct User {
    int id; std::string username, password, display_name, avatar_emoji, email;
};
struct FriendRequest {
    int id, from_user_id, to_user_id; std::string status;
};
struct Comment {
    int id, post_id, author_id; std::string author_name, text, created_at;
};
struct Post {
    int id, author_id; std::string author_name, text, mood, type;
    std::vector<std::string> images; std::set<int> liked_by;
    std::vector<Comment> comments; std::string created_at;
};
struct Birthday {
    int id, owner_id; std::string friend_name, birthday, emoji;
};
struct Event {
    int id, creator_id; std::string creator_name, title, description, date, time, location, emoji;
    std::set<int> attendees; std::string created_at;
};
struct ActivityEntry {
    int id; std::string action, message, timestamp; int user_id;
};

/* ════════════════════════════════════════════════════════════
   AUTH MANAGER (Hash Table — unordered_map O(1) lookup)
════════════════════════════════════════════════════════════ */
class AuthManager {
public:
    AuthManager() {
        addUser("ronak_gupta","password123","Ronak Gupta","\xF0\x9F\x98\x8A","ronak@mits.ac.in");
        addUser("amit_shukla","password123","Amit Shukla","\xF0\x9F\x98\x8E","amit@mits.ac.in");
        addUser("mansi_gupta","password123","Mansi Gupta","\xF0\x9F\x98\x8A","mansi@mits.ac.in");
        addUser("prateek_singh","password123","Prateek Singh","\xF0\x9F\xA7\x91\xE2\x80\x8D\xF0\x9F\x8E\x93","prateek@mits.ac.in");
        addUser("riya_sharma","password123","Riya Sharma","\xF0\x9F\x98\x84","riya@mits.ac.in");
        addUser("karan_mehta","password123","Karan Mehta","\xF0\x9F\xA7\x91\xE2\x80\x8D\xF0\x9F\x92\xBB","karan@mits.ac.in");
        addUser("divya_joshi","password123","Divya Joshi","\xF0\x9F\x8C\xB8","divya@mits.ac.in");
        addUser("sneha_patel","password123","Sneha Patel","\xF0\x9F\x98\x84","sneha@mits.ac.in");
        addUser("arjun_verma","password123","Arjun Verma","\xF0\x9F\xA7\x91\xE2\x80\x8D\xF0\x9F\x92\xBB","arjun@mits.ac.in");
        addUser("raj_patel","password123","Raj Patel","\xF0\x9F\x91\xA8","raj@mits.ac.in");
        addUser("neha_singh","password123","Neha Singh","\xF0\x9F\x91\xA9","neha@mits.ac.in");
    }

    User& addUser(const std::string& un,const std::string& pw,const std::string& dn,const std::string& em,const std::string& email="") {
        int uid = next_id_++;
        User u; u.id=uid; u.username=un; u.password=pw; u.display_name=dn; u.avatar_emoji=em; u.email=email;
        users_[uid]=u; names_[un]=uid;
        return users_[uid];
    }
    bool usernameExists(const std::string& u) const { return names_.count(u)>0; }
    int login(const std::string& un,const std::string& pw) {
        auto it=names_.find(un); if(it==names_.end()) return -1;
        auto& u=users_[it->second]; return u.password==pw ? u.id : -1;
    }
    std::string createSession(int uid) { std::string t=make_token(); sessions_[t]=uid; return t; }
    void destroySession(const std::string& t) { sessions_.erase(t); }
    int resolve(const std::string& t) const { auto it=sessions_.find(t); return it!=sessions_.end()?it->second:-1; }
    const User* getUser(int id) const { auto it=users_.find(id); return it!=users_.end()?&it->second:nullptr; }
    User* getUserMut(int id) { auto it=users_.find(id); return it!=users_.end()?&it->second:nullptr; }
    bool resetPassword(const std::string& email) {
        for(auto& kv:users_) { if(kv.second.email==email){kv.second.password="reset123";return true;} }
        return false;
    }
    std::vector<int> allUserIds() const { std::vector<int> v; for(auto& kv:users_) v.push_back(kv.first); return v; }
    int userCount() const { return (int)users_.size(); }
private:
    int next_id_=1;
    std::unordered_map<int,User> users_;
    std::unordered_map<std::string,int> names_;
    std::unordered_map<std::string,int> sessions_;
};

/* ════════════════════════════════════════════════════════════
   FRIEND MANAGER (Adjacency List Graph + BFS + DFS)
════════════════════════════════════════════════════════════ */
class FriendManager {
public:
    void seed() {
        addFriendship(1,10); addFriendship(1,11); addFriendship(2,3);
        addFriendship(2,5); addFriendship(3,4); addFriendship(4,6);
        addFriendship(5,7); addFriendship(7,8); addFriendship(9,6);
        sendRequest(8,1); sendRequest(9,1);
    }
    void addFriendship(int a,int b) { adj_[a].insert(b); adj_[b].insert(a); }
    void removeFriendship(int a,int b) { adj_[a].erase(b); adj_[b].erase(a); }
    bool areFriends(int a,int b) const { auto it=adj_.find(a); return it!=adj_.end()&&it->second.count(b); }
    std::vector<int> getFriends(int uid) const { auto it=adj_.find(uid); if(it==adj_.end()) return {}; return {it->second.begin(),it->second.end()}; }
    int friendCount(int uid) const { auto it=adj_.find(uid); return it!=adj_.end()?(int)it->second.size():0; }

    int mutualCount(int a,int b) const {
        auto iA=adj_.find(a), iB=adj_.find(b);
        if(iA==adj_.end()||iB==adj_.end()) return 0;
        int c=0; for(int f:iA->second) if(iB->second.count(f)) c++; return c;
    }

    int sendRequest(int from,int to) {
        if(areFriends(from,to)) return -1;
        for(auto& r:reqs_) if(r.status=="pending"&&((r.from_user_id==from&&r.to_user_id==to)||(r.from_user_id==to&&r.to_user_id==from))) return -1;
        int rid=next_rid_++; reqs_.push_back({rid,from,to,"pending"}); return rid;
    }
    std::vector<FriendRequest> pendingFor(int uid) const {
        std::vector<FriendRequest> out;
        for(auto& r:reqs_) if(r.to_user_id==uid&&r.status=="pending") out.push_back(r);
        return out;
    }
    bool acceptRequest(int rid) { for(auto& r:reqs_) if(r.id==rid&&r.status=="pending"){r.status="accepted";addFriendship(r.from_user_id,r.to_user_id);return true;} return false; }
    bool declineRequest(int rid) { for(auto& r:reqs_) if(r.id==rid&&r.status=="pending"){r.status="declined";return true;} return false; }

    /* BFS friend suggestions */
    std::vector<int> suggestions(int uid,const std::vector<int>& all) const {
        std::unordered_set<int> vis; std::queue<std::pair<int,int>> q; std::vector<int> res;
        vis.insert(uid); q.push({uid,0});
        while(!q.empty()) {
            auto fr=q.front(); q.pop(); int cur=fr.first, d=fr.second;
            if(d>=2) continue;
            auto it=adj_.find(cur); if(it==adj_.end()) continue;
            for(int nb:it->second) { if(vis.count(nb)) continue; vis.insert(nb); if(nb!=uid&&!areFriends(uid,nb)) res.push_back(nb); q.push({nb,d+1}); }
        }
        for(int id:all) { if(id==uid||areFriends(uid,id)||vis.count(id)) continue; res.push_back(id); }
        return res;
    }

    /* DFS connected components */
    std::vector<std::vector<int>> discoverGroups(const std::vector<int>& all) const {
        std::unordered_set<int> vis; std::vector<std::vector<int>> groups;
        for(int uid:all) {
            if(vis.count(uid)) continue;
            std::vector<int> comp; std::stack<int> stk;
            stk.push(uid); vis.insert(uid);
            while(!stk.empty()) {
                int cur=stk.top(); stk.pop(); comp.push_back(cur);
                auto it=adj_.find(cur); if(it==adj_.end()) continue;
                for(int nb:it->second) if(!vis.count(nb)){vis.insert(nb);stk.push(nb);}
            }
            if(comp.size()>=1) groups.push_back(comp);
        }
        return groups;
    }

    /* All edges for visualization */
    std::vector<std::pair<int,int>> allEdges() const {
        std::vector<std::pair<int,int>> edges; std::set<std::pair<int,int>> seen;
        for(auto& kv:adj_) for(int fid:kv.second) {
            auto e=std::make_pair(std::min(kv.first,fid),std::max(kv.first,fid));
            if(!seen.count(e)){seen.insert(e);edges.push_back(e);}
        }
        return edges;
    }

    int totalFriendships() const { int c=0; for(auto& kv:adj_) c+=(int)kv.second.size(); return c/2; }

private:
    std::unordered_map<int,std::set<int>> adj_;
    std::vector<FriendRequest> reqs_;
    int next_rid_=1;
};

/* ════════════════════════════════════════════════════════════
   POST MANAGER
════════════════════════════════════════════════════════════ */
class PostManager {
public:
    void seed() {
        createPost(3,"Mansi Gupta","Just joined BondUp! So excited to connect with everyone here.","Excited",{});
        createPost(2,"Amit Shukla","Beautiful day at MITS campus!","Happy",{});
        createPost(1,"Ronak Gupta","Working on our Friendship Network project using DSA concepts! AVL Trees, BFS, DFS - it's all coming together!","Motivated",{});
        createPost(5,"Riya Sharma","Coffee and coding - the perfect combo","Relaxed",{});
        createPost(4,"Prateek Singh","Just completed the graph algorithms module! BFS and DFS are fascinating","Thrilled",{});
    }
    Post& createPost(int aid,const std::string& an,const std::string& txt,const std::string& mood,const std::vector<std::string>& imgs) {
        Post p; p.id=next_id_++; p.author_id=aid; p.author_name=an; p.text=txt; p.mood=mood; p.images=imgs;
        p.type=imgs.empty()?"text":"photo"; p.created_at=now_iso();
        posts_.insert(posts_.begin(),p); return posts_.front();
    }
    std::vector<Post>& allPosts() { return posts_; }
    Post* getPost(int pid) { for(auto& p:posts_) if(p.id==pid) return &p; return nullptr; }
    bool deletePost(int pid) {
        auto it=std::remove_if(posts_.begin(),posts_.end(),[pid](const Post& p){return p.id==pid;});
        if(it==posts_.end()) return false; posts_.erase(it,posts_.end());
        for(auto& kv:saved_) kv.second.erase(pid); return true;
    }
    std::pair<bool,int> toggleLike(int pid,int uid) {
        auto* p=getPost(pid); if(!p) return {false,0};
        if(p->liked_by.count(uid)){p->liked_by.erase(uid);return{false,(int)p->liked_by.size()};}
        else{p->liked_by.insert(uid);return{true,(int)p->liked_by.size()};}
    }
    int addComment(int pid,int aid,const std::string& an,const std::string& txt) {
        auto* p=getPost(pid); if(!p) return -1;
        Comment c; c.id=next_cid_++; c.post_id=pid; c.author_id=aid; c.author_name=an; c.text=txt; c.created_at=now_iso();
        p->comments.push_back(c); return c.id;
    }
    bool toggleSave(int pid,int uid) {
        if(!getPost(pid)) return false;
        if(saved_[uid].count(pid)){saved_[uid].erase(pid);return false;}
        else{saved_[uid].insert(pid);return true;}
    }
    bool isSaved(int pid,int uid) const { auto it=saved_.find(uid); return it!=saved_.end()&&it->second.count(pid); }
    std::vector<Post*> getSavedPosts(int uid) {
        std::vector<Post*> out; auto it=saved_.find(uid); if(it==saved_.end()) return out;
        for(int pid:it->second){Post* p=getPost(pid);if(p) out.push_back(p);} return out;
    }
    std::vector<json> allImages() {
        std::vector<json> imgs;
        for(auto& p:posts_) for(auto& img:p.images)
            imgs.push_back({{"src",img},{"post_id",p.id},{"author_name",p.author_name},{"created_at",p.created_at}});
        return imgs;
    }
    int totalPosts() const { return (int)posts_.size(); }
private:
    std::vector<Post> posts_;
    std::unordered_map<int,std::set<int>> saved_;
    int next_id_=1, next_cid_=1;
};

/* ════════════════════════════════════════════════════════════
   BIRTHDAY MANAGER
════════════════════════════════════════════════════════════ */
class BirthdayManager {
public:
    void seed() { addBirthday(1,"Priya Sharma","2004-05-20","B"); addBirthday(1,"Raj Patel","2003-08-15","R"); addBirthday(1,"Neha Singh","2004-01-08","N"); }
    int addBirthday(int oid,const std::string& fn,const std::string& dt,const std::string& em) {
        Birthday b; b.id=next_id_++; b.owner_id=oid; b.friend_name=fn; b.birthday=dt; b.emoji=em;
        bdays_.push_back(b); return b.id;
    }
    std::vector<Birthday> getForUser(int uid) const { std::vector<Birthday> out; for(auto& b:bdays_) if(b.owner_id==uid) out.push_back(b); return out; }
    bool deleteBirthday(int bid) {
        auto it=std::remove_if(bdays_.begin(),bdays_.end(),[bid](const Birthday& b){return b.id==bid;});
        if(it==bdays_.end()) return false; bdays_.erase(it,bdays_.end()); return true;
    }
private:
    std::vector<Birthday> bdays_; int next_id_=1;
};

/* ════════════════════════════════════════════════════════════
   EVENT MANAGER
════════════════════════════════════════════════════════════ */
class EventManager {
public:
    void seed() {
        createEvent(1,"Ronak Gupta","DSA Study Group","Weekly Data Structures revision","2026-04-20","15:00","Library, Block A","S");
        createEvent(2,"Amit Shukla","Campus Photography Walk","Bring your cameras!","2026-04-22","07:00","Main Gate","P");
        createEvent(3,"Mansi Gupta","Farewell Party Planning","Plan the batch farewell!","2026-05-01","16:00","Auditorium","F");
    }
    int createEvent(int cid,const std::string& cn,const std::string& t,const std::string& d,const std::string& dt,const std::string& tm,const std::string& loc,const std::string& em) {
        Event e; e.id=next_id_++; e.creator_id=cid; e.creator_name=cn; e.title=t; e.description=d;
        e.date=dt; e.time=tm; e.location=loc; e.emoji=em; e.created_at=now_iso(); e.attendees.insert(cid);
        events_.push_back(e); return e.id;
    }
    std::vector<Event>& allEvents() { return events_; }
    Event* getEvent(int eid) { for(auto& e:events_) if(e.id==eid) return &e; return nullptr; }
    bool deleteEvent(int eid) {
        auto it=std::remove_if(events_.begin(),events_.end(),[eid](const Event& e){return e.id==eid;});
        if(it==events_.end()) return false; events_.erase(it,events_.end()); return true;
    }
    bool toggleAttend(int eid,int uid) {
        auto* e=getEvent(eid); if(!e) return false;
        if(e->attendees.count(uid)){e->attendees.erase(uid);return false;}
        else{e->attendees.insert(uid);return true;}
    }
private:
    std::vector<Event> events_; int next_id_=1;
};

/* ════════════════════════════════════════════════════════════
   ACTIVITY LOG MANAGER (Stack — LIFO)
════════════════════════════════════════════════════════════ */
class ActivityLogManager {
public:
    void push(const std::string& action,const std::string& msg,int uid=0) {
        ActivityEntry e; e.id=next_id_++; e.action=action; e.message=msg; e.timestamp=now_iso(); e.user_id=uid;
        log_stack_.push(e);
        log_vec_.insert(log_vec_.begin(),e);
        if(log_vec_.size()>200) log_vec_.pop_back();
    }
    std::vector<ActivityEntry> getAll() const { return log_vec_; }
    int count() const { return (int)log_vec_.size(); }
private:
    std::stack<ActivityEntry> log_stack_;
    std::vector<ActivityEntry> log_vec_;
    int next_id_=1;
};

/* ════════════════════════════════════════════════════════════
   HELPER: extract Bearer token
════════════════════════════════════════════════════════════ */
static std::string extractToken(const HttpRequest& req) {
    auto it = req.headers.find("authorization");
    if (it == req.headers.end()) return "";
    const std::string& val = it->second;
    if (val.substr(0, 7) == "Bearer ") return val.substr(7);
    return val;
}

/* ════════════════════════════════════════════════════════════
   MAIN
════════════════════════════════════════════════════════════ */
int main() {
    std::cout << "\n  BondUp - Friendship Network\n  C++ Backend Server\n" << std::endl;

    AuthManager auth; FriendManager friends; PostManager posts;
    BirthdayManager birthdays; EventManager events; ActivityLogManager actLog;

    friends.seed(); posts.seed(); birthdays.seed(); events.seed();
    actLog.push("system","Server started");
    actLog.push("user_added","Ronak Gupta joined",1);
    actLog.push("user_added","Amit Shukla joined",2);
    actLog.push("friendship","Ronak and Raj became friends",1);
    actLog.push("friendship","Amit and Mansi became friends",2);
    actLog.push("post","Mansi shared a post",3);
    actLog.push("post","Amit shared a post",2);
    actLog.push("event","Ronak created DSA Study Group",1);
    actLog.push("friend_request","Sneha sent request to Ronak",8);
    actLog.push("friend_request","Arjun sent request to Ronak",9);

    SimpleHttpServer svr;

    /* Health */
    svr.Get("/api/health", [](const HttpRequest&, HttpResponse& res) {
        res.set_content("{\"status\":\"ok\",\"server\":\"BondUp C++\"}", "application/json");
    });

    /* Auth: Register */
    svr.Post("/api/auth/register", [&](const HttpRequest& req, HttpResponse& res) {
        json body; try{body=json::parse(req.body);}catch(...){res.status=400;res.set_content("{\"error\":\"Invalid JSON\"}","application/json");return;}
        std::string un=body.value("username",""),pw=body.value("password",""),dn=body.value("display_name",""),em=body.value("avatar_emoji","E"),email=body.value("email","");
        if(un.size()<3){res.status=400;res.set_content("{\"error\":\"Username min 3 chars\"}","application/json");return;}
        if(pw.size()<6){res.status=400;res.set_content("{\"error\":\"Password min 6 chars\"}","application/json");return;}
        if(dn.empty()) dn=un;
        if(auth.usernameExists(un)){res.status=409;res.set_content("{\"error\":\"Username taken\"}","application/json");return;}
        auto& u=auth.addUser(un,pw,dn,em,email);
        std::string tok=auth.createSession(u.id);
        actLog.push("user_added",dn+" joined the network",u.id);
        json j; j["token"]=tok; j["user"]={{"id",u.id},{"username",u.username},{"display_name",u.display_name},{"avatar_emoji",u.avatar_emoji}};
        res.set_content(j.dump(),"application/json");
    });

    /* Auth: Login */
    svr.Post("/api/auth/login", [&](const HttpRequest& req, HttpResponse& res) {
        json body; try{body=json::parse(req.body);}catch(...){res.status=400;res.set_content("{\"error\":\"Invalid JSON\"}","application/json");return;}
        std::string un=body.value("username",""),pw=body.value("password","");
        int uid=auth.login(un,pw);
        if(uid<0){res.status=401;res.set_content("{\"error\":\"Invalid username or password\"}","application/json");return;}
        std::string tok=auth.createSession(uid);
        const User* u=auth.getUser(uid);
        actLog.push("login",u->display_name+" logged in",uid);
        json j; j["token"]=tok; j["user"]={{"id",u->id},{"username",u->username},{"display_name",u->display_name},{"avatar_emoji",u->avatar_emoji}};
        res.set_content(j.dump(),"application/json");
        std::cout << "[LOGIN] " << un << std::endl;
    });

    /* Auth: Logout */
    svr.Post("/api/auth/logout", [&](const HttpRequest& req, HttpResponse& res) {
        auth.destroySession(extractToken(req));
        res.set_content("{\"ok\":true}","application/json");
    });

    /* Auth: Forgot Password */
    svr.Post("/api/auth/forgot-password", [&](const HttpRequest& req, HttpResponse& res) {
        json body; try{body=json::parse(req.body);}catch(...){res.status=400;res.set_content("{\"error\":\"Invalid JSON\"}","application/json");return;}
        auth.resetPassword(body.value("email",""));
        res.set_content("{\"ok\":true,\"message\":\"Password reset to 'reset123'\"}","application/json");
    });

    /* Friends: Suggestions (BFS) */
    svr.Get("/api/friends/suggestions", [&](const HttpRequest& req, HttpResponse& res) {
        int uid=auth.resolve(extractToken(req));
        if(uid<0){res.status=401;res.set_content("{\"error\":\"Unauthorized\"}","application/json");return;}
        auto ids=friends.suggestions(uid,auth.allUserIds());
        json arr=json::array();
        for(int id:ids){const User* u=auth.getUser(id);if(!u) continue;
            arr.push_back({{"id",u->id},{"username",u->username},{"display_name",u->display_name},{"avatar_emoji",u->avatar_emoji},{"mutual_friends",friends.mutualCount(uid,id)}});}
        json j; j["suggestions"]=arr; res.set_content(j.dump(),"application/json");
    });

    /* Friends: My list */
    svr.Get("/api/friends", [&](const HttpRequest& req, HttpResponse& res) {
        int uid=auth.resolve(extractToken(req));
        if(uid<0){res.status=401;res.set_content("{\"error\":\"Unauthorized\"}","application/json");return;}
        auto fids=friends.getFriends(uid); json arr=json::array();
        for(int fid:fids){const User* u=auth.getUser(fid);if(!u)continue;
            arr.push_back({{"id",u->id},{"username",u->username},{"display_name",u->display_name},{"avatar_emoji",u->avatar_emoji},{"mutual_friends",friends.mutualCount(uid,fid)}});}
        json j; j["friends"]=arr; res.set_content(j.dump(),"application/json");
    });

    /* Friends: Send request */
    svr.Post("/api/friends/request/{id}", [&](const HttpRequest& req, HttpResponse& res) {
        int uid=auth.resolve(extractToken(req)); if(uid<0){res.status=401;res.set_content("{\"error\":\"Unauthorized\"}","application/json");return;}
        int tid=std::stoi(req.path_parts[0]); int rid=friends.sendRequest(uid,tid);
        if(rid<0){res.status=400;res.set_content("{\"error\":\"Already friends or pending\"}","application/json");return;}
        actLog.push("friend_request","Friend request sent",uid);
        json j; j["ok"]=true; j["request_id"]=rid; res.set_content(j.dump(),"application/json");
    });

    /* Friends: Pending requests */
    svr.Get("/api/friends/requests", [&](const HttpRequest& req, HttpResponse& res) {
        int uid=auth.resolve(extractToken(req)); if(uid<0){res.status=401;res.set_content("{\"error\":\"Unauthorized\"}","application/json");return;}
        auto reqs=friends.pendingFor(uid); json arr=json::array();
        for(auto& r:reqs){const User* u=auth.getUser(r.from_user_id);
            arr.push_back({{"request_id",r.id},{"from_user_id",r.from_user_id},{"username",u?u->username:""},{"display_name",u?u->display_name:""},{"avatar_emoji",u?u->avatar_emoji:"E"},{"friend_count",friends.friendCount(r.from_user_id)}});}
        json j; j["requests"]=arr; res.set_content(j.dump(),"application/json");
    });

    /* Friends: Accept */
    svr.Post("/api/friends/accept/{id}", [&](const HttpRequest& req, HttpResponse& res) {
        int uid=auth.resolve(extractToken(req)); if(uid<0){res.status=401;res.set_content("{\"error\":\"Unauthorized\"}","application/json");return;}
        int rid=std::stoi(req.path_parts[0]);
        if(friends.acceptRequest(rid)){actLog.push("friendship","Friend request accepted",uid);res.set_content("{\"ok\":true}","application/json");}
        else{res.status=404;res.set_content("{\"error\":\"Not found\"}","application/json");}
    });

    /* Friends: Decline */
    svr.Post("/api/friends/decline/{id}", [&](const HttpRequest& req, HttpResponse& res) {
        int uid=auth.resolve(extractToken(req)); if(uid<0){res.status=401;res.set_content("{\"error\":\"Unauthorized\"}","application/json");return;}
        if(friends.declineRequest(std::stoi(req.path_parts[0]))) res.set_content("{\"ok\":true}","application/json");
        else{res.status=404;res.set_content("{\"error\":\"Not found\"}","application/json");}
    });

    /* Friends: Remove */
    svr.Post("/api/friends/remove/{id}", [&](const HttpRequest& req, HttpResponse& res) {
        int uid=auth.resolve(extractToken(req)); if(uid<0){res.status=401;res.set_content("{\"error\":\"Unauthorized\"}","application/json");return;}
        friends.removeFriendship(uid,std::stoi(req.path_parts[0]));
        actLog.push("unfriend","Friendship removed",uid);
        res.set_content("{\"ok\":true}","application/json");
    });

    /* Network Graph */
    svr.Get("/api/network-graph", [&](const HttpRequest& req, HttpResponse& res) {
        int uid=auth.resolve(extractToken(req)); if(uid<0){res.status=401;res.set_content("{\"error\":\"Unauthorized\"}","application/json");return;}
        json nodes=json::array();
        for(int id:auth.allUserIds()){const User* u=auth.getUser(id);if(!u)continue;
            nodes.push_back({{"id",u->id},{"name",u->display_name},{"emoji",u->avatar_emoji},{"friends",friends.friendCount(u->id)}});}
        json edges=json::array();
        for(auto& e:friends.allEdges()) edges.push_back({{"source",e.first},{"target",e.second}});
        json j; j["nodes"]=nodes; j["edges"]=edges;
        j["stats"]={{"total_users",auth.userCount()},{"total_friendships",friends.totalFriendships()},{"your_friends",friends.friendCount(uid)}};
        res.set_content(j.dump(),"application/json");
    });

    /* Groups (DFS) */
    svr.Get("/api/groups", [&](const HttpRequest& req, HttpResponse& res) {
        int uid=auth.resolve(extractToken(req)); if(uid<0){res.status=401;res.set_content("{\"error\":\"Unauthorized\"}","application/json");return;}
        auto allIds=auth.allUserIds(); std::sort(allIds.begin(),allIds.end());
        auto groups=friends.discoverGroups(allIds);
        json arr=json::array(); int gn=1;
        for(auto& grp:groups){
            json members=json::array();
            for(int mid:grp){const User* u=auth.getUser(mid);if(!u)continue;members.push_back({{"id",u->id},{"name",u->display_name},{"emoji",u->avatar_emoji},{"friends",friends.friendCount(u->id)}});}
            int ie=0; for(size_t i=0;i<grp.size();i++) for(size_t k=i+1;k<grp.size();k++) if(friends.areFriends(grp[i],grp[k])) ie++;
            double density=grp.size()>1?(2.0*ie)/(grp.size()*(grp.size()-1)):0;
            arr.push_back({{"group_id",gn++},{"size",(int)grp.size()},{"members",members},{"internal_connections",ie},{"density",density}});
        }
        json j; j["groups"]=arr; j["total_groups"]=(int)groups.size(); j["algorithm"]="Iterative DFS with explicit stack";
        res.set_content(j.dump(),"application/json");
    });

    /* Activity Log (Stack LIFO) */
    svr.Get("/api/activity-log", [&](const HttpRequest& req, HttpResponse& res) {
        int uid=auth.resolve(extractToken(req)); if(uid<0){res.status=401;res.set_content("{\"error\":\"Unauthorized\"}","application/json");return;}
        auto entries=actLog.getAll(); json arr=json::array();
        for(auto& e:entries) arr.push_back({{"id",e.id},{"action",e.action},{"message",e.message},{"timestamp",e.timestamp},{"user_id",e.user_id}});
        json j; j["log"]=arr; j["total"]=actLog.count(); j["data_structure"]="Stack (LIFO)";
        res.set_content(j.dump(),"application/json");
    });

    /* Stats */
    svr.Get("/api/stats", [&](const HttpRequest& req, HttpResponse& res) {
        int uid=auth.resolve(extractToken(req)); if(uid<0){res.status=401;res.set_content("{\"error\":\"Unauthorized\"}","application/json");return;}
        json j; j["total_users"]=auth.userCount(); j["total_friendships"]=friends.totalFriendships();
        j["total_posts"]=posts.totalPosts(); j["my_friends"]=friends.friendCount(uid);
        j["total_groups"]=(int)friends.discoverGroups(auth.allUserIds()).size(); j["activity_count"]=actLog.count();
        res.set_content(j.dump(),"application/json");
    });

    /* Birthdays */
    svr.Get("/api/birthdays", [&](const HttpRequest& req, HttpResponse& res) {
        int uid=auth.resolve(extractToken(req)); if(uid<0){res.status=401;res.set_content("{\"error\":\"Unauthorized\"}","application/json");return;}
        auto list=birthdays.getForUser(uid); json arr=json::array();
        for(auto& b:list) arr.push_back({{"id",b.id},{"friend_name",b.friend_name},{"birthday",b.birthday},{"emoji",b.emoji}});
        json j; j["birthdays"]=arr; res.set_content(j.dump(),"application/json");
    });
    svr.Post("/api/birthdays", [&](const HttpRequest& req, HttpResponse& res) {
        int uid=auth.resolve(extractToken(req)); if(uid<0){res.status=401;res.set_content("{\"error\":\"Unauthorized\"}","application/json");return;}
        json body; try{body=json::parse(req.body);}catch(...){res.status=400;res.set_content("{\"error\":\"Invalid JSON\"}","application/json");return;}
        int bid=birthdays.addBirthday(uid,body.value("friend_name",""),body.value("birthday",""),body.value("emoji","B"));
        actLog.push("birthday","Birthday added",uid);
        json j; j["ok"]=true; j["id"]=bid; res.set_content(j.dump(),"application/json");
    });
    svr.Delete("/api/birthdays/{id}", [&](const HttpRequest& req, HttpResponse& res) {
        int uid=auth.resolve(extractToken(req)); if(uid<0){res.status=401;res.set_content("{\"error\":\"Unauthorized\"}","application/json");return;}
        if(birthdays.deleteBirthday(std::stoi(req.path_parts[0]))) res.set_content("{\"ok\":true}","application/json");
        else{res.status=404;res.set_content("{\"error\":\"Not found\"}","application/json");}
    });

    /* Events */
    svr.Get("/api/events", [&](const HttpRequest& req, HttpResponse& res) {
        int uid=auth.resolve(extractToken(req)); if(uid<0){res.status=401;res.set_content("{\"error\":\"Unauthorized\"}","application/json");return;}
        json arr=json::array();
        for(auto& e:events.allEvents())
            arr.push_back({{"id",e.id},{"creator_name",e.creator_name},{"title",e.title},{"description",e.description},{"date",e.date},{"time",e.time},{"location",e.location},{"emoji",e.emoji},{"attendees",(int)e.attendees.size()},{"attending",e.attendees.count(uid)>0},{"created_at",e.created_at}});
        json j; j["events"]=arr; res.set_content(j.dump(),"application/json");
    });
    svr.Post("/api/events", [&](const HttpRequest& req, HttpResponse& res) {
        int uid=auth.resolve(extractToken(req)); if(uid<0){res.status=401;res.set_content("{\"error\":\"Unauthorized\"}","application/json");return;}
        const User* u=auth.getUser(uid); json body; try{body=json::parse(req.body);}catch(...){res.status=400;res.set_content("{\"error\":\"Invalid JSON\"}","application/json");return;}
        int eid=events.createEvent(uid,u?u->display_name:"User",body.value("title",""),body.value("description",""),body.value("date",""),body.value("time",""),body.value("location",""),body.value("emoji","E"));
        actLog.push("event",(u?u->display_name:"User")+" created event: "+body.value("title",""),uid);
        json j; j["ok"]=true; j["id"]=eid; res.set_content(j.dump(),"application/json");
    });
    svr.Delete("/api/events/{id}", [&](const HttpRequest& req, HttpResponse& res) {
        int uid=auth.resolve(extractToken(req)); if(uid<0){res.status=401;res.set_content("{\"error\":\"Unauthorized\"}","application/json");return;}
        if(events.deleteEvent(std::stoi(req.path_parts[0]))) res.set_content("{\"ok\":true}","application/json");
        else{res.status=404;res.set_content("{\"error\":\"Not found\"}","application/json");}
    });
    svr.Post("/api/events/{id}/attend", [&](const HttpRequest& req, HttpResponse& res) {
        int uid=auth.resolve(extractToken(req)); if(uid<0){res.status=401;res.set_content("{\"error\":\"Unauthorized\"}","application/json");return;}
        int eid=std::stoi(req.path_parts[0]); bool attending=events.toggleAttend(eid,uid);
        auto* e=events.getEvent(eid);
        json j; j["attending"]=attending; j["attendees"]=e?(int)e->attendees.size():0;
        res.set_content(j.dump(),"application/json");
    });

    /* Posts */
    svr.Get("/api/posts", [&](const HttpRequest& req, HttpResponse& res) {
        int uid=auth.resolve(extractToken(req)); if(uid<0){res.status=401;res.set_content("{\"error\":\"Unauthorized\"}","application/json");return;}
        json arr=json::array();
        for(auto& p:posts.allPosts())
            arr.push_back({{"id",p.id},{"author_id",p.author_id},{"author_name",p.author_name},{"text",p.text},{"mood",p.mood},{"type",p.type},{"images",p.images},{"likes",(int)p.liked_by.size()},{"liked_by_me",p.liked_by.count(uid)>0},{"comments",(int)p.comments.size()},{"created_at",p.created_at},{"saved_by_me",posts.isSaved(p.id,uid)}});
        json j; j["posts"]=arr; res.set_content(j.dump(),"application/json");
    });
    svr.Post("/api/posts", [&](const HttpRequest& req, HttpResponse& res) {
        int uid=auth.resolve(extractToken(req)); if(uid<0){res.status=401;res.set_content("{\"error\":\"Unauthorized\"}","application/json");return;}
        const User* u=auth.getUser(uid); json body; try{body=json::parse(req.body);}catch(...){res.status=400;res.set_content("{\"error\":\"Invalid JSON\"}","application/json");return;}
        std::vector<std::string> imgs; if(body.contains("images")&&body["images"].is_array()) for(auto& img:body["images"]) imgs.push_back(img.get<std::string>());
        auto& p=posts.createPost(uid,u?u->display_name:"User",body.value("text",""),body.value("mood",""),imgs);
        actLog.push("post",(u?u->display_name:"User")+" shared a post",uid);
        json j; j["ok"]=true; j["post"]={{"id",p.id},{"created_at",p.created_at}};
        res.set_content(j.dump(),"application/json");
    });
    svr.Delete("/api/posts/{id}", [&](const HttpRequest& req, HttpResponse& res) {
        int uid=auth.resolve(extractToken(req)); if(uid<0){res.status=401;res.set_content("{\"error\":\"Unauthorized\"}","application/json");return;}
        if(posts.deletePost(std::stoi(req.path_parts[0]))) res.set_content("{\"ok\":true}","application/json");
        else{res.status=404;res.set_content("{\"error\":\"Not found\"}","application/json");}
    });
    svr.Post("/api/posts/{id}/like", [&](const HttpRequest& req, HttpResponse& res) {
        int uid=auth.resolve(extractToken(req)); if(uid<0){res.status=401;res.set_content("{\"error\":\"Unauthorized\"}","application/json");return;}
        auto lr=posts.toggleLike(std::stoi(req.path_parts[0]),uid);
        if(lr.first) actLog.push("like","Post liked",uid);
        json j; j["liked"]=lr.first; j["count"]=lr.second; res.set_content(j.dump(),"application/json");
    });
    svr.Post("/api/posts/{id}/save", [&](const HttpRequest& req, HttpResponse& res) {
        int uid=auth.resolve(extractToken(req)); if(uid<0){res.status=401;res.set_content("{\"error\":\"Unauthorized\"}","application/json");return;}
        bool saved=posts.toggleSave(std::stoi(req.path_parts[0]),uid);
        json j; j["saved"]=saved; res.set_content(j.dump(),"application/json");
    });
    svr.Get("/api/posts/saved", [&](const HttpRequest& req, HttpResponse& res) {
        int uid=auth.resolve(extractToken(req)); if(uid<0){res.status=401;res.set_content("{\"error\":\"Unauthorized\"}","application/json");return;}
        auto saved=posts.getSavedPosts(uid); json arr=json::array();
        for(auto* p:saved) arr.push_back({{"id",p->id},{"author_id",p->author_id},{"author_name",p->author_name},{"text",p->text},{"mood",p->mood},{"type",p->type},{"images",p->images},{"likes",(int)p->liked_by.size()},{"liked_by_me",p->liked_by.count(uid)>0},{"comments",(int)p->comments.size()},{"created_at",p->created_at},{"saved_by_me",true}});
        json j; j["posts"]=arr; res.set_content(j.dump(),"application/json");
    });
    svr.Get("/api/photos", [&](const HttpRequest& req, HttpResponse& res) {
        int uid=auth.resolve(extractToken(req)); if(uid<0){res.status=401;res.set_content("{\"error\":\"Unauthorized\"}","application/json");return;}
        json j; j["photos"]=posts.allImages(); res.set_content(j.dump(),"application/json");
    });
    svr.Get("/api/posts/{id}/comments", [&](const HttpRequest& req, HttpResponse& res) {
        int uid=auth.resolve(extractToken(req)); if(uid<0){res.status=401;res.set_content("{\"error\":\"Unauthorized\"}","application/json");return;}
        auto* p=posts.getPost(std::stoi(req.path_parts[0]));
        if(!p){res.status=404;res.set_content("{\"error\":\"Not found\"}","application/json");return;}
        json arr=json::array(); for(auto& c:p->comments) arr.push_back({{"id",c.id},{"author_name",c.author_name},{"text",c.text},{"created_at",c.created_at}});
        json j; j["comments"]=arr; res.set_content(j.dump(),"application/json");
    });
    svr.Post("/api/posts/{id}/comment", [&](const HttpRequest& req, HttpResponse& res) {
        int uid=auth.resolve(extractToken(req)); if(uid<0){res.status=401;res.set_content("{\"error\":\"Unauthorized\"}","application/json");return;}
        const User* u=auth.getUser(uid); json body; try{body=json::parse(req.body);}catch(...){res.status=400;res.set_content("{\"error\":\"Invalid JSON\"}","application/json");return;}
        std::string txt=body.value("text",""); if(txt.empty()){res.status=400;res.set_content("{\"error\":\"Text required\"}","application/json");return;}
        int cid=posts.addComment(std::stoi(req.path_parts[0]),uid,u?u->display_name:"User",txt);
        if(cid<0){res.status=404;res.set_content("{\"error\":\"Post not found\"}","application/json");return;}
        actLog.push("comment","Comment added",uid);
        json j; j["ok"]=true; j["comment_id"]=cid; res.set_content(j.dump(),"application/json");
    });

    /* Start Server */
    int PORT=8080;
    const char* ep=getenv("PORT"); if(ep) PORT=atoi(ep);
    std::cout << "  Static files: ../" << std::endl;
    std::cout << "  Accounts: ronak_gupta / password123" << std::endl;
    std::cout << "  Press Ctrl+C to stop.\n" << std::endl;
    svr.listen("0.0.0.0", PORT);
    return 0;
}
