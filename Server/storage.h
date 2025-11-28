#pragma once

#include <string>

bool receive_file(int sock, const std::string &save_path);
bool send_file_to_client(int sock, const std::string &file_path);

bool handle_clone_request(int client_sock);
bool handle_list_request(int client_sock);
bool handle_submit_request(int client_sock);
