#pragma once

#include <string>

bool db_init(const std::string &db_path);
void db_close();

bool db_create_user(const std::string &email, const std::string &name, const std::string &pw_hash, std::string &out_user_id, const std::string &phone, const std::string &created);
bool db_get_user_by_email(const std::string &email, std::string &out_user_id, std::string &out_pw_hash);

bool db_create_session(const std::string &token, const std::string &user_id, const std::string &created);
bool db_get_user_by_token(const std::string &token, std::string &out_user_id);

bool handle_auth_request(int client_sock);

std::string sha256_hex(const std::string &input);
std::string random_hex(size_t len);
