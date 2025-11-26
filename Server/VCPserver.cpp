#include <csignal>
#include <atomic>
#include <iostream>
#include <mutex>
#include <fstream>
#include <string>

std::atomic<bool> server_running{true};
static std::mutex log_mutex;

void log_event(const std::string& msg) {
    std::lock_guard<std::mutex> lock(log_mutex);
    std::ofstream log("server.log", std::ios::app);
    if (log) {
        log << msg << std::endl;
    }
}

void handle_sigint(int) {
    std::cout << "\nSIGINT received. Shutting down server...\n";
    log_event("SIGINT received. Shutting down server...");
    server_running = false;
}
#include <iostream>
#include <fstream>
#include <string>
#include <iomanip>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <filesystem>
#include <arpa/inet.h>
#include <thread>
#include <atomic>
#include <regex>
#define MAX_CLIENTS 8


using namespace std;
namespace fs = std::filesystem;

#define SERVER_PORT 8080
#define BUFFER_SIZE 1024

#include <sqlite3.h>
#include <random>
#include <sstream>
#include <openssl/evp.h>
//lock 
static std::mutex db_mutex;

static sqlite3 *g_db = nullptr;

static string sha256_hex(const string &input) {
    EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
    if(!mdctx) return string();
    if(1 != EVP_DigestInit_ex(mdctx, EVP_sha256(), nullptr)) { EVP_MD_CTX_free(mdctx); return string(); }
    if(1 != EVP_DigestUpdate(mdctx, input.data(), input.size())) { EVP_MD_CTX_free(mdctx); return string(); }
    unsigned char md_value[EVP_MAX_MD_SIZE];
    unsigned int md_len = 0;
    if(1 != EVP_DigestFinal_ex(mdctx, md_value, &md_len)) { EVP_MD_CTX_free(mdctx); return string(); }
    EVP_MD_CTX_free(mdctx);
    stringstream ss;
    ss << hex << setfill('0');
    for(unsigned int i=0;i<md_len;++i) ss << setw(2) << (int)md_value[i];
    return ss.str();
}

static string random_hex(size_t len) {
    static std::random_device rd;
    static std::mt19937_64 gen(rd());
    static std::uniform_int_distribution<uint64_t> dis(0, UINT64_MAX);
    string out;
    out.reserve(len*2);
    while(out.size() < len*2) {
        uint64_t v = dis(gen);
        std::stringstream ss;
        ss << hex << setw(16) << setfill('0') << v;
        out += ss.str();
    }
    out.resize(len*2);
    return out;
}

static bool db_init(const string &path = "vcp_server.db") {
    int rc = sqlite3_open_v2(path.c_str(), &g_db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr);
    if(rc != SQLITE_OK) {
        return false;
    }
    sqlite3_busy_timeout(g_db, 5000);
    const char *create_users =
        "CREATE TABLE IF NOT EXISTS users("
        "user_id TEXT PRIMARY KEY,"
        "email TEXT UNIQUE,"
        "full_name TEXT,"
        "phone TEXT UNIQUE,"
        "password_hash TEXT,"
        "created_at TEXT)";
    const char *create_sessions =
        "CREATE TABLE IF NOT EXISTS sessions("
        "token TEXT PRIMARY KEY,"
        "user_id TEXT,"
        "created_at TEXT)";
    char *err = nullptr;
    if(sqlite3_exec(g_db, create_users, nullptr, nullptr, &err) != SQLITE_OK) {
        if(err) sqlite3_free(err);
        return false;
    }
    if(sqlite3_exec(g_db, create_sessions, nullptr, nullptr, &err) != SQLITE_OK) {
        if(err) sqlite3_free(err);
        return false;
    }
    return true;
}

