/* ============================================================
   BondUp – Friendship Network  |  C++ Backend Server
   File: main.cpp
   
   Dependencies (header-only, already in this folder):
     - httplib.h   (cpp-httplib)
     - json.hpp    (nlohmann/json)

   Compile (MSVC):
     cl /std:c++17 /EHsc /O2 /Fe:server.exe main.cpp ws2_32.lib

   Compile (g++):
     g++ -std=c++17 -O2 main.cpp -lws2_32 -o server.exe      (Windows)
     g++ -std=c++17 -O2 main.cpp -lpthread -o server          (Linux)

   Run:
     server.exe   (or ./server on Linux)
     Then open http://localhost:8080/ in your browser.

   Data Structures Used:
     - std::unordered_map  – O(1) user/session lookups (Hash Table)
     - std::vector          – ordered collections (posts, comments, events)
     - std::map             – sorted birthday storage
     - Adjacency List       – friendship graph (undirected)
     - std::set             – friend-request dedup, like tracking, saved posts
     - std::queue           – BFS for friend suggestions
   
   OOP Concepts:
     - Encapsulation (private members, public interfaces)
     - Composition  (managers own data collections)
     - Abstraction  (clean interfaces between managers)
   ============================================================ */

#include "httplib.h"
#include "json.hpp"

#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <map>
#include <set>
#include <queue>
#include <algorithm>
#include <chrono>
#include <ctime>
#include <sstream>
#include <mutex>
#include <random>
#include <functional>
#include <iomanip>
#include <fstream>
#include <cstdlib>

using json = nlohmann::json;

/* ════════════════════════════════════════════════════════════
   UTILITY FUNCTIONS
════════════════════════════════════════════════════════════ */
static std::string now_iso() {
    auto now = std::chrono::system_clock::now();
    auto t   = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif
    std::ostringstream ss;
    ss << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%S");
    return ss.str();
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
    int         id;
    std::string username;
    std::string password;
    std::string display_name;
    std::string avatar_emoji;
    std::string email;
};

struct FriendRequest {
    int         id;
    int         from_user_id;
    int         to_user_id;
    std::string status;   // "pending", "accepted", "declined"
};

struct Comment {
    int         id;
    int         post_id;
    int         author_id;
    std::string author_name;
    std::string text;
    std::string created_at;
};

struct Post {
    int              id;
    int              author_id;
    std::string      author_name;
    std::string      text;
    std::string      mood;
    std::string      type;        // "text" or "photo"
    std::vector<std::string> images;
    std::set<int>    liked_by;
    std::vector<Comment> comments;
    std::string      created_at;
};

struct Birthday {
    int         id;
    int         owner_id;
    std::string friend_name;
    std::string birthday;   // "YYYY-MM-DD"
    std::string emoji;
};

struct Event {
    int         id;
    int         creator_id;
    std::string creator_name;
    std::string title;
    std::string description;
    std::string date;        // "YYYY-MM-DD"
    std::string time;        // "HH:MM"
    std::string location;
    std::string emoji;
    std::set<int> attendees;
    std::string created_at;
};

/* ════════════════════════════════════════════════════════════
   AUTH MANAGER
════════════════════════════════════════════════════════════ */
class AuthManager {
public:
    AuthManager() {
        addUser("ronak_gupta",   "password123", "Ronak Gupta",    "😊", "ronak@mits.ac.in");
        addUser("amit_shukla",   "password123", "Amit Shukla",    "😎", "amit@mits.ac.in");
        addUser("mansi_gupta",   "password123", "Mansi Gupta",    "😊", "mansi@mits.ac.in");
        addUser("prateek_singh", "password123", "Prateek Singh",  "🧑‍🎓", "prateek@mits.ac.in");
        addUser("riya_sharma",   "password123", "Riya Sharma",    "😄", "riya@mits.ac.in");
        addUser("karan_mehta",   "password123", "Karan Mehta",    "🧑‍💻", "karan@mits.ac.in");
        addUser("divya_joshi",   "password123", "Divya Joshi",    "🌸", "divya@mits.ac.in");
        addUser("sneha_patel",   "password123", "Sneha Patel",    "😄", "sneha@mits.ac.in");
        addUser("arjun_verma",   "password123", "Arjun Verma",    "🧑‍💻", "arjun@mits.ac.in");
        addUser("raj_patel",     "password123", "Raj Patel",      "👨", "raj@mits.ac.in");
        addUser("neha_singh",    "password123", "Neha Singh",     "👩", "neha@mits.ac.in");
    }

    User& addUser(const std::string& uname, const std::string& pw,
                  const std::string& display, const std::string& emoji,
                  const std::string& email = "") {
        int uid = next_id_++;
        User u{uid, uname, pw, display, emoji, email};
        users_by_id_[uid]     = u;
        users_by_name_[uname] = uid;
        return users_by_id_[uid];
    }

    bool usernameExists(const std::string& uname) const {
        return users_by_name_.count(uname) > 0;
    }

    int login(const std::string& uname, const std::string& pw) {
        auto it = users_by_name_.find(uname);
        if (it == users_by_name_.end()) return -1;
        auto& u = users_by_id_[it->second];
        if (u.password != pw) return -1;
        return u.id;
    }

    std::string createSession(int uid) {
        std::string tok = make_token();
        sessions_[tok] = uid;
        return tok;
    }

