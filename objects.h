#ifndef OBJECTS_H
#define OBJECTS_H

#include <string>
#include <vector>

using namespace std;
string write_blob_object(const string &file_path, const string &vcp_dir);

string write_tree_from_tracker(const string &tracker_path, const string &vcp_dir);

string write_commit_object(const string &tree_hash,
                           const vector<string> &parent_hashes,
                           const string &author,
                           const string &committer,
                           const string &message,
                           const string &vcp_dir);

string read_commit_object(const string &commit_hash, const string &vcp_dir);

string read_HEAD(const string &vcp_dir);

bool update_HEAD(const string &vcp_dir, const string &commit_hash);

#endif 
