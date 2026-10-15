
#pragma once

#include <string>
#include <utility>
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

    Socket(Socket&& other) noexcept : fd(other.fd) {
        other.fd = -1;
    }

    Socket& operator=(Socket&& other) noexcept {
        if (this != &other) {
            if (fd >= 0) close(fd);
            fd = other.fd;
            other.fd = -1;
        }
        return *this;
    }
};

class Client {
private:
    Socket sock;

public:
    std::string buffer;

    Client() : sock(-1) {}

    Client(Socket socket, std::string initial_buffer = {})
        : sock(std::move(socket)), buffer(std::move(initial_buffer)) {}
};