    void destroySession(const std::string& tok) { sessions_.erase(tok); }

    int resolve(const std::string& tok) const {
        auto it = sessions_.find(tok);
        return it != sessions_.end() ? it->second : -1;
    }

    const User* getUser(int id) const {
        auto it = users_by_id_.find(id);
        return it != users_by_id_.end() ? &it->second : nullptr;
    }

    User* getUserMut(int id) {
        auto it = users_by_id_.find(id);
        return it != users_by_id_.end() ? &it->second : nullptr;
    }

    bool resetPassword(const std::string& email) {
        for (auto& [uid, u] : users_by_id_) {
            if (u.email == email) {
                u.password = "reset123";
                return true;
            }
        }
        return false;
    }

    std::vector<int> allUserIds() const {
        std::vector<int> ids;
        ids.reserve(users_by_id_.size());
        for (auto& [id, _] : users_by_id_) ids.push_back(id);
        return ids;
    }

    int userCount() const { return (int)users_by_id_.size(); }

private:
    int next_id_ = 1;
    std::unordered_map<int, User>        users_by_id_;
    std::unordered_map<std::string, int> users_by_name_;
    std::unordered_map<std::string, int> sessions_;
};

/* ════════════════════════════════════════════════════════════
   FRIEND MANAGER  (Adjacency List Graph + BFS)
════════════════════════════════════════════════════════════ */
class FriendManager {
public:
    FriendManager() = default;

    void seed(AuthManager& auth) {
        addFriendship(1, 10);   // ronak <-> raj
        addFriendship(1, 11);   // ronak <-> neha
        sendRequest(8, 1);      // sneha -> ronak
        sendRequest(9, 1);      // arjun -> ronak
    }

    void addFriendship(int a, int b) {
        adj_[a].insert(b);
        adj_[b].insert(a);
    }

    void removeFriendship(int a, int b) {
        adj_[a].erase(b);
        adj_[b].erase(a);
    }

    bool areFriends(int a, int b) const {
        auto it = adj_.find(a);
        return it != adj_.end() && it->second.count(b);
    }

    std::vector<int> getFriends(int uid) const {
        auto it = adj_.find(uid);
        if (it == adj_.end()) return {};
        return {it->second.begin(), it->second.end()};
    }

    int friendCount(int uid) const {
        auto it = adj_.find(uid);
        return it != adj_.end() ? (int)it->second.size() : 0;
    }

    /* Count mutual friends between two users */
    int mutualCount(int a, int b) const {
        auto itA = adj_.find(a);
        auto itB = adj_.find(b);
        if (itA == adj_.end() || itB == adj_.end()) return 0;
        int count = 0;
        for (int f : itA->second)
            if (itB->second.count(f)) count++;
        return count;
    }

    int sendRequest(int from, int to) {
        if (areFriends(from, to)) return -1;
        for (auto& r : requests_) {
            if (r.status == "pending" &&
                ((r.from_user_id == from && r.to_user_id == to) ||
                 (r.from_user_id == to   && r.to_user_id == from)))
                return -1;
        }
        int rid = next_req_id_++;
        requests_.push_back({rid, from, to, "pending"});
        return rid;
    }

    std::vector<FriendRequest> pendingFor(int uid) const {
        std::vector<FriendRequest> out;
        for (auto& r : requests_)
            if (r.to_user_id == uid && r.status == "pending")
                out.push_back(r);
        return out;
    }

    bool acceptRequest(int reqId) {
        for (auto& r : requests_) {
            if (r.id == reqId && r.status == "pending") {
                r.status = "accepted";
                addFriendship(r.from_user_id, r.to_user_id);
                return true;
            }
        }
        return false;
    }

    bool declineRequest(int reqId) {
        for (auto& r : requests_) {
            if (r.id == reqId && r.status == "pending") {
                r.status = "declined";
                return true;
            }
        }
        return false;
    }

    /* BFS-based friend suggestions */
    std::vector<int> suggestions(int uid, const std::vector<int>& allIds) const {
        std::unordered_set<int> visited;
        std::queue<std::pair<int,int>> q;
        std::vector<int> result;

        visited.insert(uid);
        q.push({uid, 0});

        while (!q.empty()) {
            auto [cur, depth] = q.front(); q.pop();
            if (depth >= 2) continue;
            auto it = adj_.find(cur);
            if (it == adj_.end()) continue;
            for (int nb : it->second) {
                if (visited.count(nb)) continue;
                visited.insert(nb);
                if (nb != uid && !areFriends(uid, nb))
                    result.push_back(nb);
                q.push({nb, depth + 1});
            }
        }

        for (int id : allIds) {
            if (id == uid || areFriends(uid, id) || visited.count(id)) continue;
            result.push_back(id);
        }
        return result;
    }

private:
    std::unordered_map<int, std::set<int>> adj_;
    std::vector<FriendRequest> requests_;
    int next_req_id_ = 1;
};

/* ════════════════════════════════════════════════════════════
   POST MANAGER  (with saved/bookmarked posts)
════════════════════════════════════════════════════════════ */
class PostManager {
public:
    PostManager() = default;

    void seed() {
        createPost(3, "Mansi Gupta",
                   "Just joined BondUp! 🎉 So excited to connect with everyone here.",
                   "🥳 Excited", {});
        createPost(2, "Amit Shukla",
                   "Beautiful day at MITS campus! ☀️",
                   "😊 Happy", {});
    }

