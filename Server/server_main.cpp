#include "protocol.h"
#include "storage.h"
#include "logging.h"
#include "auth.h"
#include <csignal>
#include <atomic>
#include <iostream>
#include <mutex>
#include <fstream>
#include <string>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <thread>
#include <chrono>

using namespace std; 
#define MAX_CLIENTS 8
#define SERVER_PORT 8080
atomic<bool> server_running{true};

void handle_sigint(int) {
    cout << "\nSIGINT received. Shutting down server...\n";
    log_event("SIGINT received. Shutting down server...");
    server_running = false;
}

int main() {
    if (!db_init("vcp_server.db")) {
        cerr << "Warning: Failed to initialize SQLite DB (auth disabled or not writable)\n";
    }

    signal(SIGINT, handle_sigint);

    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        cerr << "Error creating server socket." << endl;
        return 1;
    }
    int opt = 1;
    if (setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        cerr << "setsockopt failed" << endl;
        close(server_sock);
        return 1;
    }

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (::bind(server_sock, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)) < 0) {
        cerr << "Bind failed." << endl;
        close(server_sock);
        return 1;
    }
    if (listen(server_sock, 1) < 0) {
        cerr << "Listen failed." << endl;
        close(server_sock);
        return 1;
    }

    cout << "Server listening on port " << SERVER_PORT << "..." << endl;

    atomic<int> client_count{0};

    auto client_handler = [&](int client_sock) {
        client_count++;
        string connect_msg = "Client connected. Active clients: " + to_string(client_count);
        cout << connect_msg << "\n";
        log_event(connect_msg);

        string command;
        if (!receive_data(client_sock, command)) {
            string err_msg = "Failed to receive command.";
            cerr << err_msg << "\n";
            log_event(err_msg);
            close(client_sock);
            client_count--;
            return;
        }

        string cmd_msg = "Received command: " + command;
        cout << cmd_msg << "\n";
        log_event(cmd_msg);

        //// Process commands
        switch (parse_command(command)) {
            case ProtocolCommand::SUBMIT: handle_submit_request(client_sock); break;
            case ProtocolCommand::CLONE:  handle_clone_request(client_sock);  break;
            case ProtocolCommand::LIST:   handle_list_request(client_sock);   break;
            default: {
                string unknown_msg = "Unknown command: " + command;
                cerr << unknown_msg << "\n";
                log_event(unknown_msg);
                send_ack(client_sock, 0);
                break;
            }
        }

        close(client_sock);
        string disconnect_msg = "Connection closed. Active clients: " + to_string(client_count - 1);
        cout << disconnect_msg << "\n";
        log_event(disconnect_msg);
        client_count--;
    };

    while (server_running) {
        try {
            if (client_count >= MAX_CLIENTS) {
                this_thread::sleep_for(chrono::milliseconds(100));
                continue;
            }

            int client_sock = accept(server_sock, nullptr, nullptr);
            if (!server_running) {
                if (client_sock >= 0) close(client_sock);
                break;
            }

            if (client_sock < 0) {
                if (errno == EINTR && !server_running) break;
                cerr << "Failed to accept client connection." << endl;
                log_event("Failed to accept client connection.");
                continue;
            }

            thread([&, client_sock]() {
                try {
                    client_handler(client_sock);
                } catch (const exception& e) {
                    string err = "Exception in client handler: " + string(e.what());
                    cerr << err << "\n";
                    log_event(err);
                    close(client_sock);
                    client_count--;
                } catch (...) {
                    string err = "Unknown exception in client handler.";
                    cerr << err << "\n";
                    log_event(err);
                    close(client_sock);
                    client_count--;
                }
            }).detach();

        } catch (const exception& e) {
            string err = "Exception in main accept loop: " + string(e.what());
            cerr << err << "\n";
            log_event(err);
        } catch (...) {
            string err = "Unknown exception in main accept loop.";
            cerr << err << "\n";
            log_event(err);
        }
    }

    close(server_sock);
    db_close();
    log_event("Server socket closed. Exiting main.");
    cout << "Server shut down." << endl;

    return 0;
}
