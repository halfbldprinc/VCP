#include "logging.h"
#include <mutex>
#include <fstream>
#include <iostream> 

using namespace std;
static mutex log_mutex;

void log_event(const string &msg) {
    lock_guard<mutex> lock(log_mutex); 
    ofstream log("server.log", ios::app); 
    if (!log) {
        cerr << "Failed to open server.log" << endl;
        return;
    }
    log << msg << endl;
}
