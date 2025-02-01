
#include <iostream>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <sstream>
#include <chrono>
#include <iomanip>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <cstring>
#include <unordered_set>
#include "FTP.cpp"  //for file transfering must be compiled together 

namespace fs = std::filesystem;

using namespace std; 

string cpath, vcpPath; 

class VCP {
private:
    // Generate file fingerprint 
    // ** TODO use a better algo
    string hashFile(const string &fpath) {
        ifstream file(fpath, ios::binary);
        if (!file) { 
            cerr << "Can't read " << fpath << " (permissions? missing?)" << endl; 
            return ""; 
        }
        
        // Read whole file - might get stuck on big files
        string content((istreambuf_iterator<char>(file)), istreambuf_iterator<char>());
        size_t fhash = hash<string>{}(content);
        stringstream hs; 
        hs << hex << fhash;
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
        unordered_map<string,string> new_items;
        unordered_map<string,string> changed_files;
        
        try {
            for(const auto& entry : fs::recursive_directory_iterator(cpath, 
                fs::directory_options::skip_permission_denied)) 
            {
                string rel_path = fs::relative(entry.path(), cpath).string();
                
                // Skip hidden files
                if(rel_path.find("/.") != string::npos || 
                   rel_path[0] == '.') continue;

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
        
        if (fs::is_directory(fpath)) {
            // Add directory contents
            try {
                for(const auto& entry : fs::recursive_directory_iterator(fpath)) {
                    if(fs::is_regular_file(entry) && !isExe(entry.path())) {
                        string item_rel = fs::relative(entry.path(), cpath).string();
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

    void help() {
        cout << "VCP - Version Control Program\n";
        cout << "Commands:\n"
             << "  init     - Start new project\n"
             << "  state    - Show changes\n"
             << "  add      - Track files\n"
             << "  submit   - Send to server\n";
            //  << "  clone    - Get from server \n";
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
    // else if(cmd == "clone") {
    //     
    // }
    else {
        cerr << "Unknown command '" << cmd << "'\n";
        vcp.help();
        return 1;
    }

    return 0;
}