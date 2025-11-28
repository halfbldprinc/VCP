#include "protocol.h"
#include <string>
#include <vector>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <algorithm>
#include <cctype> 

using namespace std; 

ProtocolCommand parse_command(const string &cmd_str) {
    string s = cmd_str;
    transform(s.begin(), s.end(), s.begin(), ::toupper); 
    if (s == "SUBMIT") return ProtocolCommand::SUBMIT;
    if (s == "CLONE")  return ProtocolCommand::CLONE;
    if (s == "LIST")   return ProtocolCommand::LIST;
    if (s == "AUTH")   return ProtocolCommand::AUTH;
    return ProtocolCommand::UNKNOWN;
}

bool recv_all(int sock, char *buffer, size_t len) {
    size_t total = 0;
    while (total < len) {
        ssize_t r = recv(sock, buffer + total, len - total, 0);
        if (r <= 0) return false; 
        total += r;
    }
    return true;
}


bool receive_data(int sock, string &data) {
    uint32_t net_len;
    if (!recv_all(sock, reinterpret_cast<char*>(&net_len), sizeof(net_len)))
        return false;

    uint32_t len = ntohl(net_len); 
    if (len == 0) { data = ""; return true; }

    vector<char> buffer(len + 1);
    if (!recv_all(sock, buffer.data(), len)) return false;

    buffer[len] = '\0';
    data = buffer.data();
    return true;
}

bool send_string_to_client(int sock, const string &str) {
    uint32_t net_len = htonl(str.size());
    if (send(sock, &net_len, sizeof(net_len), 0) != sizeof(net_len)) return false;

    if (!str.empty()) {
        if (send(sock, str.c_str(), str.size(), 0) != (ssize_t)str.size())
            return false;
    }
    return true;
}


bool send_ack(int sock, uint32_t ack_val) {
    uint32_t net_val = htonl(ack_val);
    if (send(sock, &net_val, sizeof(net_val), 0) != sizeof(net_val))
        return false;
    return true;
}
