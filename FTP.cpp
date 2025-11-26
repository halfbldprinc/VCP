// A bit rough around the edges, but gets the job done
// Client for sending project files to remote server
// TODO: Add proper timeout handling for slow networks

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <iomanip>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <filesystem>
#include <regex>
#include "FTP.h"
using namespace std;
namespace fs = std::filesystem;

#define SERVER_PORT 8080  
#define BUFFER_SIZE 1024 
#define TRACKER_FILE ".vcp/tracker.txt"

// file-local helper used by clone/list routines
static bool recv_all(int sock, char *buffer, size_t len) {
    size_t total = 0;
    while(total < len) {
        ssize_t r = recv(sock, buffer + total, len - total, 0);
        if(r <= 0) return false;
        total += r;
    }
    return true;
}

// Implementations for FileTransfer declared in FTP.h
bool FileTransfer::send_chunk(int sock, const void* data, size_t length) {
    size_t bytes_sent = 0;
    while(bytes_sent < length) {
        ssize_t result = send(sock,
                            static_cast<const char*>(data) + bytes_sent,
                            length - bytes_sent,
                            0);
        if(result <= 0) {
            cerr << "Network hiccup sending data (err " << errno << ")\n";
            return false;
        }
        bytes_sent += result;
    }
    return true;
}

void FileTransfer::send_string(int sock, const string &str) {
    uint32_t len = htonl(str.size());
    if(!send_chunk(sock, &len, sizeof(len))) {
        throw runtime_error("Failed to send string length");
    }
    if(!send_chunk(sock, str.data(), str.size())) {
        throw runtime_error("Failed to send string contents");
    }
}

bool FileTransfer::pump_file(int sock, const string &path) {
    ifstream file(path, ios::binary | ios::ate);
    if(!file) {
        cerr << "Couldn't open " << path << " - skipping\n";
        return false;
    }

    auto file_size = file.tellg();
    file.seekg(0);

    uint64_t net_size = htonll(file_size);
    if(!send_chunk(sock, &net_size, sizeof(net_size))) {
        cerr << "Size header failed for " << path << endl;
        return false;
    }

    char buffer[BUFFER_SIZE];
    uint64_t total_size = static_cast<uint64_t>(file_size);
    uint64_t sent_so_far = 0;
    while(file_size > 0) {
        size_t read_size;
        if ((streamsize)BUFFER_SIZE < file_size)
            read_size  = (streamsize)BUFFER_SIZE;
        else
            read_size = file_size;

        file.read(buffer, read_size);
        if(!send_chunk(sock, buffer, read_size)) {
            cerr << "Aborting " << path << " transfer mid-stream\n";
            return false;
        }
        sent_so_far += read_size;
        double pct = (total_size > 0) ? (100.0 * sent_so_far / total_size) : 100.0;
        cout << "Uploading: " << path << " - " << sent_so_far << "/" << total_size
             << " bytes (" << fixed << setprecision(1) << pct << "% )\r";
        cout.flush();

        file_size -= read_size;
    }
    cout << endl;
    return true;
}

bool FileTransfer::get_confirmation(int sock) {
    uint32_t response;
    ssize_t bytes = recv(sock, &response, sizeof(response), 0);
    if(bytes != sizeof(response)) {
        cerr << "Server hung up during confirmation!\n";
        return false;
    }
    return ntohl(response) == 1;
}

