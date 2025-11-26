
#include <iostream>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <sstream>
#include <chrono>
#include <iomanip>
#include <openssl/evp.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <cstring>
#include <unordered_set>
#include <fnmatch.h>
#include "FTP.h"  // for file transferring
#include "objects.h"

namespace fs = std::filesystem;

using namespace std; 

string cpath, vcpPath; 

class VCP {
private:
    //using SHA-256 (OpenSSL EVP)
    string hashFile(const string &fpath) {
        ifstream file(fpath, ios::binary);
        if (!file) {
            cerr << "Can't read " << fpath << " (permissions? missing?)" << endl;
            return "";
        }

        EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
        if (!mdctx) {
            cerr << "Failed to create OpenSSL digest context" << endl;
            return "";
        }

        if (1 != EVP_DigestInit_ex(mdctx, EVP_sha256(), nullptr)) {
            cerr << "EVP_DigestInit_ex failed" << endl;
            EVP_MD_CTX_free(mdctx);
            return "";
        }

        char buf[8192];
        while (file.good()) {
            file.read(buf, sizeof(buf));
            std::streamsize n = file.gcount();
            if (n > 0) {
                if (1 != EVP_DigestUpdate(mdctx, buf, (size_t)n)) {
                    cerr << "EVP_DigestUpdate failed" << endl;
                    EVP_MD_CTX_free(mdctx);
                    return "";
                }
            }
        }

        unsigned char md_value[EVP_MAX_MD_SIZE];
        unsigned int md_len = 0;
        if (1 != EVP_DigestFinal_ex(mdctx, md_value, &md_len)) {
            cerr << "EVP_DigestFinal_ex failed" << endl;
            EVP_MD_CTX_free(mdctx);
            return "";
        }

        EVP_MD_CTX_free(mdctx);

        stringstream hs;
        for (unsigned int i = 0; i < md_len; ++i)
            hs << hex << setw(2) << setfill('0') << (int)md_value[i];
        return hs.str();
    }

    // Skip executables and hidden files for security 
    bool isExe(const fs::path &fpath) {
        fs::file_status status = fs::status(fpath);
        bool is_exec = 
            (status.permissions() & fs::perms::owner_exec) != fs::perms::none || 
            (status.permissions() & fs::perms::group_exec) != fs::perms::none || 
            (status.permissions() & fs::perms::others_exec) != fs::perms::none;
        
        // Heuristic for executable extension
        bool no_ext = fpath.extension().empty();
        return is_exec || no_ext; 
    }

    // .vcpignore support
    std::vector<std::string> vcpignore_patterns;

    static inline std::string trim(const std::string &s) {
        size_t a = 0;
        while (a < s.size() && isspace((unsigned char)s[a])) ++a;
        size_t b = s.size();
        while (b > a && isspace((unsigned char)s[b-1])) --b;
        return s.substr(a, b-a);
    }

    void loadVcpIgnore() {
        vcpignore_patterns.clear();
        std::string ignore_path = cpath + "/.vcpignore";
        if (!fs::exists(ignore_path)) return;
        std::ifstream ig(ignore_path);
        if(!ig) return;
        std::string line;
        while (std::getline(ig, line)) {
            line = trim(line);
            if (line.empty() || line[0] == '#') continue;
            // normalize patterns: remove leading './'
            if (line.rfind("./", 0) == 0) line = line.substr(2);
            vcpignore_patterns.push_back(line);
        }
    }

