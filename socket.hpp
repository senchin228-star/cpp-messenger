
#pragma once
#include <unistd.h>

class Socket {
    private:
        int fd;
    public:
        explicit Socket(int socket_fd) : fd(socket_fd) {}
        int get() const {
            return fd;
        }
        ~Socket() {
            if (fd >= 0) close(fd);
        }
        Socket(const Socket&) = delete;
        Socket& operator=(const Socket&) = delete;
};