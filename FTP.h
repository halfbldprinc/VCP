#ifndef FTP_H
#define FTP_H

#include <string>

class FileTransfer {
private:
    bool send_chunk(int sock, const void* data, size_t length);
    void send_string(int sock, const std::string &str);
    bool pump_file(int sock, const std::string &path);
    bool receive_file_from_server(int sock, const std::string &save_path);
    bool get_confirmation(int sock);
public:
    int submit();
    int clone_project(const std::string &project_name);
    int list_projects(const std::string &session_token = "");
    int auth_signup(const std::string &email,
                    const std::string &full_name,
                    const std::string &password,
                    const std::string &phone,
                    std::string &out_token);

    int auth_login(const std::string &email,
                   const std::string &password,
                   std::string &out_token);
};

#endif 
