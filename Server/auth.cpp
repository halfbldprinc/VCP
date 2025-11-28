#include "auth.h"
#include "protocol.h"
#include "logging.h"

#include <sqlite3.h>
#include <openssl/evp.h>
#include <random>
#include <sstream>
#include <iomanip>
#include <mutex>
#include <chrono>
#include <filesystem>

#include <iostream>

static sqlite3 *g_db = nullptr;
static std::mutex db_mutex;

bool db_init(const std::string &db_path) {
    std::lock_guard<std::mutex> lock(db_mutex);
    if (g_db) return true; // already initialized
    if (sqlite3_open(db_path.c_str(), &g_db) != SQLITE_OK) {
        g_db = nullptr;
        return false;
    }
    const char *sql = R"(CREATE TABLE IF NOT EXISTS users (
        id TEXT PRIMARY KEY,
        email TEXT UNIQUE,
        pw_hash TEXT,
        name TEXT,
        phone TEXT,
        created TEXT
    );
    CREATE TABLE IF NOT EXISTS sessions (
        token TEXT PRIMARY KEY,
        user_id TEXT,
        created TEXT
    );)";

    char *err = nullptr;
    if (sqlite3_exec(g_db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
        if (err) sqlite3_free(err);
        return false;
    }
    return true;
}

void db_close() {
    std::lock_guard<std::mutex> lock(db_mutex);
    if (g_db) {
        sqlite3_close(g_db);
        g_db = nullptr;
    }
}

static std::string make_id() {
    return random_hex(16);
}

bool db_create_user(const std::string &email, const std::string &name, const std::string &pw_hash, std::string &out_user_id, const std::string &phone, const std::string &created) {
    std::lock_guard<std::mutex> lock(db_mutex);
    if (!g_db) return false;
    std::string user_id = make_id();
    const char *sql = "INSERT INTO users (id, email, pw_hash, name, phone, created) VALUES (?, ?, ?, ?, ?, ?);";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, user_id.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, email.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, pw_hash.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, name.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 5, phone.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 6, created.c_str(), -1, SQLITE_STATIC);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) return false;
    out_user_id = user_id;
    return true;
}

bool db_get_user_by_email(const std::string &email, std::string &out_user_id, std::string &out_pw_hash) {
    std::lock_guard<std::mutex> lock(db_mutex);
    if (!g_db) return false;
    const char *sql = "SELECT id, pw_hash FROM users WHERE email = ? LIMIT 1;";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, email.c_str(), -1, SQLITE_STATIC);
    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        const unsigned char *id = sqlite3_column_text(stmt, 0);
        const unsigned char *hash = sqlite3_column_text(stmt, 1);
        out_user_id = id ? reinterpret_cast<const char*>(id) : "";
        out_pw_hash = hash ? reinterpret_cast<const char*>(hash) : "";
        sqlite3_finalize(stmt);
        return true;
    }
    sqlite3_finalize(stmt);
    return false;
}

bool db_create_session(const std::string &token, const std::string &user_id, const std::string &created) {
    std::lock_guard<std::mutex> lock(db_mutex);
    if(!g_db) return false;
    const char *sql = "INSERT OR REPLACE INTO sessions (token, user_id, created) VALUES (?, ?, ?);";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, token.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, user_id.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, created.c_str(), -1, SQLITE_STATIC);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) return false;
    return true;
}

bool db_get_user_by_token(const std::string &token, std::string &out_user_id) {
    std::lock_guard<std::mutex> lock(db_mutex);
    if (!g_db) return false;
    const char *sql = "SELECT user_id FROM sessions WHERE token = ? LIMIT 1;";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, token.c_str(), -1, SQLITE_STATIC);
    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        const unsigned char *uid = sqlite3_column_text(stmt, 0);
        out_user_id = uid ? reinterpret_cast<const char*>(uid) : "";
        sqlite3_finalize(stmt);
        return true;
    }
    sqlite3_finalize(stmt);
    return false;
}