    bool isIgnored(const std::string &rel_path, bool is_dir=false) {
        if (vcpignore_patterns.empty()) return false;
        // convert rel_path to posix style (should already be)
        std::string path = rel_path;
        for (const auto &pat_in : vcpignore_patterns) {
            std::string pat = pat_in;
            // If pattern ends with '/', treat as directory-only pattern
            bool dir_only = false;
            if (!pat.empty() && pat.back() == '/') { dir_only = true; pat.pop_back(); }
            if (dir_only && !is_dir) continue;

            // fnmatch expects C strings; match against path
            // Allow pattern to match anywhere: if pattern does not contain '/', try to match basename too
            int flags = FNM_PATHNAME; // make / special
            if (fnmatch(pat.c_str(), path.c_str(), flags) == 0) return true;

            if (pat.find('/') == std::string::npos) {
                // match against filename portion
                auto pos = path.find_last_of('/');
                std::string base = (pos==std::string::npos) ? path : path.substr(pos+1);
                if (fnmatch(pat.c_str(), base.c_str(), 0) == 0) return true;
            }
        }
        return false;
    }

public:

// Set up new project
    void init() {
        string tracker_path = vcpPath + "/tracker.txt";
        if(fs::exists(tracker_path)){
            cerr << "Existing project found!\n"; 
            return;
        }

        // Make .vcp dir
        if (!fs::create_directory(vcpPath)) {
            cerr << "Failed to create .vcp directory - permissions issue?\n";
            return;
        }

        // Create a default .vcpignore if one doesn't exist
        std::string ignore_path = cpath + "/.vcpignore";
        if (!fs::exists(ignore_path)) {
            std::ofstream ig(ignore_path);
            if (ig) {
                ig << "# .vcpignore - files and folders to ignore\n";
                ig << ".vcp/\n";
                ig << "*.o\n";
                ig << "*.exe\n";
                ig << "*.log\n";
                ig << "node_modules/\n";
                ig << ".DS_Store\n";
            }
        }

        // Get project name from user
        string proj_name;
        cout << "Enter project name (no spaces): ";
        cin.ignore();
        getline(cin, proj_name);
        
        // Add timestamp to ensure project name is unique and avoid conflicts
        auto now = chrono::system_clock::now();
        time_t now_c = chrono::system_clock::to_time_t(now);
        stringstream timestamp;
        timestamp << put_time(localtime(&now_c), "%Y%m%d_%H%M");
        proj_name += "_" + timestamp.str();

        // Create tracker file and write project name on it
        ofstream tracker(tracker_path);
        if (!tracker) {
            cerr << "Failed to create tracker file!\n";
            return;
        }
        tracker << proj_name << endl;
        cout << "Project '" << proj_name << "' ready!\n";
    }

// Show state of repository
    void state() {
        string tracker_file = vcpPath + "/tracker.txt";
        if(!fs::exists(tracker_file)){
            cerr << "No project here - run 'init' first!\n"; 
            return;
        }

        // Load existing files
        ifstream tf(tracker_file);
        string proj_name;
        getline(tf, proj_name);
        
        unordered_set<string> tracked_dirs;
        unordered_map<string,string> tracked_files;
        string path, hash;
        while(tf >> path >> hash) {
            if(path.back() == '/') {
                tracked_dirs.insert(path);
            } else {
                tracked_files[path] = hash;
            }
        }
        tf.close();

        // Scan current dir
        loadVcpIgnore();
        unordered_map<string,string> new_items;
        unordered_map<string,string> changed_files;
        
        try {
            for (auto it = fs::recursive_directory_iterator(cpath, 
                    fs::directory_options::skip_permission_denied);
                 it != fs::recursive_directory_iterator(); ++it)
            {
                const auto &entry = *it;
                string rel_path = fs::relative(entry.path(), cpath).string();

                // Skip hidden files
                if(rel_path.find("/.") != string::npos || rel_path[0] == '.') continue;

                // Skip patterns from .vcpignore. If a directory is ignored, disable recursion
                if (isIgnored(rel_path, fs::is_directory(entry.path()))) {
                    if (fs::is_directory(entry.path())) it.disable_recursion_pending();
                    continue;
                }

                if(fs::is_regular_file(entry)) {
                    string current_hash = hashFile(entry.path().string());
                    if(current_hash.empty()) continue;  // Skip unreadable

                    if(!tracked_files.count(rel_path)) {
                        new_items[rel_path] = current_hash;
                    } else if(tracked_files[rel_path] != current_hash) {
                        changed_files[rel_path] = current_hash;
                    }
                }
                else if(fs::is_directory(entry)) {
                    if(!tracked_dirs.count(rel_path + "/")) {
                        new_items[rel_path + "/"] = "";
                    }
                }
            }
        } catch(fs::filesystem_error& e) {
            cerr << "Scan failed  " << endl;
        }

        // Print results
        if(!new_items.empty()) {
            cout << "** New items **\n";
            for(const auto& item : new_items) {
                cout << "  + " << item.first << endl;
            }
        }
        
        if(!changed_files.empty()) {
            cout << "\n** Modified files **\n";
            for(const auto& file : changed_files) {
                cout << "  * " << file.first << endl;
            }
        }
        
        if(new_items.empty() && changed_files.empty()) {
            cout << "No changes detected\n";
        }
    }

    // Add files to tracking
    bool add(const string &fpath) {
        string tracker_path = vcpPath + "/tracker.txt";
        if(!fs::exists(tracker_path)) {
            cerr << "No project! Run 'init' first.\n"; 
            return false;
        }

        if (!fs::exists(fpath)) { 
            cerr << "Can't find '" << fpath << "' - typo?\n"; 
            return false; 
        }

        // Read existing tracker data
        ifstream tracker(tracker_path);
        string proj_name;
        getline(tracker, proj_name);
        
        unordered_map<string, string> tracked;
        string path, hash;
        while(tracker >> path >> hash) {
            tracked[path] = hash;
        }
        tracker.close();

        // Process input path
        string rel_path = fs::relative(fpath, cpath).string();
        loadVcpIgnore();
        
        if (fs::is_directory(fpath)) {
            // Add directory contents
            try {
                for (auto it = fs::recursive_directory_iterator(fpath);
                     it != fs::recursive_directory_iterator(); ++it) {
                    const auto &entry = *it;
                    // Skip ignored patterns
                    string item_rel = fs::relative(entry.path(), cpath).string();
                    if (isIgnored(item_rel, fs::is_directory(entry.path()))) {
                        if (fs::is_directory(entry.path())) it.disable_recursion_pending();
                        continue;
                    }

                    if(fs::is_regular_file(entry) && !isExe(entry.path())) {
                        tracked[item_rel] = hashFile(entry.path().string());
                    }
                }
            } catch(...) {
                cerr << "Error scanning directory\n";
                return false;
            }
        } 
        else if(fs::is_regular_file(fpath)) {
            if(isExe(fpath)) {
                cerr << "Unsupported file type\n \t executable file\n";
                return false;
            }
            tracked[rel_path] = hashFile(fpath);
        }
        else {
            cerr << "Unsupported file type\n";
            return false;
        }

        // Write back updated tracker
        ofstream out(tracker_path);
        out << proj_name << endl;
        for(const auto& item : tracked) {
            out << item.first << " " << item.second << endl;
        }
        return true;
    }

// Push to server
    void submit() {
        add(vcpPath);
        FileTransfer ft;
        ft.submit();  
    }

    
    void clone(const string &project_name) {
        if(project_name.empty()) {
            cerr << "Please specify a project name to clone\n";
            return;
        }

        FileTransfer ft;
        if(ft.clone_project(project_name) == 0) {
            if(fs::exists(project_name + "/.vcp")) {
                cout << "Project '" << project_name << "' cloned successfully!\n";
                cout << "Navigate to the project directory with: cd " << project_name << endl;
            }
        }
    }

    
    void list() {
        FileTransfer ft;
        ft.list_projects();
    }
    