    Post& createPost(int authorId, const std::string& authorName,
                     const std::string& text, const std::string& mood,
                     const std::vector<std::string>& images) {
        int pid = next_id_++;
        Post p;
        p.id          = pid;
        p.author_id   = authorId;
        p.author_name = authorName;
        p.text        = text;
        p.mood        = mood;
        p.images      = images;
        p.type        = images.empty() ? "text" : "photo";
        p.created_at  = now_iso();
        posts_.insert(posts_.begin(), p);
        return posts_.front();
    }

    std::vector<Post>& allPosts() { return posts_; }

    Post* getPost(int pid) {
        for (auto& p : posts_) if (p.id == pid) return &p;
        return nullptr;
    }

    bool deletePost(int pid) {
        auto it = std::remove_if(posts_.begin(), posts_.end(),
                                 [pid](const Post& p){ return p.id == pid; });
        if (it == posts_.end()) return false;
        posts_.erase(it, posts_.end());
        // Remove from all saved lists
        for (auto& [uid, s] : saved_) s.erase(pid);
        return true;
    }

    std::pair<bool,int> toggleLike(int pid, int uid) {
        auto* p = getPost(pid);
        if (!p) return {false, 0};
        if (p->liked_by.count(uid)) {
            p->liked_by.erase(uid);
            return {false, (int)p->liked_by.size()};
        } else {
            p->liked_by.insert(uid);
            return {true, (int)p->liked_by.size()};
        }
    }

    int addComment(int pid, int authorId, const std::string& authorName,
                   const std::string& text) {
        auto* p = getPost(pid);
        if (!p) return -1;
        int cid = next_comment_id_++;
        Comment c{cid, pid, authorId, authorName, text, now_iso()};
        p->comments.push_back(c);
        return cid;
    }

    /* Saved/Bookmarked posts */
    bool toggleSave(int pid, int uid) {
        if (!getPost(pid)) return false;
        if (saved_[uid].count(pid)) {
            saved_[uid].erase(pid);
            return false;  // unsaved
        } else {
            saved_[uid].insert(pid);
            return true;   // saved
        }
    }

    bool isSaved(int pid, int uid) const {
        auto it = saved_.find(uid);
        return it != saved_.end() && it->second.count(pid);
    }

    std::vector<Post*> getSavedPosts(int uid) {
        std::vector<Post*> out;
        auto it = saved_.find(uid);
        if (it == saved_.end()) return out;
        for (int pid : it->second) {
            Post* p = getPost(pid);
            if (p) out.push_back(p);
        }
        return out;
    }

    /* Get all images from all posts (for photos gallery) */
    std::vector<json> allImages() {
        std::vector<json> imgs;
        for (auto& p : posts_) {
            for (auto& img : p.images) {
                imgs.push_back({
                    {"src",         img},
                    {"post_id",     p.id},
                    {"author_name", p.author_name},
                    {"created_at",  p.created_at}
                });
            }
        }
        return imgs;
    }

private:
    std::vector<Post> posts_;
    std::unordered_map<int, std::set<int>> saved_;  // uid -> saved post IDs
    int next_id_         = 1;
    int next_comment_id_ = 1;
};

/* ════════════════════════════════════════════════════════════
   BIRTHDAY MANAGER
════════════════════════════════════════════════════════════ */
class BirthdayManager {
public:
    BirthdayManager() = default;

    void seed() {
        addBirthday(1, "Priya Sharma",  "2004-05-20", "🎂");
        addBirthday(1, "Raj Patel",     "2003-08-15", "👨");
        addBirthday(1, "Neha Singh",    "2004-01-08", "👩");
    }

    int addBirthday(int ownerId, const std::string& friendName,
                    const std::string& date, const std::string& emoji) {
        int bid = next_id_++;
        birthdays_.push_back({bid, ownerId, friendName, date, emoji});
        return bid;
    }

    std::vector<Birthday> getForUser(int uid) const {
        std::vector<Birthday> out;
        for (auto& b : birthdays_)
            if (b.owner_id == uid) out.push_back(b);
        return out;
    }

    bool deleteBirthday(int bid) {
        auto it = std::remove_if(birthdays_.begin(), birthdays_.end(),
                                 [bid](const Birthday& b){ return b.id == bid; });
        if (it == birthdays_.end()) return false;
        birthdays_.erase(it, birthdays_.end());
        return true;
    }

private:
    std::vector<Birthday> birthdays_;
    int next_id_ = 1;
};

/* ════════════════════════════════════════════════════════════
   EVENT MANAGER
════════════════════════════════════════════════════════════ */
class EventManager {
public:
    EventManager() = default;

    void seed() {
        Event e1;
        e1.id = next_id_++; e1.creator_id = 1; e1.creator_name = "Ronak Gupta";
        e1.title = "DSA Study Group"; e1.description = "Weekly Data Structures revision session";
        e1.date = "2026-04-20"; e1.time = "15:00"; e1.location = "Library, Block A";
        e1.emoji = "📚"; e1.created_at = now_iso();
        events_.push_back(e1);

        Event e2;
        e2.id = next_id_++; e2.creator_id = 2; e2.creator_name = "Amit Shukla";
        e2.title = "Campus Photography Walk"; e2.description = "Bring your cameras! Exploring the campus gardens.";
        e2.date = "2026-04-22"; e2.time = "07:00"; e2.location = "Main Gate";
        e2.emoji = "📸"; e2.created_at = now_iso();
        events_.push_back(e2);

        Event e3;
        e3.id = next_id_++; e3.creator_id = 3; e3.creator_name = "Mansi Gupta";
        e3.title = "Farewell Party Planning"; e3.description = "Let's plan the batch farewell event!";
        e3.date = "2026-05-01"; e3.time = "16:00"; e3.location = "Auditorium";
        e3.emoji = "🎉"; e3.created_at = now_iso();
        events_.push_back(e3);
    }

