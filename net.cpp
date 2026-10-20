#include "net.hpp"
#include <sys/socket.h>
#include <cerrno>
#include <iostream>
bool send_all(int fd, const std::string& data)
{
    size_t sent_total = 0;
    while (sent_total < data.size()) {
        ssize_t sent = send(fd,data.data() + sent_total,
                            data.size() - sent_total, MSG_NOSIGNAL);
        if (sent < 0) {
            std::cerr << "Error sending data:" << std::endl;
            if (errno == EPIPE) {
                std::cerr << "Server closed the connection" << std::endl;
                return false;
            }
        }
        sent_total += static_cast<std::size_t>(sent);
    }
    return true;
}