int FileTransfer::submit() {
    // Read tracker file and project name first
    ifstream tracker(TRACKER_FILE);
    if(!tracker) {
        cerr << "Can't find tracker file \n";
        return 1;
    }

    string project_name;
    getline(tracker, project_name);
    string session_token;
    const char *home = getenv("HOME");
    if(home) {
        string token_path = string(home) + "/.vcp_session";
        ifstream t(token_path);
            if(t) {
                getline(t, session_token);
                while(!session_token.empty() && isspace((unsigned char)session_token.back())) session_token.pop_back();
            }
    }
    if(session_token.empty()) {
        cerr << "Not authenticated. Run './vcp auth' to log in or sign up before submitting.\n";
        return 1;
    }


    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if(sock < 0) {
        cerr << "Something went wrong\n";
        return 1;
    }
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);
    if(connect(sock, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        cerr << "Connection failed - make sure server is running\n";
        close(sock);
        return 1;
    }
    std::regex valid_name("^[A-Za-z0-9._-]{1,100}$");
    if (!std::regex_match(project_name, valid_name)) {
        cerr << "Invalid project name in tracker: " << project_name << "\n";
        close(sock);
        return 1;
    }
    if(project_name.empty()) {
        cerr << "Tracker file corrupted - missing project name\n";
        close(sock);
        return 1;
    }

    try {
        send_string(sock, "SUBMIT");
        send_string(sock, session_token);
        send_string(sock, project_name);
    } catch(const exception &e) {
        cerr << "Project name send failed: " << e.what() << endl;
        close(sock);
        return 1;
    }

    if(!get_confirmation(sock)) {
        cerr << "Server rejected project '" << project_name << "'\n";
        close(sock);
        return 1;
    }

    string line;
    while(getline(tracker, line)) {
        istringstream iss(line);
        string file_path;
        iss >> file_path;
        if(file_path.empty()) continue;
        try {
            send_string(sock, file_path);
            if(!pump_file(sock, file_path)) {
                cerr << "Skipping " << file_path << " due to errors\n";
                continue;
            }
        } catch(const exception &e) {
            cerr << "Critical failure sending " << file_path << ": " 
                 << e.what() << endl;
            close(sock);
            return 1;
        }
        if(!get_confirmation(sock)) {
            cerr << "Server rejected " << file_path << " - aborting\n";
            close(sock);
            return 1;
        }
    }

    uint32_t end_marker = htonl(0);
    send_chunk(sock, &end_marker, sizeof(end_marker));

    if(get_confirmation(sock)) {
        cout << "All files delivered successfully!\n";
    } else {
        cerr << "Server reported transfer issues\n";
    }

    close(sock);
    return 0;
}

int FileTransfer::clone_project(const string &project_name) {
    std::regex valid_name("^[A-Za-z0-9._-]{1,100}$");
    if (!std::regex_match(project_name, valid_name)) {
        cerr << "Invalid project name for clone: " << project_name << endl;
        return 1;
    }
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if(sock < 0) {
        cerr << "Socket creation failed\n";
        return 1;
    }
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);
    if(connect(sock, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        cerr << "Connection failed - make sure server is running\n";
        close(sock);
        return 1;
    }
    try {
        send_string(sock, "CLONE");
        send_string(sock, project_name);
    } catch(const exception &e) {
        cerr << "Failed to send clone request: " << e.what() << endl;
        close(sock);
        return 1;
    }
    if(!get_confirmation(sock)) {
        cerr << "Server doesn't have project '" << project_name << "' or clone failed\n";
        close(sock);
        return 1;
    }
    if(fs::exists(project_name)) {
        cerr << "Project '" << project_name << "' already exists locally\n";
        close(sock);
        return 1;
    }
    if(!fs::create_directory(project_name)) {
        cerr << "Failed to create local project directory\n";
        close(sock);
        return 1;
    }
    cout << "Cloning project '" << project_name << "'...\n";
    while(true) {
        string filename;
        uint32_t len;
        if(recv(sock, &len, sizeof(len), 0) != sizeof(len)) {
            cerr << "Connection lost while receiving filename length\n";
            break;
        }
        len = ntohl(len);
        if(len == 0) break;
        std::vector<char> buffer(len + 1);
        if(!recv_all(sock, buffer.data(), len)) {
            cerr << "Failed to receive filename\n";
            break;
        }
        buffer[len] = '\0';
        filename = buffer.data();
        string local_path = project_name + "/" + filename;
        cout << "Receiving: " << filename << endl;
        if(!receive_file_from_server(sock, local_path)) {
            cerr << "Failed to receive file: " << filename << endl;
            break;
        }
    }
    cout << "Clone completed successfully!\n";
    close(sock);
    return 0;
}



int FileTransfer::list_projects(const std::string &session_token) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if(sock < 0) {
        cerr << "Socket creation failed\n";
        return 1;
    }
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);
    if(connect(sock, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        cerr << "Connection failed - make sure server is running\n";
        close(sock);
        return 1;
    }
    try {
        send_string(sock, "LIST");
        send_string(sock, session_token);
    } catch(const exception &e) {
        cerr << "Failed to send list request: " << e.what() << endl;
        close(sock);
        return 1;
    }
    cout << "Available projects on server:\n";
    while(true) {
        string project_name;
        uint32_t len;
        if(recv(sock, &len, sizeof(len), 0) != sizeof(len)) {
            break;
        }
        len = ntohl(len);
        if(len == 0) break;
        std::vector<char> buffer(len + 1);
        if(!recv_all(sock, buffer.data(), len)) {
            break;
        }
        buffer[len] = '\0';
        project_name = buffer.data();
        cout << "  - " << project_name << endl;
    }
    close(sock);
    return 0;
}

