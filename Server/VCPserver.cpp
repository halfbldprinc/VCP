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

// Declarition of functions
bool handle_submit_request(int client_sock);
bool handle_clone_request(int client_sock);
bool handle_list_request(int client_sock);

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
    while(file_size > 0) {
        size_t read_size = (file_size < BUFFER_SIZE) ? file_size : BUFFER_SIZE;
        file.read(buffer, read_size);
        if(send(sock, buffer, read_size, 0) != (ssize_t)read_size) {
            return false;
        }
        file_size -= read_size;
    }

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
    try {
        for(const auto& entry : fs::directory_iterator(".")) {
            if(fs::is_directory(entry) && entry.path().filename().string()[0] != '.') {
                string project_name = entry.path().filename().string();
                cout << "Sending project: " << project_name << endl;
                
                if(!send_string_to_client(client_sock, project_name)) {
                    cerr << "Failed to send project name: " << project_name << endl;
                    return false;
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

    while (true) {
        int client_sock = accept(server_sock, nullptr, nullptr);
        if (client_sock < 0) {
            cerr << "Failed to accept client connection.\n";
            break;
        }
        cout << "Client connected.\n";

        string command;
        if (!receive_data(client_sock, command)) {
            cerr << "Failed to receive command.\n";
            close(client_sock);
            continue;
        }
        cout << "Received command: " << command << "\n";

        if (command == "SUBMIT") {
            handle_submit_request(client_sock);
        }
        else if (command == "CLONE") {
            handle_clone_request(client_sock);
        }
        else if (command == "LIST") {
            handle_list_request(client_sock);
        }
        else {
            cerr << "Unknown command: " << command << "\n";
            send_ack(client_sock, 0);
        }

        close(client_sock);
        cout << "Connection closed.\n";
    }
    close(server_sock);
    return 0;
}

bool handle_submit_request(int client_sock) {
    string project_name;
    if (!receive_data(client_sock, project_name)) {
        cerr << "Failed to receive project name.\n";
        return false;
    }
    cout << "Received project name: " << project_name << "\n";

    if (!fs::exists(project_name)) {
        if (!fs::create_directory(project_name)) {
            cerr << "Failed to create directory: " << project_name << "\n";
            return false;
        }
    }
    // Send ack for project name
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

    return true;
}
