#include "storage.h"
#include "protocol.h"
#include "logging.h"
#include "auth.h"
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <regex>

namespace fs = std::filesystem;

bool receive_file(int sock, const std::string &save_path) {
    uint64_t net_size;
    if (!recv_all(sock, reinterpret_cast<char*>(&net_size), sizeof(net_size))) return false;
    uint64_t file_size = ntohll(net_size);
    fs::path outpath(save_path);
    fs::path parent = outpath.parent_path();
    if(!parent.empty() && !fs::exists(parent)) {
        try {
            fs::create_directories(parent);
        } catch(const fs::filesystem_error &e) {
            std::cerr << "Error creating directories: " << e.what() << "\n";
            return false;
        }
    }

    std::ofstream outfile(save_path, std::ios::binary);
    if (!outfile) {
        std::cerr << "Error: Cannot open file for writing: " << save_path << "\n";
        return false;
    }

    const size_t BUFFER_SIZE = 1024;
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
        std::cout << "Receiving: " << save_path << " - " << received << "/" << total
             << " bytes (" << std::fixed << std::setprecision(1) << pct << "% )\r";
        std::cout.flush();
    }
    std::cout << std::endl;
    outfile.close();
    return true;
}

bool send_file_to_client(int sock, const std::string &file_path) {
    std::ifstream file(file_path, std::ios::binary | std::ios::ate);
    if(!file) {
        std::cerr << "Cannot open file for sending: " << file_path << std::endl;
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
    const size_t BUFFER_SIZE = 1024;
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
        std::cout << "Sending: " << file_path << " - " << sent << "/" << total
             << " bytes (" << std::fixed << std::setprecision(1) << pct << "% )\r";
        std::cout.flush();
    }
    std::cout << std::endl;

    return true;
}

bool handle_clone_request(int client_sock) {
    std::string project_name;
    if(!receive_data(client_sock, project_name)) {
        std::cerr << "Failed to receive project name for clone\n";
        return false;
    }

    std::cout << "Clone request for project: " << project_name << std::endl;
    log_event("Clone request for project: " + project_name);

    // project name
    std::regex valid_name("^[A-Za-z0-9._-]{1,100}$");
    if (!std::regex_match(project_name, valid_name)) {
        std::cerr << "Invalid project name for clone: " << project_name << std::endl;
        send_ack(client_sock, 0);
        return false;
    }

    if(!fs::exists(project_name) || !fs::is_directory(project_name)) {
        std::cerr << "Project '" << project_name << "' not found\n";
        send_ack(client_sock, 0);
        return false;
    }
    // Send ack for project name
    if(!send_ack(client_sock, 1)) {
        std::cerr << "Failed to send clone ack\n";
        return false;
    }

    try {
        for(const auto& entry : fs::recursive_directory_iterator(project_name)) {
            if(fs::is_regular_file(entry)) {
                std::string rel_path = fs::relative(entry.path(), project_name).string();
                std::cout << "Sending file: " << rel_path << std::endl;
                log_event("Sending file: " + rel_path);
                
                if(!send_string_to_client(client_sock, rel_path)) {
                    std::cerr << "Failed to send filename: " << rel_path << std::endl;
                    return false;
                }
                
                if(!send_file_to_client(client_sock, entry.path().string())) {
                    std::cerr << "Failed to send file: " << rel_path << std::endl;
                    return false;
                }
            }
        }
    } catch(const fs::filesystem_error& e) {
        std::cerr << "Error accessing project files: " << e.what() << std::endl;
        return false;
    }

    uint32_t end_marker = htonl(0);
    if(send(client_sock, &end_marker, sizeof(end_marker), 0) != sizeof(end_marker)) {
        std::cerr << "Failed to send end marker\n";
        return false;
    }

    std::cout << "Clone completed for project: " << project_name << std::endl;
    log_event("Clone completed for project: " + project_name);
    return true;
}

bool handle_list_request(int client_sock) {
    std::cout << "List request received\n";
    log_event("List request received");
    try {
        for(const auto& entry : fs::directory_iterator(".")) {
            if(fs::is_directory(entry) && entry.path().filename().string()[0] != '.') {
                std::string project_name = entry.path().filename().string();
                std::cout << "Sending project: " << project_name << std::endl;
                log_event("Sending project: " + project_name);
                
                if(!send_string_to_client(client_sock, project_name)) {
                    std::cerr << "Failed to send project name: " << project_name << std::endl;
                    return false;
                }
            }
        }
    } catch(const fs::filesystem_error& e) {
        std::cerr << "Error listing projects: " << e.what() << std::endl;
        return false;
    }

    uint32_t end_marker = htonl(0);
    if(send(client_sock, &end_marker, sizeof(end_marker), 0) != sizeof(end_marker)) {
        std::cerr << "Failed to send end marker\n";
        return false;
    }

    std::cout << "Project list sent successfully\n";
    return true;
}

bool handle_submit_request(int client_sock) {
    // First value: session token (required)
    std::string session_token;
    if(!receive_data(client_sock, session_token)) {
        std::cerr << "Failed to receive session token for SUBMIT.\n";
        send_ack(client_sock, 0);
        return false;
    }
    std::string user_id;
    if(session_token.empty() || !db_get_user_by_token(session_token, user_id)) {
        std::cerr << "Invalid or missing session token for SUBMIT\n";
        send_ack(client_sock, 0);
        return false;
    }

    std::string project_name;
    if (!receive_data(client_sock, project_name)) {
        std::cerr << "Failed to receive project name.\n";
        return false;
    }
    std::cout << "Received project name: " << project_name << "\n";
    std::regex valid_name("^[A-Za-z0-9._-]{1,100}$");
    if (!std::regex_match(project_name, valid_name)) {
        std::cerr << "Invalid project name received: " << project_name << "\n";
        send_ack(client_sock, 0);
        return false;
    }

    fs::path base = fs::path("users") / user_id / project_name;
    try {
        fs::create_directories(base);
    } catch(const fs::filesystem_error &e) {
        std::cerr << "Failed to create user project directory: " << e.what() << "\n";
        send_ack(client_sock, 0);
        return false;
    }
    if (!send_ack(client_sock, 1)) {
        std::cerr << "Failed to send ack for project name.\n";
        return false;
    }
    while (true) {
        std::string filepath;
        if (!receive_data(client_sock, filepath)) {
            std::cerr << "Failed to receive file name.\n";
            break;
        }
        if (filepath.empty()) break;
        if (fs::path(filepath).is_absolute() || filepath.find("..") != std::string::npos) {
            std::cerr << "Rejected unsafe filepath: " << filepath << "\n";
            send_ack(client_sock, 0);
            continue;
        }

        fs::path save_path = base / fs::path(filepath);
        std::cout << "Receiving file: " << filepath << " -> " << save_path.string() << "\n";
        log_event("Receiving file: " + save_path.string());
        if (receive_file(client_sock, save_path.string())) {
            if (!send_ack(client_sock, 1)) {
                std::cerr << "Failed to send ack for file: " << filepath << "\n";
                break;
            }
        } else {
            std::cerr << "Error receiving file: " << filepath << "\n";
            send_ack(client_sock, 0); 
            break;
        }
    }

    if (!send_ack(client_sock, 1))
        std::cerr << "Failed to send final ack.\n";

    return true;
}
