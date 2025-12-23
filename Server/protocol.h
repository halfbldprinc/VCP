#pragma once

#include <cstdint>
#include <string>

enum class ProtocolCommand {
    SUBMIT,
    CLONE,
    LIST,
    AUTH,
    UNKNOWN
};

ProtocolCommand parse_command(const std::string &cmd_str);

bool recv_all(int sock, char *buffer, size_t len);
bool receive_data(int sock, std::string &data);
bool send_string_to_client(int sock, const std::string &str);
bool send_ack(int sock, uint32_t ack_val);