std::string sha256_hex(const std::string &input) {
    EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
    if(!mdctx) return std::string();
    if(1 != EVP_DigestInit_ex(mdctx, EVP_sha256(), nullptr)) { EVP_MD_CTX_free(mdctx); return std::string(); }
    if(1 != EVP_DigestUpdate(mdctx, input.data(), input.size())) { EVP_MD_CTX_free(mdctx); return std::string(); }
    unsigned char md_value[EVP_MAX_MD_SIZE];
    unsigned int md_len = 0;
    if(1 != EVP_DigestFinal_ex(mdctx, md_value, &md_len)) { EVP_MD_CTX_free(mdctx); return std::string(); }
    EVP_MD_CTX_free(mdctx);
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    for(unsigned int i=0;i<md_len;++i) ss << std::setw(2) << (int)md_value[i];
    return ss.str();
}

std::string random_hex(size_t len) {
    static std::random_device rd;
    static std::mt19937_64 gen(rd());
    static std::uniform_int_distribution<uint64_t> dis(0, UINT64_MAX);
    std::string out;
    out.reserve(len*2);
    while(out.size() < len*2) {
        uint64_t v = dis(gen);
        std::stringstream ss;
        ss << std::hex << std::setw(16) << std::setfill('0') << v;
        out += ss.str();
    }
    out.resize(len*2);
    return out;
}

bool handle_auth_request(int client_sock) {
    std::string sub;
    if (!receive_data(client_sock, sub)) {
        std::cerr << "Failed to receive auth subcommand\n";
        send_ack(client_sock, 0);
        return false;
    }
    if (sub == "SIGNUP") {
        std::string email, name, password, phone;
        if(!receive_data(client_sock, email) || !receive_data(client_sock, name) || !receive_data(client_sock, password) || !receive_data(client_sock, phone)) {
            std::cerr << "Incomplete signup data\n";
            send_ack(client_sock, 0);
            return false;
        }
        std::string pw_hash = sha256_hex(password);
        auto now = std::chrono::system_clock::now();
        time_t now_c = std::chrono::system_clock::to_time_t(now);
        std::string created = std::ctime(&now_c);
        if(!created.empty() && created.back()=='\n') created.pop_back();
        std::string user_id;
        if(!db_create_user(email, name, pw_hash, user_id, phone, created)) {
            send_ack(client_sock, 0);
            send_string_to_client(client_sock, "Failed to create user");
            return false;
        }
        std::string token = random_hex(16);
        if(!db_create_session(token, user_id, created)) {
            send_ack(client_sock, 0);
            send_string_to_client(client_sock, "Failed to create session");
            return false;
        }
        send_ack(client_sock, 1);
        send_string_to_client(client_sock, token);
        std::cout << "Created new user: " << email << " -> " << user_id << std::endl;
        return true;
    } else if (sub == "LOGIN") {
        std::string email, password;
        if(!receive_data(client_sock, email) || !receive_data(client_sock, password)) {
            std::cerr << "Incomplete login data\n";
            send_ack(client_sock, 0);
            return false;
        }
        std::string user_id, pw_hash;
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
        auto now = std::chrono::system_clock::now();
        time_t now_c = std::chrono::system_clock::to_time_t(now);
        std::string created = std::ctime(&now_c);
        if(!created.empty() && created.back()=='\n') created.pop_back();
        std::string token = random_hex(16);
        if(!db_create_session(token, user_id, created)) {
            send_ack(client_sock, 0);
            send_string_to_client(client_sock, "Failed to create session");
            return false;
        }
        send_ack(client_sock, 1);
        send_string_to_client(client_sock, token);
        std::cout << "Login success for " << email << " -> " << user_id << std::endl;
        return true;
    } else {
        std::cerr << "Unknown auth subcommand: " << sub << std::endl;
        send_ack(client_sock, 0);
        return false;
    }
}
