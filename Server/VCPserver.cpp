#include <iostream>
#include <fstream>
#include <string>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <filesystem>
#include <arpa/inet.h>

using namespace std;
namespace fs = std::filesystem;

#define SERVER_PORT 8080
#define BUFFER_SIZE 1024

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
    char *buffer = new char[len + 1];
    if (!recv_all(sock, buffer, len)) { delete[] buffer; return false; }
    buffer[len] = '\0';
    data = buffer;
    delete[] buffer;
    return true;
}

// Helper to send a uint32_t ack value
bool send_ack(int sock, uint32_t ack_val) {
    uint32_t net_val = htonl(ack_val);
    if (send(sock, &net_val, sizeof(net_val), 0) != sizeof(net_val))
        return false;
    return true;
}

// // Since htonll and ntohll are not standard, define them here:
// #if defined(__APPLE__) || defined(__MACH__) || defined(__linux__)
// static inline uint64_t htonll(uint64_t value) {
//     // Use htonl on lower and upper 32 bits.
//     return ((uint64_t)htonl(value & 0xFFFFFFFF) << 32) | htonl(value >> 32);
// }

// static inline uint64_t ntohll(uint64_t value) {
//     return ((uint64_t)ntohl(value & 0xFFFFFFFF) << 32) | ntohl(value >> 32);
// }
// #endif

// Helper to receive a file sent by client
bool receive_file(int sock, const string &save_path) {
    uint64_t net_size;
    if (!recv_all(sock, reinterpret_cast<char*>(&net_size), sizeof(net_size))) return false;
    uint64_t file_size = ntohll(net_size);

    ofstream outfile(save_path, ios::binary);
    if (!outfile) {
        cerr << "Error: Cannot open file for writing: " << save_path << "\n";
        return false;
    }

    char buffer[BUFFER_SIZE];
    uint64_t remaining = file_size;
    while (remaining > 0) {
        size_t to_read = (remaining < BUFFER_SIZE) ? remaining : BUFFER_SIZE;
        ssize_t r = recv(sock, buffer, to_read, 0);
        if (r <= 0) return false;
        outfile.write(buffer, r);
        remaining -= r;
    }
    outfile.close();
    return true;
}

int main() {
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

    cout << "Server listening on port " << SERVER_PORT << "...\n";

    int client_sock = accept(server_sock, nullptr, nullptr);
    if (client_sock < 0) {
        cerr << "Failed to accept client connection.\n";
        close(server_sock);
        return 1;
    }
    cout << "Client connected.\n";

    // Step 1: Receive project name and create folder
    string project_name;
    if (!receive_data(client_sock, project_name)) {
        cerr << "Failed to receive project name.\n";
        close(client_sock);
        close(server_sock);
        return 1;
    }
    cout << "Received project name: " << project_name << "\n";

    if (!fs::exists(project_name)) {
        if (!fs::create_directory(project_name)) {
            cerr << "Failed to create directory: " << project_name << "\n";
            close(client_sock);
            close(server_sock);
            return 1;
        }
    }
    // Send ack for project name
    if (!send_ack(client_sock, 1)) {
        cerr << "Failed to send ack for project name.\n";
        close(client_sock);
        close(server_sock);
        return 1;
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

        // Save file inside the project folder using only the filename.
        fs::path p(filepath);
        string filename = p.filename().string();
        string save_path = project_name + "/" + filename;
        cout << "Receiving file: " << filepath << " -> " << save_path << "\n";
        if (receive_file(client_sock, save_path)) {
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

    close(client_sock);
    close(server_sock);
    cout << "Connection closed.\n";
    return 0;
}
