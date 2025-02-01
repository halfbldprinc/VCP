// A bit rough around the edges, but gets the job done
// Client for sending project files to remote server
// TODO: Add proper timeout handling for slow networks

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
using namespace std;

#define SERVER_PORT 8080  
#define BUFFER_SIZE 1024 
#define TRACKER_FILE ".vcp/tracker.txt"

class FileTransfer {
private:
    // Helper to push data through the socket
    // Returns false if we hit a snag
    bool send_chunk(int sock, const void* data, size_t length) {
        size_t bytes_sent = 0;
        while(bytes_sent < length) {
            // Might need to handle signals here
            ssize_t result = send(sock, 
                                static_cast<const char*>(data) + bytes_sent,
                                length - bytes_sent, 
                                0);
            
            if(result <= 0) {
                // Socket went sideways
                cerr << "Network hiccup sending data (err " << errno << ")\n";
                return false;
            }
            bytes_sent += result;
        }
        return true;
    }

    // Package up strings with length prefix
    void send_string(int sock, const string &str) {
        // First size 
        uint32_t len = htonl(str.size());
        if(!send_chunk(sock, &len, sizeof(len))) {
            throw runtime_error("Failed to send string length");
        }
        
        // actual contents
        if(!send_chunk(sock, str.data(), str.size())) {
            throw runtime_error("Failed to send string contents");
        }
    }

    // Handle file transmission
    bool pump_file(int sock, const string &path) {
        ifstream file(path, ios::binary | ios::ate);
        if(!file) {
            cerr << "Couldn't open " << path << " - skipping\n";
            return false;
        }

        
        auto file_size = file.tellg();
        file.seekg(0);  // Reset to start
        
        // Convert size to network byte order
        uint64_t net_size = htonll(file_size);  
        if(!send_chunk(sock, &net_size, sizeof(net_size))) {
            cerr << "Size header failed for " << path << endl;
            return false;
        }

        // send in chunks 
        char buffer[BUFFER_SIZE];
        while(file_size > 0) {
            size_t read_size ;
            if ((streamsize)BUFFER_SIZE < file_size)
            read_size  = (streamsize)BUFFER_SIZE;
            else 
            read_size = file_size;

            file.read(buffer, read_size);
            
            if(!send_chunk(sock, buffer, read_size)) {
                cerr << "Aborting " << path << " transfer mid-stream\n";
                return false;
            }
            
            file_size -= read_size;
        }

        return true;
    }

    // wait for ACK  
    bool get_confirmation(int sock) {
        uint32_t response;
        ssize_t bytes = recv(sock, &response, sizeof(response), 0);
        
        if(bytes != sizeof(response)) {
            cerr << "Server hung up during confirmation!\n";
            return false;
        }
        
        return ntohl(response) == 1;  // 1 != failed 
    }

public:
    int submit() {
        // Set up connection
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if(sock < 0) {
            cerr << "Something went wrong\n";// socket creation failed 
            return 1;
        }

        sockaddr_in server_addr{};
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(SERVER_PORT);
        inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);  //loopback ip for local testing 

        // Try connecting
        if(connect(sock, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            cerr << "Connection failed - make sure server is running\n";
            close(sock);
            return 1;
        }

        // Read project details
        ifstream tracker(TRACKER_FILE);
        if(!tracker) {
            cerr << "Can't find tracker file \n";//wont happen reach here cause before calling submit we are adding trakcer.txt to tracker  but good practice to have it 
            close(sock);
            return 1;
        }

        string project_name;
        getline(tracker, project_name);
        if(project_name.empty()) {
            cerr << "Tracker file corrupted - missing project name\n";
            close(sock);
            return 1;
        }

        try {
            send_string(sock, project_name);
        } catch(const exception &e) {
            cerr << "Project name send failed: " << e.what() << endl;
            close(sock);
            return 1;
        }

        // Wait for server ack
        if(!get_confirmation(sock)) {
            cerr << "Server rejected project '" << project_name << "'\n";
            close(sock);
            return 1;
        }

        // Process file list
        string line;
        while(getline(tracker, line)) {
            // Extract first part before hash 
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

        // Signal end of transmission
        uint32_t end_marker = htonl(0);
        send_chunk(sock, &end_marker, sizeof(end_marker));

        // Final confirmation
        if(get_confirmation(sock)) {
            cout << "All files delivered successfully!\n";
        } else {
            cerr << "Server reported transfer issues\n";
        }

        close(sock);
        return 0;
    }
};