static bool db_create_user(const string &user_id, const string &email, const string &full_name, const string &phone, const string &pw_hash, const string &created_at) {
    std::lock_guard<std::mutex> lock(db_mutex);
    const char *sql = "INSERT INTO users(user_id,email,full_name,phone,password_hash,created_at) VALUES(?,?,?,?,?,?)";
    sqlite3_stmt *stmt = nullptr;
    if(sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, user_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, email.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, full_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, phone.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, pw_hash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, created_at.c_str(), -1, SQLITE_TRANSIENT);
    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

static bool db_get_user_by_email(const string &email, string &out_user_id, string &out_pw_hash) {
    std::lock_guard<std::mutex> lock(db_mutex);
    const char *sql = "SELECT user_id,password_hash FROM users WHERE email = ? LIMIT 1";
    sqlite3_stmt *stmt = nullptr;
    if(sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, email.c_str(), -1, SQLITE_TRANSIENT);
    bool ok = false;
    if(sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char *uid = sqlite3_column_text(stmt, 0);
        const unsigned char *ph = sqlite3_column_text(stmt, 1);
        if(uid) out_user_id = reinterpret_cast<const char*>(uid);
        if(ph) out_pw_hash = reinterpret_cast<const char*>(ph);
        ok = true;
    }
    sqlite3_finalize(stmt);
    return ok;
}

static bool db_create_session(const string &token, const string &user_id, const string &created_at) {
    std::lock_guard<std::mutex> lock(db_mutex);
    const char *sql = "INSERT INTO sessions(token,user_id,created_at) VALUES(?,?,?)";
    sqlite3_stmt *stmt = nullptr;
    if(sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, token.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, user_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, created_at.c_str(), -1, SQLITE_TRANSIENT);
    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

static bool db_get_user_by_token(const string &token, string &out_user_id) {
    std::lock_guard<std::mutex> lock(db_mutex);
    const char *sql = "SELECT user_id FROM sessions WHERE token = ? LIMIT 1";
    sqlite3_stmt *stmt = nullptr;
    if(sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, token.c_str(), -1, SQLITE_TRANSIENT);
    bool ok = false;
    if(sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char *uid = sqlite3_column_text(stmt, 0);
        if(uid) out_user_id = reinterpret_cast<const char*>(uid);
        ok = true;
    }
    sqlite3_finalize(stmt);
    return ok;
}

static bool db_get_user_by_phone(const string &phone, string &out_user_id) {
    const char *sql = "SELECT user_id FROM users WHERE phone = ? LIMIT 1";
    sqlite3_stmt *stmt = nullptr;
    if(sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, phone.c_str(), -1, SQLITE_TRANSIENT);
    bool ok = false;
    if(sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char *uid = sqlite3_column_text(stmt, 0);
        if(uid) out_user_id = reinterpret_cast<const char*>(uid);
        ok = true;
    }
    sqlite3_finalize(stmt);
    return ok;
}

// Declarition of functions
bool handle_submit_request(int client_sock);
bool handle_clone_request(int client_sock);
bool handle_list_request(int client_sock);
bool handle_auth_request(int client_sock);

// Helper to receive exactly n bytes
bool recv_all(int sock, char *buffer, size_t len) {
    size_t total = 0;
    while (total < len) {
        ssize_t r = recv(sock, buffer + total, len - total, 0);
        if (r <= 0) return false;
        total += r;
    }
    return true;
}

// Helper to receive a length-prefixed string
bool receive_data(int sock, string &data) {
    uint32_t net_len;
    if (!recv_all(sock, reinterpret_cast<char*>(&net_len), sizeof(net_len))) return false;
    uint32_t len = ntohl(net_len);
    if (len == 0) { data = ""; return true; }
    std::vector<char> buffer(len + 1);
    if (!recv_all(sock, buffer.data(), len)) { return false; }
    buffer[len] = '\0';
    data = buffer.data();
    return true;
}

// Helper to send a uint32_t ack value
bool send_ack(int sock, uint32_t ack_val) {
    uint32_t net_val = htonl(ack_val);
    if (send(sock, &net_val, sizeof(net_val), 0) != sizeof(net_val))
        return false;
    return true;
}

// htonll and ntohll are available on macOS

// Helper to receive a file sent by client
bool receive_file(int sock, const string &save_path) {
    uint64_t net_size;
    if (!recv_all(sock, reinterpret_cast<char*>(&net_size), sizeof(net_size))) return false;
    uint64_t file_size = ntohll(net_size);
    fs::path outpath(save_path);
    fs::path parent = outpath.parent_path();
    if(!parent.empty() && !fs::exists(parent)) {
        try {
            fs::create_directories(parent);
        } catch(const fs::filesystem_error &e) {
            cerr << "Error creating directories: " << e.what() << "\n";
            return false;
        }
    }

    ofstream outfile(save_path, ios::binary);
    if (!outfile) {
        cerr << "Error: Cannot open file for writing: " << save_path << "\n";
        return false;
    }

    char buffer[BUFFER_SIZE];
    uint64_t remaining = file_size;
    uint64_t total = file_size;
    uint64_t received = 0;
    while (remaining > 0) {
        size_t to_read = (remaining < BUFFER_SIZE) ? remaining : BUFFER_SIZE;
        ssize_t r = recv(sock, buffer, to_read, 0);
        if (r <= 0) return false;
        outfile.write(buffer, r);
        remaining -= r;
        received += r;
        double pct = (total > 0) ? (100.0 * received / total) : 100.0;
        cout << "Receiving: " << save_path << " - " << received << "/" << total
             << " bytes (" << fixed << setprecision(1) << pct << "% )\r";
        cout.flush();
    }
    cout << endl;
    outfile.close();
    return true;
}

bool send_file_to_client(int sock, const string &file_path) {
    ifstream file(file_path, ios::binary | ios::ate);
    if(!file) {
        cerr << "Cannot open file for sending: " << file_path << endl;
        return false;
    }

    uint64_t file_size = file.tellg();
    file.seekg(0);

    // Send file size
    uint64_t net_size = htonll(file_size);
    if(send(sock, &net_size, sizeof(net_size), 0) != sizeof(net_size)) {
        return false;
    }

    // Send file content
    char buffer[BUFFER_SIZE];
    uint64_t total = file_size;
    uint64_t sent = 0;
    while(file_size > 0) {
        size_t read_size = (file_size < BUFFER_SIZE) ? file_size : BUFFER_SIZE;
        file.read(buffer, read_size);
        if(send(sock, buffer, read_size, 0) != (ssize_t)read_size) {
            return false;
        }
        file_size -= read_size;
        sent += read_size;
        double pct = (total > 0) ? (100.0 * sent / total) : 100.0;
        cout << "Sending: " << file_path << " - " << sent << "/" << total
             << " bytes (" << fixed << setprecision(1) << pct << "% )\r";
        cout.flush();
    }
    cout << endl;

    return true;
}


bool send_string_to_client(int sock, const string &str) {
    uint32_t len = htonl(str.size());
    if(send(sock, &len, sizeof(len), 0) != sizeof(len)) {
        return false;
    }
    if(str.size() > 0) {
        if(send(sock, str.c_str(), str.size(), 0) != (ssize_t)str.size()) {
            return false;
        }
    }
    return true;
}

// Clone project from server
bool handle_clone_request(int client_sock) {
    string project_name;
    if(!receive_data(client_sock, project_name)) {
        cerr << "Failed to receive project name for clone\n";
        return false;
    }

    cout << "Clone request for project: " << project_name << endl;

    // Validate project name
    std::regex valid_name("^[A-Za-z0-9._-]{1,100}$");
    if (!std::regex_match(project_name, valid_name)) {
        cerr << "Invalid project name for clone: " << project_name << endl;
        send_ack(client_sock, 0);
        return false;
    }

    // Check if project exists
    if(!fs::exists(project_name) || !fs::is_directory(project_name)) {
        cerr << "Project '" << project_name << "' not found\n";
        send_ack(client_sock, 0); // Send failure
        return false;
    }
    // Send ack for project name
    if(!send_ack(client_sock, 1)) {
        cerr << "Failed to send clone ack\n";
        return false;
    }

    // Send Recursive directory contents
    try {
        for(const auto& entry : fs::recursive_directory_iterator(project_name)) {
            if(fs::is_regular_file(entry)) {
                string rel_path = fs::relative(entry.path(), project_name).string();
                cout << "Sending file: " << rel_path << endl;
                
                if(!send_string_to_client(client_sock, rel_path)) {
                    cerr << "Failed to send filename: " << rel_path << endl;
                    return false;
                }
                
                if(!send_file_to_client(client_sock, entry.path().string())) {
                    cerr << "Failed to send file: " << rel_path << endl;
                    return false;
                }
            }
        }
    } catch(const fs::filesystem_error& e) {
        cerr << "Error accessing project files: " << e.what() << endl;
        return false;
    }

    uint32_t end_marker = htonl(0);
    if(send(client_sock, &end_marker, sizeof(end_marker), 0) != sizeof(end_marker)) {
        cerr << "Failed to send end marker\n";
        return false;
    }

    cout << "Clone completed for project: " << project_name << endl;
    return true;
}

bool handle_list_request(int client_sock) {
    cout << "List request received\n";
    // Expect a session token (may be empty) next from client
    string token;
    if(!receive_data(client_sock, token)) {
        cerr << "Failed to receive session token for LIST\n";
        return false;
    }

    try {
        if(token.empty()) {
            cout << "Unauthenticated LIST request - no projects will be returned\n";
        } else {
            string user_id;
            if(!db_get_user_by_token(token, user_id)) {
                cerr << "Invalid session token for LIST\n";
            } else {
                fs::path user_root = fs::path("users") / user_id;
                if(fs::exists(user_root) && fs::is_directory(user_root)) {
                    for(const auto &entry : fs::recursive_directory_iterator(user_root)) {
                        if(fs::is_regular_file(entry)) {
                            string rel = fs::relative(entry.path(), user_root).string();
                            cout << "Sending user file: " << rel << endl;
                            if(!send_string_to_client(client_sock, rel)) {
                                cerr << "Failed to send user file: " << rel << endl;
                                return false;
                            }
                        }
                    }
                }
            }
        }
    } catch(const fs::filesystem_error& e) {
        cerr << "Error listing projects: " << e.what() << endl;
        return false;
    }

    uint32_t end_marker = htonl(0);
    if(send(client_sock, &end_marker, sizeof(end_marker), 0) != sizeof(end_marker)) {
        cerr << "Failed to send end marker\n";
        return false;
    }

    cout << "Project list sent successfully\n";
    return true;
}

bool handle_auth_request(int client_sock) {
    cout << "Auth request received\n";
    string sub;
    if(!receive_data(client_sock, sub)) {
        cerr << "Failed to receive auth subcommand\n";
        send_ack(client_sock, 0);
        return false;
    }
    if(sub == "SIGNUP") {
        string email, full_name, password, phone;
        if(!receive_data(client_sock, email) || !receive_data(client_sock, full_name) || !receive_data(client_sock, password) || !receive_data(client_sock, phone)) {
            cerr << "Incomplete signup data\n";
            send_ack(client_sock, 0);
            return false;
        }
        string tmp_uid, tmp_hash;
        if(db_get_user_by_email(email, tmp_uid, tmp_hash)) {
            send_ack(client_sock, 0);
            send_string_to_client(client_sock, "Email already in use");
            return false;
        }
        string tmp_uid2;
        if(db_get_user_by_phone(phone, tmp_uid2)) {
            send_ack(client_sock, 0);
            send_string_to_client(client_sock, "Phone number already in use");
            return false;
        }
        // generate user id
        string user_id = "u_" + random_hex(12);
        string pw_hash = sha256_hex(password);
        auto now = chrono::system_clock::now();
        time_t now_c = chrono::system_clock::to_time_t(now);
        string created = ctime(&now_c);
        if(!created.empty() && created.back() == '\n') created.pop_back();
        if(!db_create_user(user_id, email, full_name, phone, pw_hash, created)) {
            send_ack(client_sock, 0);
            send_string_to_client(client_sock, "Failed to create user (db error)");
            return false;
        }
        // create user folder
        fs::path user_root = fs::path("users") / user_id;
        try { fs::create_directories(user_root); } catch(...) {}
        
        string token = random_hex(16);
        if(!db_create_session(token, user_id, created)) {
            send_ack(client_sock, 0);
            send_string_to_client(client_sock, "Failed to create session");
            return false;
        }
        send_ack(client_sock, 1);
        send_string_to_client(client_sock, token);
        cout << "Created new user: " << email << " -> " << user_id << endl;
        return true;
    }
    else if(sub == "LOGIN") {
        string email, password;
        if(!receive_data(client_sock, email) || !receive_data(client_sock, password)) {
            cerr << "Incomplete login data\n";
            send_ack(client_sock, 0);
            return false;
        }
        string user_id, pw_hash;
        if(!db_get_user_by_email(email, user_id, pw_hash)) {
            send_ack(client_sock, 0);
            send_string_to_client(client_sock, "Unknown email or wrong password");
            return false;
        }
        if(sha256_hex(password) != pw_hash) {
            send_ack(client_sock, 0);
            send_string_to_client(client_sock, "Unknown email or wrong password");
            return false;
        }
        auto now = chrono::system_clock::now();
        time_t now_c = chrono::system_clock::to_time_t(now);
        string created = ctime(&now_c);
        if(!created.empty() && created.back() == '\n') created.pop_back();
        string token = random_hex(16);
        if(!db_create_session(token, user_id, created)) {
            send_ack(client_sock, 0);
            send_string_to_client(client_sock, "Failed to create session");
            return false;
        }
        send_ack(client_sock, 1);
        send_string_to_client(client_sock, token);
        cout << "Login success for " << email << " -> " << user_id << endl;
        return true;
    }
    else {
        cerr << "Unknown auth subcommand: " << sub << endl;
        send_ack(client_sock, 0);
        return false;
    }
}

int main() {
        struct sigaction sa;
        sa.sa_handler = handle_sigint;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0; // do NOT set SA_RESTART
        if (sigaction(SIGINT, &sa, nullptr) != 0) {
            cerr << "Failed to install SIGINT handler\n";
        }
    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        cerr << "Error creating server socket.\n";
        return 1;
    }

    // Allow address reuse
    int opt = 1;
    if (setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        cerr << "setsockopt failed\n";
        close(server_sock);
        return 1;
    }

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (::bind(server_sock, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)) < 0) {
        cerr << "Bind failed.\n";
        close(server_sock);
        return 1;
    }

    if (listen(server_sock, 1) < 0) {
        cerr << "Listen failed.\n";
        close(server_sock);
        return 1;
    }

    // Initialize SQLite DB for auth
    if(!db_init("vcp_server.db")) {
        cerr << "Warning: Failed to initialize SQLite DB (auth disabled)\n";
    }

    cout << "Server listening on port " << SERVER_PORT << "...\n";

    std::atomic<int> client_count{0};
    auto client_handler = [&](int client_sock) {
        client_count++;
        std::string connect_msg = "Client connected. Active clients: " + std::to_string(client_count);
        cout << connect_msg << "\n";
        log_event(connect_msg);
        string command;
        if (!receive_data(client_sock, command)) {
            std::string err_msg = "Failed to receive command.";
            cerr << err_msg << "\n";
            log_event(err_msg);
            close(client_sock);
            client_count--;
            return;
        }
        std::string cmd_msg = "Received command: " + command;
        cout << cmd_msg << "\n";
        log_event(cmd_msg);
        if (command == "SUBMIT") {
            handle_submit_request(client_sock);
        } else if (command == "CLONE") {
            handle_clone_request(client_sock);
        } else if (command == "AUTH") {
            handle_auth_request(client_sock);
        } else if (command == "LIST") {
            handle_list_request(client_sock);
        } else {
            std::string unknown_msg = "Unknown command: " + command;
            cerr << unknown_msg << "\n";
            log_event(unknown_msg);
            send_ack(client_sock, 0);
        }
        close(client_sock);
        std::string disconnect_msg = "Connection closed. Active clients: " + std::to_string(client_count - 1);
        cout << disconnect_msg << "\n";
        log_event(disconnect_msg);
        client_count--;
    };

    while (server_running) {
        try {
            if (client_count >= MAX_CLIENTS) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            int client_sock = accept(server_sock, nullptr, nullptr);
            if (!server_running) {
                if (client_sock >= 0) close(client_sock);
                break;
            }
            if (client_sock < 0) {
                if (errno == EINTR && !server_running) break;
                cerr << "Failed to accept client connection.\n";
                log_event("Failed to accept client connection.");
                continue;
            }
            std::thread([&, client_sock]() {
                try {
                    client_handler(client_sock);
                } catch (const std::exception& e) {
                    std::string err = std::string("Exception in client handler: ") + e.what();
                    cerr << err << "\n";
                    log_event(err);
                    close(client_sock);
                    client_count--;
                } catch (...) {
                    std::string err = "Unknown exception in client handler.";
                    cerr << err << "\n";
                    log_event(err);
                    close(client_sock);
                    client_count--;
                }
            }).detach();
        } catch (const std::exception& e) {
            std::string err = std::string("Exception in main accept loop: ") + e.what();
            cerr << err << "\n";
            log_event(err);
        } catch (...) {
            std::string err = "Unknown exception in main accept loop.";
            cerr << err << "\n";
            log_event(err);
        }
    }
    close(server_sock);
    log_event("Server socket closed. Exiting main.");
    std::cout << "Server shut down.\n";
    return 0;
}

