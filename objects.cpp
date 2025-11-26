#include "objects.h"

#include <fstream>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <openssl/evp.h>
#include <sys/stat.h>

using namespace std; 
namespace fs = std::filesystem;

static string bytes_to_hex(const unsigned char *data, size_t length) {
    ostringstream out;
    out << hex << setfill('0');
    for (size_t i = 0; i < length; ++i) {
        out << setw(2) << (int)data[i];
    }
    return out.str();
}

static string sha256(const string &input) {
    EVP_MD_CTX *context = EVP_MD_CTX_new();
    if (!context) return "";

    if (EVP_DigestInit_ex(context, EVP_sha256(), nullptr) != 1) { EVP_MD_CTX_free(context); return ""; }
    if (EVP_DigestUpdate(context, input.data(), input.size()) != 1) { EVP_MD_CTX_free(context); return ""; }

    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len = 0;

    if (EVP_DigestFinal_ex(context, digest, &digest_len) != 1) { EVP_MD_CTX_free(context); return ""; }

    EVP_MD_CTX_free(context);
    return bytes_to_hex(digest, digest_len);
}


static bool make_dir(const fs::path &path) {
    error_code ec;
    if (fs::exists(path, ec)) return true;
    return fs::create_directories(path, ec);
}


static string store_object(const string &type, const string &content, const string &vcp_dir) {
    string header = type + " " + to_string(content.size()) + '\0';
    string full_data = header + content;

    string hash = sha256(full_data);
    if (hash.empty()) return "";

    fs::path base = fs::path(vcp_dir) / "objects";
    fs::path sub = base / hash.substr(0, 2);
    fs::path file_path = sub / hash.substr(2);

    if (!make_dir(sub)) return "";
    if (fs::exists(file_path)) return hash; 

    ofstream out(file_path, ios::binary);
    if (!out) return "";

    out.write(full_data.data(), full_data.size());
    return hash;
}


string write_blob_object(const string &file_path, const string &vcp_dir) {
    ifstream file(file_path, ios::binary | ios::ate);
    if (!file) return "";

    size_t size = file.tellg();
    file.seekg(0);

    string data(size, '\0');
    file.read(&data[0], size);

    return store_object("blob", data, vcp_dir);
}


string write_tree_from_tracker(const string &tracker_path, const string &vcp_dir) {
    ifstream tracker(tracker_path);
    if (!tracker) return "";

    string project;
    getline(tracker, project); //// first line ignored (project name)

    string line;
    ostringstream tree;

    while (getline(tracker, line)) {
        istringstream iss(line);
        string path, hash;
        if (!(iss >> path >> hash) || path.empty()) continue;

        tree << "100644 " << path << " " << hash << "\n";
    }

    return store_object("tree", tree.str(), vcp_dir);
}


string write_commit_object(const string &tree_hash,
                           const vector<string> &parents,
                           const string &author,
                           const string &committer,
                           const string &message,
                           const string &vcp_dir) {

    ostringstream out;
    out << "tree " << tree_hash << "\n";

    for (const auto &p : parents)
        out << "parent " << p << "\n";

    out << "author " << author << "\n"
        << "committer " << committer << "\n\n"
        << message << "\n";

    return store_object("commit", out.str(), vcp_dir);
}


string read_commit_object(const string &hash, const string &vcp_dir) {
    fs::path file = fs::path(vcp_dir) / "objects" / hash.substr(0, 2) / hash.substr(2);

    ifstream in(file, ios::binary | ios::ate);
    if (!in) return "";

    size_t size = in.tellg();
    in.seekg(0);

    string raw(size, '\0');
    in.read(&raw[0], size);

    size_t pos = raw.find('\0');
    if (pos == string::npos) return "";

    return raw.substr(pos + 1);
}


string read_HEAD(const string &vcp_dir) {
    fs::path head = fs::path(vcp_dir) / "HEAD";
    ifstream file(head);
    if (!file) return "";
    string value;
    getline(file, value);
    return value;
}


bool update_HEAD(const string &vcp_dir, const string &hash) {
    fs::path head = fs::path(vcp_dir) / "HEAD";

    if (!make_dir(head.parent_path())) return false;

    ofstream out(head, ios::trunc);
    if (!out) return false;

    out << hash << endl;
    return true;
}
