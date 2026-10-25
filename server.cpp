#include <string>
#include <iostream>
#include <vector>
#include <map>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <poll.h>
#include <cstring>
#include <cerrno>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include "socket.hpp"
#include "net.hpp"

constexpr std::size_t MAX_BUFFER = 4096;
constexpr auto LOGIN_TIMEOUT = std::chrono::milliseconds(1000);

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
    return value;
}

std::string trim(std::string& str) {
    auto first = str.find_first_not_of(" \t\n\r");
    if (first == std::string::npos) {
        return ""; // String is all whitespace
    }
    auto last = str.find_last_not_of(" \t\n\r");
    str = str.substr(first, last - first + 1);
    return str;
}

void broadcast(std::map<int, Client>& clients, const std::string payload, int exclude_fd = -1)
{
    for (auto& [fd, client] : clients){
        if (fd == exclude_fd) continue;
        if(!send_all(fd, payload)) {
            std::cerr << "Send to fd " << fd << " failed: " << strerror(errno) << std::endl;
        }
    }
}
bool process_buffer(Client& client, int fd, std::map<int, Client>& clients) {
    std::size_t pos;
    while ((pos = client.buffer.find('\n')) != std::string::npos) {
        std::string line = client.buffer.substr(0, pos);
        client.buffer.erase(0, pos + 1);
        if (line.empty()) continue;
        try {
            auto j = nlohmann::json::parse(line);
            std::string type = j.at("type").get<std::string>();
            if (type.empty()) continue;
            nlohmann::json out = {};
            if (type == "login"){
                std::string username = j.at("user").get<std::string>();
                std::string reason;
                username = trim(username);
                const std::string username_lower = lowercase(username);
                if (username.size() > 16) reason = "Username too long";
                if (!client.name.empty()) reason = "User is already logged in";
                else if (username.empty()) reason = "Empty user name";
                for (const auto& [other_fd, other_client] : clients){
                    if (other_fd != client.get_sock() && lowercase(other_client.name) == username_lower){
                        reason = "Already logged in";
                        break;
                    }
                }
                out = {
                    {"type", "login_result"},
                    {"ok", reason.empty()},
                    {"reason", reason}
                };
                std::string log_res = out.dump() + '\n';
                if (!send_all(client.get_sock(), log_res)) return false;
                if (!reason.empty()){
                    std::cout << username << ": Login unsucces. Reason: "<< reason << std::endl;
                    return false;
                }
                else{
                    client.name = username;
                    std::cout << username << " Logined" << std::endl;
                }
            }
            if (client.name.empty()) continue;
            if (type == "text"){
                std::cout << "type: " << j.at("type").get<std::string>() + "   "
                        << client.name << ": "
                        << j.at("message").get<std::string>() << std::endl;
                out["type"] = "text";
                out["message"] = j.at("message").get<std::string>();
                out["nickname"] = client.name;
                broadcast(clients, out.dump() +  "\n", fd);
            }
        } catch (const nlohmann::json::exception& e) {
            std::cerr << "Bad message: " << e.what() << std::endl;
            nlohmann::json error = {{"type", "error"}, {"er_text", e.what()}};
            std::string error_line = error.dump() + '\n';
            send_all(client.get_sock(), error_line);
        }
    }
    if (client.buffer.size() > MAX_BUFFER) {
        std::cerr << "Message too long, dropping client" << std::endl;
        return false;
    }
    return true;
}
int main(){
    Socket listen_sock(socket(AF_INET, SOCK_STREAM, 0));
    if (listen_sock.get() < 0) {
        std::cerr << "Error creating socket" << std::endl;
        return 1;
    }
    struct sockaddr_in server_addr = {};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(5050);
    server_addr.sin_addr.s_addr = INADDR_ANY;
    int opt = 1;
    if (setsockopt(listen_sock.get(), SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        std::cerr << "setsockopt failed: " << strerror(errno) << std::endl;
        return 1;
    }
    if (bind(listen_sock.get(), (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "Error binding socket: " << strerror(errno) << std::endl;
        return 1;
    }
    if (listen(listen_sock.get(), 5) < 0) {
        std::cerr << "Error listening on socket: " << strerror(errno) << std::endl;
        return 1;
    }
    std::cout << "Server is listening on port 5050..." << std::endl;

    std::vector<pollfd> fds;
    fds.push_back({listen_sock.get(), POLLIN, 0});
    std::map<int, Client> clients;
    char recv_buffer[4096];
    
    while (true){
        int poll_timeout = -1;
        const auto now = std::chrono::steady_clock::now();
        for (const auto& [fd, client] : clients) {
            (void)fd;
            if (!client.name.empty()) continue;
            const auto deadline = client.connected_at + LOGIN_TIMEOUT;
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
            const int client_timeout = remaining <= 0 ? 0 : static_cast<int>(remaining);
            poll_timeout = poll_timeout < 0 ? client_timeout : std::min(poll_timeout, client_timeout);
        }

        if (poll(fds.data(), fds.size(), poll_timeout) < 0) {
            if (errno == EINTR) continue;
            std::cout << "pollfd error" << std::endl;
            return 1;
        }
        if (fds[0].revents & POLLIN){
            sockaddr_in client_addr = {};
            socklen_t client_addr_len = sizeof(client_addr);
            Socket client_sock(accept(listen_sock.get(),
            reinterpret_cast<struct sockaddr*>(&client_addr), &client_addr_len));
            if (client_sock.get() < 0) {
                std::cerr << "Error accepting connection: " << strerror(errno) << std::endl;
                continue;
            }
            int fd = client_sock.get();
            clients.emplace(fd, Client{std::move(client_sock), ""});
            fds.push_back({fd, POLLIN, 0});
        }
        for (std::size_t i = fds.size(); i-- > 1; ) {
            int fd = fds[i].fd;
            Client& client = clients.at(fd);
            bool keep = true;

            if (client.name.empty() && std::chrono::steady_clock::now() - client.connected_at >= LOGIN_TIMEOUT) {
                std::cout << "Unauthenticated client timed out" << std::endl;
                keep = false;
            } else {
                if (!(fds[i].revents & (POLLIN | POLLERR | POLLHUP))) continue;
                ssize_t n = recv(fd, recv_buffer, sizeof(recv_buffer), 0);
                if (n < 0) {
                    if (errno == EINTR) continue;
                    std::cerr << "Error receiving data: " << strerror(errno) << std::endl;
                    keep = false;
                } else if (n == 0) {
                    if (!client.name.empty()) std::cout << "Client " << client.name << " disconnected" << std::endl;
                    keep = false;
                } else {
                    client.buffer.append(recv_buffer, static_cast<std::size_t>(n));
                    keep = process_buffer(client, fds[0].fd, clients);
                }
            }

            if (!keep) {
                clients.erase(fd);
                fds.erase(fds.begin() + static_cast<std::ptrdiff_t>(i));
            }
        }
    }
    return 0;
}