bool handle_submit_request(int client_sock) {
    string session_token;
    if(!receive_data(client_sock, session_token)) {
        cerr << "Failed to receive session token for SUBMIT.\n";
        send_ack(client_sock, 0);
        return false;
    }
    string user_id;
    if(session_token.empty() || !db_get_user_by_token(session_token, user_id)) {
        cerr << "Invalid or missing session token for SUBMIT\n";
        send_ack(client_sock, 0);
        return false;
    }

    string project_name;
    if (!receive_data(client_sock, project_name)) {
        cerr << "Failed to receive project name.\n";
        return false;
    }
    cout << "Received project name: " << project_name << "\n";

    std::regex valid_name("^[A-Za-z0-9._-]{1,100}$");
    if (!std::regex_match(project_name, valid_name)) {
        cerr << "Invalid project name received: " << project_name << "\n";
        send_ack(client_sock, 0);
        return false;
    }

    // Save under users/<user_id>/<project_name>/...
    fs::path base = fs::path("users") / user_id / project_name;
    try {
        fs::create_directories(base);
    } catch(const fs::filesystem_error &e) {
        cerr << "Failed to create user project directory: " << e.what() << "\n";
        send_ack(client_sock, 0);
        return false;
    }
    // Send ack for project name (and token validation)
    if (!send_ack(client_sock, 1)) {
        cerr << "Failed to send ack for project name.\n";
        return false;
    }

    // Step 2: Receive files until an empty filename is received.
    while (true) {
        string filepath;
        if (!receive_data(client_sock, filepath)) {
            cerr << "Failed to receive file name.\n";
            break;
        }
        // An empty string signals end-of-transfer.
        if (filepath.empty()) break;
        // Reject absolute paths or parent traversal attempts
        if (fs::path(filepath).is_absolute() || filepath.find("..") != string::npos) {
            cerr << "Rejected unsafe filepath: " << filepath << "\n";
            send_ack(client_sock, 0);
            continue;
        }

        // Preserve relative path when saving under the user's project directory
        fs::path save_path = base / fs::path(filepath);
        cout << "Receiving file: " << filepath << " -> " << save_path.string() << "\n";
        if (receive_file(client_sock, save_path.string())) {
            if (!send_ack(client_sock, 1)) {
                cerr << "Failed to send ack for file: " << filepath << "\n";
                break;
            }
        } else {
            cerr << "Error receiving file: " << filepath << "\n";
            send_ack(client_sock, 0); // indicate error
            break;
        }
    }

    // Final ack after all files received.
    if (!send_ack(client_sock, 1))
        cerr << "Failed to send final ack.\n";

    return true;
}