    int createEvent(int creatorId, const std::string& creatorName,
                    const std::string& title, const std::string& desc,
                    const std::string& date, const std::string& time,
                    const std::string& location, const std::string& emoji) {
        Event e;
        e.id = next_id_++; e.creator_id = creatorId; e.creator_name = creatorName;
        e.title = title; e.description = desc; e.date = date; e.time = time;
        e.location = location; e.emoji = emoji; e.created_at = now_iso();
        e.attendees.insert(creatorId);
        events_.push_back(e);
        return e.id;
    }

    std::vector<Event>& allEvents() { return events_; }

    Event* getEvent(int eid) {
        for (auto& e : events_) if (e.id == eid) return &e;
        return nullptr;
    }

    bool deleteEvent(int eid) {
        auto it = std::remove_if(events_.begin(), events_.end(),
                                 [eid](const Event& e){ return e.id == eid; });
        if (it == events_.end()) return false;
        events_.erase(it, events_.end());
        return true;
    }

    bool toggleAttend(int eid, int uid) {
        auto* e = getEvent(eid);
        if (!e) return false;
        if (e->attendees.count(uid)) {
            e->attendees.erase(uid);
            return false;
        } else {
            e->attendees.insert(uid);
            return true;
        }
    }

private:
    std::vector<Event> events_;
    int next_id_ = 1;
};

/* ════════════════════════════════════════════════════════════
   HELPER: extract Bearer token
════════════════════════════════════════════════════════════ */
static std::string extractToken(const httplib::Request& req) {
    auto it = req.headers.find("Authorization");
    if (it == req.headers.end()) return "";
    const std::string& val = it->second;
    if (val.rfind("Bearer ", 0) == 0) return val.substr(7);
    return val;
}