int FileTransfer::auth_signup(const std::string &email,
                              const std::string &full_name,
                              const std::string &password,
                              const std::string &phone,
                              std::string &out_token) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if(sock < 0) {
        cerr << "Socket creation failed\n";
        return 1;
    }
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);
    if(connect(sock, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        cerr << "Connection failed - make sure server is running\n";
        close(sock);
        return 1;
    }
    try {
        send_string(sock, "AUTH");
        send_string(sock, "SIGNUP");
        send_string(sock, email);
        send_string(sock, full_name);
        send_string(sock, password);
        send_string(sock, phone);
    } catch(const exception &e) {
        cerr << "Failed to send signup request: " << e.what() << endl;
        close(sock);
        return 1;
    }

    if(!get_confirmation(sock)) {
        uint32_t len;
        if(recv(sock, &len, sizeof(len), 0) == sizeof(len)) {
            len = ntohl(len);
            if(len > 0) {
                std::vector<char> buf(len+1);
                if(recv_all(sock, buf.data(), len)) {
                    buf[len] = '\0';
                    cerr << "Signup failed: " << buf.data() << endl;
                }
            }
        }
        close(sock);
        return 1;
    }

    // read token
    uint32_t len;
    if(recv(sock, &len, sizeof(len), 0) != sizeof(len)) {
        cerr << "Failed reading token length\n";
        close(sock);
        return 1;
    }
    len = ntohl(len);
    if(len == 0) {
        cerr << "Server returned empty token\n";
        close(sock);
        return 1;
    }
    std::vector<char> buf(len+1);
    if(!recv_all(sock, buf.data(), len)) {
        cerr << "Failed reading token body\n";
        close(sock);
        return 1;
    }
    buf[len] = '\0';
    out_token = buf.data();
    close(sock);
    return 0;
}

int FileTransfer::auth_login(const std::string &email,
                             const std::string &password,
                             std::string &out_token) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if(sock < 0) {
        cerr << "Socket creation failed\n";
        return 1;
    }
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);
    if(connect(sock, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        cerr << "Connection failed - make sure server is running\n";
        close(sock);
        return 1;
    }
    try {
        send_string(sock, "AUTH");
        send_string(sock, "LOGIN");
        send_string(sock, email);
        send_string(sock, password);
    } catch(const exception &e) {
        cerr << "Failed to send login request: " << e.what() << endl;
        close(sock);
        return 1;
    }

    if(!get_confirmation(sock)) {
        uint32_t len;
        if(recv(sock, &len, sizeof(len), 0) == sizeof(len)) {
            len = ntohl(len);
            if(len > 0) {
                std::vector<char> buf(len+1);
                if(recv_all(sock, buf.data(), len)) {
                    buf[len] = '\0';
                    cerr << "Login failed: " << buf.data() << endl;
                }
            }
        }
        close(sock);
        return 1;
    }

    // read token
    uint32_t len;
    if(recv(sock, &len, sizeof(len), 0) != sizeof(len)) {
        cerr << "Failed reading token length\n";
        close(sock);
        return 1;
    }
    len = ntohl(len);
    if(len == 0) {
        cerr << "Server returned empty token\n";
        close(sock);
        return 1;
    }
    std::vector<char> buf(len+1);
    if(!recv_all(sock, buf.data(), len)) {
        cerr << "Failed reading token body\n";
        close(sock);
        return 1;
    }
    buf[len] = '\0';
    out_token = buf.data();
    close(sock);
    return 0;
}

bool FileTransfer::receive_file_from_server(int sock, const string &save_path) {
    uint64_t net_size;
    if(recv(sock, &net_size, sizeof(net_size), 0) != sizeof(net_size)) {
        return false;
    }
    uint64_t file_size = ntohll(net_size);
    fs::path parent_path = fs::path(save_path).parent_path();
    if(!parent_path.empty() && !fs::exists(parent_path)) {
        fs::create_directories(parent_path);
    }
    ofstream outfile(save_path, ios::binary);
    if(!outfile) {
        cerr << "Cannot create file: " << save_path << endl;
        return false;
    }
    char buffer[BUFFER_SIZE];
    uint64_t remaining = file_size;
    uint64_t total_size = file_size;
    uint64_t received = 0;
    while(remaining > 0) {
        size_t to_read = (remaining < BUFFER_SIZE) ? remaining : BUFFER_SIZE;
        ssize_t r = recv(sock, buffer, to_read, 0);
        if(r <= 0) return false;
        outfile.write(buffer, r);
        remaining -= r;
        received += r;
        double pct = (total_size > 0) ? (100.0 * received / total_size) : 100.0;
        cout << "Downloading: " << save_path << " - " << received << "/" << total_size
             << " bytes (" << fixed << setprecision(1) << pct << "% )\r";
        cout.flush();
    }
    cout << endl;
    return true;
}
