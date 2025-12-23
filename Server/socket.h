#pragma once
#include <unistd.h>

struct Socket {
    int fd;
    explicit Socket(int fd_ = -1) : fd(fd_) {}
    ~Socket() { if (fd >= 0) ::close(fd); }
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    Socket(Socket&& other) noexcept : fd(other.fd) { other.fd = -1; }
    Socket& operator=(Socket&& other) noexcept {
        if (this != &other) {
            if (fd >= 0) ::close(fd);
            fd = other.fd;
            other.fd = -1;
        }
        return *this;
    }
    int get() const { return fd; }
    int release() { int tmp = fd; fd = -1; return tmp; }
    explicit operator bool() const { return fd >= 0; }
};