/* ════════════════════════════════════════════════════════════
   MAIN — wire everything together
════════════════════════════════════════════════════════════ */
int main() {
    std::cout << R"(
  ____                  _ _   _       
 | __ )  ___  _ __   __| | | | |_ __  
 |  _ \ / _ \| '_ \ / _` | | | | '_ \ 
 | |_) | (_) | | | | (_| | |_| | |_) |
 |____/ \___/|_| |_|\__,_|\___/| .__/ 
                                |_|    
  Friendship Network — C++ Backend
)" << std::endl;

    // Managers
    AuthManager     auth;
    FriendManager   friends;
    PostManager     posts;
    BirthdayManager birthdays;
    EventManager    events;

    friends.seed(auth);
    posts.seed();
    birthdays.seed();
    events.seed();

    httplib::Server svr;

    // Serve static files from parent directory
    svr.set_mount_point("/", "..");

    // CORS
    svr.Options(R"(.*)", [](const httplib::Request&, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin",  "*");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
        res.status = 204;
    });
    svr.set_post_routing_handler([](const httplib::Request&, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin",  "*");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
    });

    // Root -> frontend.html
    svr.Get("/", [](const httplib::Request&, httplib::Response& res) {
        std::ifstream f("../frontend.html", std::ios::binary);
        if (!f) { res.status = 404; return; }
        std::string body((std::istreambuf_iterator<char>(f)),
                          std::istreambuf_iterator<char>());
        res.set_content(body, "text/html");
    });

    /* ─── HEALTH ─────────────────────────────────────────── */
    svr.Get("/api/health", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(R"({"status":"ok","server":"BondUp C++ Backend"})", "application/json");
    });

    /* ─── AUTH: REGISTER ─────────────────────────────────── */
    svr.Post("/api/auth/register",
        [&auth](const httplib::Request& req, httplib::Response& res) {
        json body;
        try { body = json::parse(req.body); } catch (...) {
            res.status = 400; res.set_content(R"({"error":"Invalid JSON"})", "application/json"); return;
        }
        std::string uname   = body.value("username", "");
        std::string pw      = body.value("password", "");
        std::string display = body.value("display_name", "");
        std::string emoji   = body.value("avatar_emoji", "😊");
        std::string email   = body.value("email", "");

        if (uname.size() < 3) { res.status=400; res.set_content(R"({"error":"Username must be at least 3 characters"})", "application/json"); return; }
        if (pw.size() < 6)    { res.status=400; res.set_content(R"({"error":"Password must be at least 6 characters"})", "application/json"); return; }
        if (display.empty())  display = uname;
        if (auth.usernameExists(uname)) { res.status=409; res.set_content(R"({"error":"Username already taken"})", "application/json"); return; }

        auto& u = auth.addUser(uname, pw, display, emoji, email);
        std::string token = auth.createSession(u.id);

        json j;
        j["token"] = token;
        j["user"]  = {{"id", u.id}, {"username", u.username}, {"display_name", u.display_name}, {"avatar_emoji", u.avatar_emoji}};
        res.set_content(j.dump(), "application/json");
        std::cout << "[REGISTER] " << uname << std::endl;
    });

    /* ─── AUTH: LOGIN ────────────────────────────────────── */
    svr.Post("/api/auth/login",
        [&auth](const httplib::Request& req, httplib::Response& res) {
        json body;
        try { body = json::parse(req.body); } catch (...) {
            res.status = 400; res.set_content(R"({"error":"Invalid JSON"})", "application/json"); return;
        }
        std::string uname = body.value("username", "");
        std::string pw    = body.value("password", "");
        int uid = auth.login(uname, pw);
        if (uid < 0) { res.status = 401; res.set_content(R"({"error":"Invalid username or password"})", "application/json"); return; }

        std::string token = auth.createSession(uid);
        const User* u = auth.getUser(uid);
        json j;
        j["token"] = token;
        j["user"]  = {{"id", u->id}, {"username", u->username}, {"display_name", u->display_name}, {"avatar_emoji", u->avatar_emoji}};
        res.set_content(j.dump(), "application/json");
        std::cout << "[LOGIN] " << uname << std::endl;
    });

    /* ─── AUTH: LOGOUT ───────────────────────────────────── */
    svr.Post("/api/auth/logout",
        [&auth](const httplib::Request& req, httplib::Response& res) {
        auth.destroySession(extractToken(req));
        res.set_content(R"({"ok":true})", "application/json");
    });

    /* ─── AUTH: FORGOT PASSWORD ──────────────────────────── */
    svr.Post("/api/auth/forgot-password",
        [&auth](const httplib::Request& req, httplib::Response& res) {
        json body;
        try { body = json::parse(req.body); } catch (...) {
            res.status = 400; res.set_content(R"({"error":"Invalid JSON"})", "application/json"); return;
        }
        std::string email = body.value("email", "");
        if (email.empty()) { res.status = 400; res.set_content(R"({"error":"Email required"})", "application/json"); return; }
        bool found = auth.resetPassword(email);
        // Always respond success (don't reveal if email exists)
        json j; j["ok"] = true; j["message"] = "If this email is registered, the password has been reset to 'reset123'.";
        res.set_content(j.dump(), "application/json");
        if (found) std::cout << "[RESET] Password reset for " << email << std::endl;
    });

    /* ─── FRIENDS: SUGGESTIONS ───────────────────────────── */
    svr.Get("/api/friends/suggestions",
        [&auth, &friends](const httplib::Request& req, httplib::Response& res) {
        int uid = auth.resolve(extractToken(req));
        if (uid < 0) { res.status = 401; res.set_content(R"({"error":"Unauthorized"})", "application/json"); return; }

        auto ids = friends.suggestions(uid, auth.allUserIds());
        json arr = json::array();
        for (int id : ids) {
            const User* u = auth.getUser(id);
            if (!u) continue;
            arr.push_back({
                {"id", u->id}, {"username", u->username},
                {"display_name", u->display_name}, {"avatar_emoji", u->avatar_emoji},
                {"mutual_friends", friends.mutualCount(uid, id)}
            });
        }
        json j; j["suggestions"] = arr;
        res.set_content(j.dump(), "application/json");
    });

    /* ─── FRIENDS: MY LIST ───────────────────────────────── */
    svr.Get("/api/friends",
        [&auth, &friends](const httplib::Request& req, httplib::Response& res) {
        int uid = auth.resolve(extractToken(req));
        if (uid < 0) { res.status = 401; res.set_content(R"({"error":"Unauthorized"})", "application/json"); return; }

        auto fids = friends.getFriends(uid);
        json arr = json::array();
        for (int fid : fids) {
            const User* u = auth.getUser(fid);
            if (!u) continue;
            arr.push_back({
                {"id", u->id}, {"username", u->username},
                {"display_name", u->display_name}, {"avatar_emoji", u->avatar_emoji},
                {"mutual_friends", friends.mutualCount(uid, fid)}
            });
        }
        json j; j["friends"] = arr;
        res.set_content(j.dump(), "application/json");
    });

    /* ─── FRIENDS: SEND REQUEST ──────────────────────────── */
    svr.Post(R"(/api/friends/request/(\d+))",
        [&auth, &friends](const httplib::Request& req, httplib::Response& res) {
        int uid = auth.resolve(extractToken(req));
        if (uid < 0) { res.status=401; res.set_content(R"({"error":"Unauthorized"})", "application/json"); return; }
        int targetId = std::stoi(req.matches[1]);
        int rid = friends.sendRequest(uid, targetId);
        if (rid < 0) { res.status=400; res.set_content(R"({"error":"Already friends or request pending"})", "application/json"); return; }
        json j; j["ok"] = true; j["request_id"] = rid;
        res.set_content(j.dump(), "application/json");
    });

    /* ─── FRIENDS: PENDING REQUESTS ──────────────────────── */
    svr.Get("/api/friends/requests",
        [&auth, &friends](const httplib::Request& req, httplib::Response& res) {
        int uid = auth.resolve(extractToken(req));
        if (uid < 0) { res.status=401; res.set_content(R"({"error":"Unauthorized"})", "application/json"); return; }
        auto reqs = friends.pendingFor(uid);
        json arr = json::array();
        for (auto& r : reqs) {
            const User* u = auth.getUser(r.from_user_id);
            arr.push_back({
                {"request_id", r.id}, {"from_user_id", r.from_user_id},
                {"username", u ? u->username : ""}, {"display_name", u ? u->display_name : ""},
                {"avatar_emoji", u ? u->avatar_emoji : "😊"},
                {"friend_count", friends.friendCount(r.from_user_id)}
            });
        }
        json j; j["requests"] = arr;
        res.set_content(j.dump(), "application/json");
    });

    /* ─── FRIENDS: ACCEPT ────────────────────────────────── */
    svr.Post(R"(/api/friends/accept/(\d+))",
        [&auth, &friends](const httplib::Request& req, httplib::Response& res) {
        int uid = auth.resolve(extractToken(req));
        if (uid < 0) { res.status=401; res.set_content(R"({"error":"Unauthorized"})", "application/json"); return; }
        int reqId = std::stoi(req.matches[1]);
        if (friends.acceptRequest(reqId)) res.set_content(R"({"ok":true})", "application/json");
        else { res.status=404; res.set_content(R"({"error":"Request not found"})", "application/json"); }
    });

    /* ─── FRIENDS: DECLINE ───────────────────────────────── */
    svr.Post(R"(/api/friends/decline/(\d+))",
        [&auth, &friends](const httplib::Request& req, httplib::Response& res) {
        int uid = auth.resolve(extractToken(req));
        if (uid < 0) { res.status=401; res.set_content(R"({"error":"Unauthorized"})", "application/json"); return; }
        int reqId = std::stoi(req.matches[1]);
        if (friends.declineRequest(reqId)) res.set_content(R"({"ok":true})", "application/json");
        else { res.status=404; res.set_content(R"({"error":"Request not found"})", "application/json"); }
    });

    /* ─── FRIENDS: UNFRIEND ──────────────────────────────── */
    svr.Post(R"(/api/friends/remove/(\d+))",
        [&auth, &friends](const httplib::Request& req, httplib::Response& res) {
        int uid = auth.resolve(extractToken(req));
        if (uid < 0) { res.status=401; res.set_content(R"({"error":"Unauthorized"})", "application/json"); return; }
        int targetId = std::stoi(req.matches[1]);
        friends.removeFriendship(uid, targetId);
        res.set_content(R"({"ok":true})", "application/json");
    });

    /* ─── BIRTHDAYS: GET ALL ─────────────────────────────── */
    svr.Get("/api/birthdays",
        [&auth, &birthdays](const httplib::Request& req, httplib::Response& res) {
        int uid = auth.resolve(extractToken(req));
        if (uid < 0) { res.status=401; res.set_content(R"({"error":"Unauthorized"})", "application/json"); return; }
        auto list = birthdays.getForUser(uid);
        json arr = json::array();
        for (auto& b : list) arr.push_back({{"id",b.id},{"friend_name",b.friend_name},{"birthday",b.birthday},{"emoji",b.emoji}});
        json j; j["birthdays"] = arr;
        res.set_content(j.dump(), "application/json");
    });

    /* ─── BIRTHDAYS: ADD ─────────────────────────────────── */
    svr.Post("/api/birthdays",
        [&auth, &birthdays](const httplib::Request& req, httplib::Response& res) {
        int uid = auth.resolve(extractToken(req));
        if (uid < 0) { res.status=401; res.set_content(R"({"error":"Unauthorized"})", "application/json"); return; }
        json body; try { body = json::parse(req.body); } catch (...) { res.status=400; res.set_content(R"({"error":"Invalid JSON"})", "application/json"); return; }
        std::string name = body.value("friend_name",""), date = body.value("birthday",""), emoji = body.value("emoji","😊");
        if (name.empty()||date.empty()) { res.status=400; res.set_content(R"({"error":"friend_name and birthday required"})", "application/json"); return; }
        int bid = birthdays.addBirthday(uid, name, date, emoji);
        json j; j["ok"]=true; j["id"]=bid;
        res.set_content(j.dump(), "application/json");
    });

    /* ─── BIRTHDAYS: DELETE ──────────────────────────────── */
    svr.Delete(R"(/api/birthdays/(\d+))",
        [&auth, &birthdays](const httplib::Request& req, httplib::Response& res) {
        int uid = auth.resolve(extractToken(req));
        if (uid < 0) { res.status=401; res.set_content(R"({"error":"Unauthorized"})", "application/json"); return; }
        int bid = std::stoi(req.matches[1]);
        if (birthdays.deleteBirthday(bid)) res.set_content(R"({"ok":true})", "application/json");
        else { res.status=404; res.set_content(R"({"error":"Not found"})", "application/json"); }
    });

    /* ─── EVENTS: GET ALL ────────────────────────────────── */
    svr.Get("/api/events",
        [&auth, &events](const httplib::Request& req, httplib::Response& res) {
        int uid = auth.resolve(extractToken(req));
        if (uid < 0) { res.status=401; res.set_content(R"({"error":"Unauthorized"})", "application/json"); return; }
        json arr = json::array();
        for (auto& e : events.allEvents()) {
            arr.push_back({
                {"id", e.id}, {"creator_id", e.creator_id}, {"creator_name", e.creator_name},
                {"title", e.title}, {"description", e.description},
                {"date", e.date}, {"time", e.time}, {"location", e.location},
                {"emoji", e.emoji}, {"attendees", (int)e.attendees.size()},
                {"attending", e.attendees.count(uid)>0}, {"created_at", e.created_at}
            });
        }
        json j; j["events"] = arr;
        res.set_content(j.dump(), "application/json");
    });

    /* ─── EVENTS: CREATE ─────────────────────────────────── */
    svr.Post("/api/events",
        [&auth, &events](const httplib::Request& req, httplib::Response& res) {
        int uid = auth.resolve(extractToken(req));
        if (uid < 0) { res.status=401; res.set_content(R"({"error":"Unauthorized"})", "application/json"); return; }
        const User* u = auth.getUser(uid);
        json body; try { body = json::parse(req.body); } catch (...) { res.status=400; res.set_content(R"({"error":"Invalid JSON"})", "application/json"); return; }
        std::string title = body.value("title",""), desc = body.value("description",""),
                    date = body.value("date",""), time = body.value("time",""),
                    loc = body.value("location",""), emoji = body.value("emoji","🏷️");
        if (title.empty()||date.empty()) { res.status=400; res.set_content(R"({"error":"Title and date required"})", "application/json"); return; }
        int eid = events.createEvent(uid, u?u->display_name:"User", title, desc, date, time, loc, emoji);
        json j; j["ok"]=true; j["id"]=eid;
        res.set_content(j.dump(), "application/json");
    });

    /* ─── EVENTS: DELETE ─────────────────────────────────── */
    svr.Delete(R"(/api/events/(\d+))",
        [&auth, &events](const httplib::Request& req, httplib::Response& res) {
        int uid = auth.resolve(extractToken(req));
        if (uid < 0) { res.status=401; res.set_content(R"({"error":"Unauthorized"})", "application/json"); return; }
        int eid = std::stoi(req.matches[1]);
        if (events.deleteEvent(eid)) res.set_content(R"({"ok":true})", "application/json");
        else { res.status=404; res.set_content(R"({"error":"Not found"})", "application/json"); }
    });

    /* ─── EVENTS: TOGGLE ATTEND ──────────────────────────── */
    svr.Post(R"(/api/events/(\d+)/attend)",
        [&auth, &events](const httplib::Request& req, httplib::Response& res) {
        int uid = auth.resolve(extractToken(req));
        if (uid < 0) { res.status=401; res.set_content(R"({"error":"Unauthorized"})", "application/json"); return; }
        int eid = std::stoi(req.matches[1]);
        bool attending = events.toggleAttend(eid, uid);
        auto* e = events.getEvent(eid);
        json j; j["attending"]=attending; j["attendees"]= e ? (int)e->attendees.size() : 0;
        res.set_content(j.dump(), "application/json");
    });

    /* ─── POSTS: GET FEED ────────────────────────────────── */
    svr.Get("/api/posts",
        [&auth, &posts](const httplib::Request& req, httplib::Response& res) {
        int uid = auth.resolve(extractToken(req));
        if (uid < 0) { res.status=401; res.set_content(R"({"error":"Unauthorized"})", "application/json"); return; }
        json arr = json::array();
        for (auto& p : posts.allPosts()) {
            arr.push_back({
                {"id", p.id}, {"author_id", p.author_id}, {"author_name", p.author_name},
                {"text", p.text}, {"mood", p.mood}, {"type", p.type}, {"images", p.images},
                {"likes", (int)p.liked_by.size()}, {"liked_by_me", p.liked_by.count(uid)>0},
                {"comments", (int)p.comments.size()}, {"created_at", p.created_at},
                {"saved_by_me", posts.isSaved(p.id, uid)}
            });
        }
        json j; j["posts"] = arr;
        res.set_content(j.dump(), "application/json");
    });

    /* ─── POSTS: CREATE ──────────────────────────────────── */
    svr.Post("/api/posts",
        [&auth, &posts](const httplib::Request& req, httplib::Response& res) {
        int uid = auth.resolve(extractToken(req));
        if (uid < 0) { res.status=401; res.set_content(R"({"error":"Unauthorized"})", "application/json"); return; }
        const User* u = auth.getUser(uid);
        json body; try { body = json::parse(req.body); } catch (...) { res.status=400; res.set_content(R"({"error":"Invalid JSON"})", "application/json"); return; }
        std::string text = body.value("text",""), mood = body.value("mood","");
        std::vector<std::string> images;
        if (body.contains("images")&&body["images"].is_array())
            for (auto& img : body["images"]) images.push_back(img.get<std::string>());
        auto& p = posts.createPost(uid, u?u->display_name:"User", text, mood, images);
        json j; j["ok"]=true; j["post"]={{"id",p.id},{"created_at",p.created_at}};
        res.set_content(j.dump(), "application/json");
        std::cout << "[POST] #" << p.id << " by " << (u?u->display_name:"?") << std::endl;
    });

    /* ─── POSTS: DELETE ──────────────────────────────────── */
    svr.Delete(R"(/api/posts/(\d+))",
        [&auth, &posts](const httplib::Request& req, httplib::Response& res) {
        int uid = auth.resolve(extractToken(req));
        if (uid < 0) { res.status=401; res.set_content(R"({"error":"Unauthorized"})", "application/json"); return; }
        int pid = std::stoi(req.matches[1]);
        if (posts.deletePost(pid)) res.set_content(R"({"ok":true})", "application/json");
        else { res.status=404; res.set_content(R"({"error":"Not found"})", "application/json"); }
    });

    /* ─── POSTS: LIKE ────────────────────────────────────── */
    svr.Post(R"(/api/posts/(\d+)/like)",
        [&auth, &posts](const httplib::Request& req, httplib::Response& res) {
        int uid = auth.resolve(extractToken(req));
        if (uid < 0) { res.status=401; res.set_content(R"({"error":"Unauthorized"})", "application/json"); return; }
        int pid = std::stoi(req.matches[1]);
        auto [liked, count] = posts.toggleLike(pid, uid);
        json j; j["liked"]=liked; j["count"]=count;
        res.set_content(j.dump(), "application/json");
    });

    /* ─── POSTS: SAVE/BOOKMARK ───────────────────────────── */
    svr.Post(R"(/api/posts/(\d+)/save)",
        [&auth, &posts](const httplib::Request& req, httplib::Response& res) {
        int uid = auth.resolve(extractToken(req));
        if (uid < 0) { res.status=401; res.set_content(R"({"error":"Unauthorized"})", "application/json"); return; }
        int pid = std::stoi(req.matches[1]);
        bool saved = posts.toggleSave(pid, uid);
        json j; j["saved"]=saved;
        res.set_content(j.dump(), "application/json");
    });

    /* ─── POSTS: GET SAVED ───────────────────────────────── */
    svr.Get("/api/posts/saved",
        [&auth, &posts](const httplib::Request& req, httplib::Response& res) {
        int uid = auth.resolve(extractToken(req));
        if (uid < 0) { res.status=401; res.set_content(R"({"error":"Unauthorized"})", "application/json"); return; }
        auto saved = posts.getSavedPosts(uid);
        json arr = json::array();
        for (auto* p : saved) {
            arr.push_back({
                {"id", p->id}, {"author_id", p->author_id}, {"author_name", p->author_name},
                {"text", p->text}, {"mood", p->mood}, {"type", p->type}, {"images", p->images},
                {"likes", (int)p->liked_by.size()}, {"liked_by_me", p->liked_by.count(uid)>0},
                {"comments", (int)p->comments.size()}, {"created_at", p->created_at},
                {"saved_by_me", true}
            });
        }
        json j; j["posts"] = arr;
        res.set_content(j.dump(), "application/json");
    });

    /* ─── POSTS: PHOTOS GALLERY ──────────────────────────── */
    svr.Get("/api/photos",
        [&auth, &posts](const httplib::Request& req, httplib::Response& res) {
        int uid = auth.resolve(extractToken(req));
        if (uid < 0) { res.status=401; res.set_content(R"({"error":"Unauthorized"})", "application/json"); return; }
        json j; j["photos"] = posts.allImages();
        res.set_content(j.dump(), "application/json");
    });

    /* ─── POSTS: GET COMMENTS ────────────────────────────── */
    svr.Get(R"(/api/posts/(\d+)/comments)",
        [&auth, &posts](const httplib::Request& req, httplib::Response& res) {
        int uid = auth.resolve(extractToken(req));
        if (uid < 0) { res.status=401; res.set_content(R"({"error":"Unauthorized"})", "application/json"); return; }
        int pid = std::stoi(req.matches[1]);
        auto* p = posts.getPost(pid);
        if (!p) { res.status=404; res.set_content(R"({"error":"Not found"})", "application/json"); return; }
        json arr = json::array();
        for (auto& c : p->comments)
            arr.push_back({{"id",c.id},{"author_name",c.author_name},{"text",c.text},{"created_at",c.created_at}});
        json j; j["comments"] = arr;
        res.set_content(j.dump(), "application/json");
    });

    /* ─── POSTS: ADD COMMENT ─────────────────────────────── */
    svr.Post(R"(/api/posts/(\d+)/comment)",
        [&auth, &posts](const httplib::Request& req, httplib::Response& res) {
        int uid = auth.resolve(extractToken(req));
        if (uid < 0) { res.status=401; res.set_content(R"({"error":"Unauthorized"})", "application/json"); return; }
        const User* u = auth.getUser(uid);
        int pid = std::stoi(req.matches[1]);
        json body; try { body = json::parse(req.body); } catch (...) { res.status=400; res.set_content(R"({"error":"Invalid JSON"})", "application/json"); return; }
        std::string text = body.value("text","");
        if (text.empty()) { res.status=400; res.set_content(R"({"error":"Text required"})", "application/json"); return; }
        int cid = posts.addComment(pid, uid, u?u->display_name:"User", text);
        if (cid < 0) { res.status=404; res.set_content(R"({"error":"Post not found"})", "application/json"); return; }
        json j; j["ok"]=true; j["comment_id"]=cid;
        res.set_content(j.dump(), "application/json");
    });

    /* ─── START SERVER ───────────────────────────────────── */
    // Use PORT env variable for deployment (Render, Railway, etc.)
    int PORT = 8080;
    const char* envPort = std::getenv("PORT");
    if (envPort) PORT = std::atoi(envPort);

    std::cout << "  Server: http://localhost:" << PORT << "/" << std::endl;
    std::cout << "  Static: ../" << std::endl;
    std::cout << "  Accounts: ronak_gupta / password123  (all pw: password123)" << std::endl;
    std::cout << "  Press Ctrl+C to stop.\n" << std::endl;

    if (!svr.listen("0.0.0.0", PORT)) {
        std::cerr << "[FATAL] Failed to bind port " << PORT << std::endl;
        return 1;
    }
    return 0;
}
