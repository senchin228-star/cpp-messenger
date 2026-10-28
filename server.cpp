#include <string>
#include <iostream>
#include <vector>
#include <map>
#include <set>
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

using RoomMembers = std::map<std::string, std::set<std::string>>;

void broadcast(const RoomMembers& rooms, const std::map<int, Client>& clients,
               const std::string& room, const std::string& payload) {
    auto room_it = rooms.find(room);
    if (room_it == rooms.end()) return;

    for (const std::string& member : room_it->second) {
        auto client_it = std::find_if(clients.begin(), clients.end(),
            [&member](const auto& entry) {
                return !entry.second.name.empty() && lowercase(entry.second.name) == member;
            });
        if (client_it != clients.end() && !send_all(client_it->first, payload)) {
            std::cerr << "Send to " << client_it->second.name << " failed: "
                      << strerror(errno) << std::endl;
        }
    }
}

void send_system(const RoomMembers& rooms, const std::map<int, Client>& clients,
                 const std::string& room, const std::string& message) {
    nlohmann::json event = {{"type", "system"}, {"room", room}, {"message", message}};
    broadcast(rooms, clients, room, event.dump() + '\n');
}

bool process_buffer(Client& client, const std::map<int, Client>& clients, RoomMembers& rooms) {
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
                    const std::string user_key = lowercase(client.name);
                    const bool first_join = rooms["general"].insert(user_key).second;
                    if (first_join) send_system(rooms, clients, "general", client.name + " joined");
                    std::cout << username << " Logined" << std::endl;
                }
            }
            if (client.name.empty()) continue;
            if (type == "join") {
                std::string room = j.at("room").get<std::string>();
                if (room.empty() || room.size() > 64 || room.find_first_of(" \t\r\n") != std::string::npos) {
                    nlohmann::json error = {{"type", "error"}, {"er_text", "Invalid room name"}};
                    send_all(client.get_sock(), error.dump() + '\n');
                    continue;
                }
                if (rooms[room].insert(lowercase(client.name)).second) {
                    send_system(rooms, clients, room, client.name + " joined");
                }
                continue;
            }
            if (type == "leave") {
                const std::string room = j.at("room").get<std::string>();
                auto room_it = rooms.find(room);
                if (room_it == rooms.end() || room_it->second.erase(lowercase(client.name)) == 0) {
                    nlohmann::json error = {{"type", "error"}, {"er_text", "You are not in room: " + room}};
                    send_all(client.get_sock(), error.dump() + '\n');
                    continue;
                }
                if (!room_it->second.empty()) {
                    send_system(rooms, clients, room, client.name + " left");
                } else {
                    rooms.erase(room_it);
                }
                continue;
            }
            if (type == "rooms") {
                nlohmann::json response = {{"type", "rooms"}, {"rooms", nlohmann::json::array()}};
                const std::string user_key = lowercase(client.name);
                for (const auto& [room, members] : rooms) {
                    if (members.count(user_key) != 0) response["rooms"].push_back(room);
                }
                send_all(client.get_sock(), response.dump() + '\n');
                continue;
            }
            if (type == "private") {
                const std::string recipient_name = j.at("to").get<std::string>();
                if (recipient_name.empty()) {
                    nlohmann::json error {
                        {"type", "error"},
                        {"er_text", "Empty user name"}
                    };
                    send_all(client.get_sock(), error.dump() + '\n');
                    continue;
                }
                const std::string private_message = j.at("message").get<std::string>();
                auto recipient = std::find_if(clients.begin(), clients.end(),
                    [&recipient_name](const auto& entry) {
                        return entry.second.name == recipient_name;
                    });
                if (recipient == clients.end()) {
                    nlohmann::json error = {
                        {"type", "error"},
                        {"er_text", "User not found: " + recipient_name}
                    };
                    send_all(client.get_sock(), error.dump() + '\n');
                    continue;
                }
                out = {
                    {"type", "private"},
                    {"from", client.name},
                    {"message", private_message}
                };
                if (!send_all(recipient->first, out.dump() + '\n')) {
                    std::cerr << "Send private message to " << recipient_name
                              << " failed: " << strerror(errno) << std::endl;
                }
            }
            if (type == "text"){
                const std::string room = j.at("room").get<std::string>();
                const std::string user_key = lowercase(client.name);
                auto room_it = rooms.find(room);
                if (room_it == rooms.end() || room_it->second.count(user_key) == 0) {
                    nlohmann::json error = {{"type", "error"}, {"er_text", "Join room before sending: " + room}};
                    send_all(client.get_sock(), error.dump() + '\n');
                    continue;
                }
                std::cout << "type: " << j.at("type").get<std::string>() + "   "
                        << client.name << ": "
                        << j.at("message").get<std::string>() << std::endl;
                out["type"] = "text";
                out["message"] = j.at("message").get<std::string>();
                out["nickname"] = client.name;
                out["room"] = room;
                broadcast(rooms, clients, room, out.dump() + "\n");
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
    RoomMembers rooms;
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
                    keep = process_buffer(client, clients, rooms);
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