    void commit(const string &message, const string &author) {
        string tracker_path = vcpPath + "/tracker.txt";
        if(!fs::exists(tracker_path)) {
            cerr << "No tracker file - nothing staged to commit\n";
            return;
        }

        if(!fs::exists(vcpPath)) {
            cerr << "No .vcp directory - run 'init' first\n";
            return;
        }
        string tree_hash = write_tree_from_tracker(tracker_path, vcpPath);
        if(tree_hash.empty()) { cerr << "Failed to write tree object\n"; return; }

        string parent = read_HEAD(vcpPath);
        vector<string> parents;
        if(!parent.empty()) parents.push_back(parent);

        string committer = author.empty() ? "vcp <vcp@local>" : author;
        string author_field = committer;

        string commit_hash = write_commit_object(tree_hash, parents, author_field, committer, message, vcpPath);
        if(commit_hash.empty()) { cerr << "Failed to write commit object\n"; return; }

        if(!update_HEAD(vcpPath, commit_hash)) { cerr << "Failed to update HEAD\n"; return; }

        cout << "Committed as " << commit_hash << "\n";
    }

    void log() {
        string head = read_HEAD(vcpPath);
        if(head.empty()) { cout << "No commits yet\n"; return; }
        string cur = head;
        while(!cur.empty()) {
            string body = read_commit_object(cur, vcpPath);
            if(body.empty()) break;
            std::istringstream iss(body);
            string line;
            string author;
            string message;
            vector<string> parents;
            while(std::getline(iss, line)) {
                if(line.empty()) break;
                if(line.rfind("author ",0) == 0) author = line.substr(7);
                else if(line.rfind("parent ",0) == 0) parents.push_back(line.substr(7));
            }
            std::string msg;
            std::getline(iss, msg, '\0');
            cout << "commit " << cur << "\n";
            if(!author.empty()) cout << "Author: " << author << "\n";
            cout << msg << "\n\n";
            if(!parents.empty()) cur = parents[0]; else break;
        }
    }

    void help() {
        cout << "VCP - Version Control Program\n";
        cout << "Commands:\n"
             << "  init     - Start new project\n"
             << "  state    - Show changes\n"
             << "  add      - Track files\n"
             << "  submit   - Send to server\n"
             << "  clone    - Clone project from server\n"
             << "  list     - List available projects on server\n";
    }
};


int main(int argc, char *argv[]) {
    VCP vcp;
    cpath = fs::current_path().string();
    vcpPath = cpath + "/.vcp"; 

    if(argc < 2) {
        vcp.help();
        return 1;
    }

    string cmd = argv[1];
    
    if(cmd == "init") {
        vcp.init();
    }
    else if(cmd == "state") {
        vcp.state();
    }
    else if(cmd == "add") {
        if(argc < 3) {
            cerr << "Missing file to add!\n";
            return 1;
        }
        vcp.add(argv[2]);
    }
    else if(cmd == "submit") {
        vcp.submit();
    }
    else if(cmd == "commit") {
        string message;
        string author;
        for(int i=2;i<argc;++i) {
            string a = argv[i];
            if(a == "-m" && i+1<argc) { message = argv[++i]; }
            else if(a == "--author" && i+1<argc) { author = argv[++i]; }
        }
        if(message.empty()) {
            cerr << "Missing commit message. Use -m \"message\"\n";
            return 1;
        }
        vcp.commit(message, author);
    }
    else if(cmd == "log") {
        vcp.log();
    }
    else if(cmd == "clone") {
        if(argc < 3) {
            cerr << "Missing project name to clone!\n";
            return 1;
        }
        vcp.clone(argv[2]);
    }
    else if(cmd == "list") {
        vcp.list();
    }
    else {
        cerr << "Unknown command '" << cmd << "'\n";
        vcp.help();
        return 1;
    }

    return 0;
}