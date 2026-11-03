#include <string>
#include <iostream>
#include <fstream>
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
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <openssl/sha.h>
#include <iomanip>
#include "socket.hpp"
#include "net.hpp"

constexpr std::size_t MAX_BUFFER = 10 * 1024 * 1024; // 10 MB
constexpr auto LOGIN_TIMEOUT = std::chrono::milliseconds(1000);
constexpr const char* HISTORY_FILE = "messages.json";
constexpr std::size_t MAX_ROOM_HISTORY = 200;
constexpr std::size_t MAX_PRIVATE_HISTORY = 500;

// Структура для долговременного хранения истории сообщений мессенджера
struct ServerHistory {
    uint64_t next_id = 1;                              // Уникальный ID для каждого сообщения
    std::map<std::string, std::string> users_auth; // Известные пользователи (user -> password)
    std::map<std::string, std::vector<nlohmann::json>> rooms; // История по комнатам (room -> массив сообщений)
    std::map<std::string, std::set<std::string>> users_rooms; // Пользователи и комнаты, в которых они находятся (user -> set<room>)
    std::vector<nlohmann::json> privates;              // История личных сообщений (from, to, message, time, id)
};

// Функция получения текущего времени в формате "ЧЧ:ММ"
std::string get_current_time_str() {
    auto now = std::chrono::system_clock::now();
    std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
    localtime_r(&now_time, &tm_buf);
    std::ostringstream oss;
    oss << std::setfill('0') << std::setw(2) << tm_buf.tm_hour << ":"
        << std::setfill('0') << std::setw(2) << tm_buf.tm_min;
    return oss.str();
}

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

// Функция для вычисления SHA-256 хэша строки
std::string sha256(const std::string& str) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(str.data()), str.size(), hash);
    std::ostringstream ss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }
    return ss.str();
}

// Загрузка сохраненной истории из JSON файла при запуске сервера
void load_history(const std::string& path, ServerHistory& hist) {
    std::ifstream f(path);
    if (!f.is_open()) {
        std::cout << "Файл истории не найден (" << path << "), будет создан при новых сообщениях." << std::endl;
        return;
    }
    try {
        nlohmann::json j;
        f >> j;
        if (j.contains("users") && !j["users"].is_null()) {
            if (j["users"].is_object()) {
                for (auto it = j["users"].begin(); it != j["users"].end(); ++it) {
                    if (it.value().is_string()) {
                        hist.users_auth[lowercase(it.key())] = it.value().get<std::string>();
                    }
                }
            } else if (j["users"].is_array()) {
                for (const auto& u : j["users"]) {
                    if (u.is_object() && u.contains("user") && u.contains("password")) {
                        std::string user = lowercase(u.at("user").get<std::string>());
                        std::string password = u.at("password").get<std::string>();
                        hist.users_auth[user] = password;
                    }
                }
            }
        }
        if (j.contains("rooms") && j["rooms"].is_object()) {
            for (auto it = j["rooms"].begin(); it != j["rooms"].end(); ++it) {
                if (it.value().is_array()) {
                    for (const auto& msg : it.value()) {
                        hist.rooms[it.key()].push_back(msg);
                        if (msg.contains("id") && msg["id"].is_number_unsigned()) {
                            hist.next_id = std::max(hist.next_id, msg["id"].get<uint64_t>() + 1);
                        }
                    }
                }
            }
        }
        if (j.contains("privates") && j["privates"].is_array()) {
            for (const auto& msg : j["privates"]) {
                hist.privates.push_back(msg);
                if (msg.contains("id") && msg["id"].is_number_unsigned()) {
                    hist.next_id = std::max(hist.next_id, msg["id"].get<uint64_t>() + 1);
                }
            }
        }
        std::cout << "Успешно загружена история: "
                  << hist.rooms.size() << " комнат(ы), "
                  << hist.privates.size() << " личных сообщений, "
                  << hist.users_auth.size() << " известных пользователей." << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Ошибка при чтении файла истории " << path << ": " << e.what() << std::endl;
    }
}

// Сохранение всей текущей истории в JSON файл
void save_history(const std::string& path, const ServerHistory& hist) {
    try {
        nlohmann::json j;
        j["users"] = hist.users_auth;
        j["rooms"] = hist.rooms;
        j["privates"] = hist.privates;
        std::ofstream f(path);
        if (f.is_open()) {
            f << j.dump(2) << std::endl;
        } else {
            std::cerr << "Не удалось открыть файл для записи: " << path << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "Исключение при сохранении истории: " << e.what() << std::endl;
    }
}

// Отправка клиенту сохраненных сообщений комнаты
bool send_room_history(int client_fd, const std::string& room, const ServerHistory& hist) {
    auto it = hist.rooms.find(room);
    if (it == hist.rooms.end()) return true;
    for (const auto& msg : it->second) {
        nlohmann::json hist_msg = msg;
        hist_msg["history"] = true;
        if (!send_all(client_fd, hist_msg.dump() + '\n')) {
            return false;
        }
    }
    return true;
}

// Отправка клиенту сохраненных личных сообщений (входящих и исходящих)
bool send_private_history(int client_fd, const std::string& username, const ServerHistory& hist) {
    const std::string user_lower = lowercase(username);
    for (const auto& msg : hist.privates) {
        std::string from = msg.value("from", "");
        std::string to = msg.value("to", "");
        if (lowercase(from) == user_lower || lowercase(to) == user_lower) {
            nlohmann::json hist_msg = msg;
            hist_msg["history"] = true;
            if (!send_all(client_fd, hist_msg.dump() + '\n')) {
                return false;
            }
        }
    }
    return true;
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

bool process_buffer(Client& client, const std::map<int, Client>& clients, RoomMembers& rooms, ServerHistory& hist) {
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
                const std::string password = j.value("password", "");
                if (username.size() > 16) reason = "Username too long";
                if (!client.name.empty()) reason = "User is already logged in";
                else if (username.empty()) reason = "Empty user name";
                else if (hist.users_auth.find(username_lower) == hist.users_auth.end()) reason = "User not registered";
                else {
                    std::string password_hash = sha256(password);
                    if (hist.users_auth[username_lower] != password_hash) reason = "Incorrect password";
                    else {
                        for (const auto& [other_fd, other_client] : clients){
                            if (other_fd != client.get_sock() && lowercase(other_client.name) == username_lower){
                                reason = "Already logged in";
                                break;
                            }
                        }
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

                    // Добавляем пользователя в список известных (зарегистрированных)
                    hist.users_auth[user_key] = hist.users_auth[username_lower]; // сохраняем пароль
                    hist.users_rooms[user_key].insert("general");
                    save_history(HISTORY_FILE, hist);

                    for (const auto& room : hist.users_rooms[user_key]) {
                        // Отправляем клиенту сохраненную историю сообщений каждой комнаты, в которой он состоит
                        if (rooms.find(room) == rooms.end()) continue; // Пропускаем комнаты, которые больше не существуют
                        if (!send_room_history(client.get_sock(), room, hist)) return false;
                    }
                    // Отправляем клиенту сохраненные личные сообщения (где он отправитель или получатель)
                    if (!send_private_history(client.get_sock(), client.name, hist)) return false;
                }
            }
            if (type == "register"){
                std::string username = j.value("user", "");
                username = trim(username);
                const std::string username_lower = lowercase(username);
                std::string reason;
                if (username.empty()) reason = "Empty user name";
                else if (username.size() > 16) reason = "Username too long";
                else if (hist.users_auth.find(username_lower) != hist.users_auth.end()) reason = "User already registered";
                else if (!j.contains("password") || !j["password"].is_string() || j["password"].get<std::string>().empty()) reason = "Password is required";
                else if (j["password"].get<std::string>().size() > 64) reason = "Password is too long";
                else {
                    std::string password = j.at("password").get<std::string>();
                    std::string password_hash = sha256(password);
                    hist.users_auth[username_lower] = password_hash;
                    save_history(HISTORY_FILE, hist);
                    std::cout << username << " Registered" << std::endl;
                }

                out = {
                    {"type", "register_result"},
                    {"ok", reason.empty()},
                    {"reason", reason}
                };
                std::string reg_res = out.dump() + '\n';
                if (!send_all(client.get_sock(), reg_res)) return false;
                if (!reason.empty()){
                    std::cout << username << ": Register unsucces. Reason: "<< reason << std::endl;
                    return false;
                }
                continue;
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
                    // Отправляем новому участнику сохраненную историю сообщений этой комнаты
                    if (!send_room_history(client.get_sock(), room, hist)) return false;
                }
                hist.users_rooms[lowercase(client.name)].insert(room);
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
            if (type == "users") {
                nlohmann::json response = {{"type", "users"}, {"users", nlohmann::json::array()}};
                for (const auto& [other_fd, other_client] : clients) {
                    if (!other_client.name.empty()) {
                        response["users"].push_back(other_client.name);
                    }
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
                if (lowercase(recipient_name) == lowercase(client.name)) {
                    nlohmann::json error {
                        {"type", "error"},
                        {"er_text", "Cannot send private message to yourself"}
                    };
                    send_all(client.get_sock(), error.dump() + '\n');
                    continue;
                }
                const std::string private_message = j.at("message").get<std::string>();
                auto recipient = std::find_if(clients.begin(), clients.end(),
                    [&recipient_name](const auto& entry) {
                        return lowercase(entry.second.name) == lowercase(recipient_name);
                    });

                const bool is_online = (recipient != clients.end());
                const bool is_known = is_online || (hist.users_auth.find(lowercase(recipient_name)) != hist.users_auth.end());

                if (!is_known) {
                    nlohmann::json error = {
                        {"type", "error"},
                        {"er_text", "User not found: " + recipient_name}
                    };
                    send_all(client.get_sock(), error.dump() + '\n');
                    continue;
                }

                std::string time_str = get_current_time_str();

                out = {
                    {"type", "private"},
                    {"id", hist.next_id++},
                    {"from", client.name},
                    {"to", recipient_name},
                    {"message", private_message},
                    {"time", time_str}
                };
                if (j.contains("image")) {
                    out["image"] = j.at("image");
                }

                // Сохраняем личное сообщение в историю сервера
                hist.privates.push_back(out);
                if (hist.privates.size() > MAX_PRIVATE_HISTORY) {
                    hist.privates.erase(hist.privates.begin());
                }
                save_history(HISTORY_FILE, hist);

                // Если получатель онлайн — доставляем в реальном времени
                if (is_online) {
                    if (!send_all(recipient->first, out.dump() + '\n')) {
                        std::cerr << "Send private message to " << recipient_name
                                  << " failed: " << strerror(errno) << std::endl;
                    }
                }
                continue;
            }
            if (type == "message"  || type == "text" || type == "image") {
                const std::string room = j.at("room").get<std::string>();
                const std::string user_key = lowercase(client.name);
                auto room_it = rooms.find(room);
                if (room_it == rooms.end() || room_it->second.count(user_key) == 0) {
                    nlohmann::json error = {{"type", "error"}, {"er_text", "Join room before sending: " + room}};
                    send_all(client.get_sock(), error.dump() + '\n');
                    continue;
                }
                if (hist.users_auth.find(user_key) == hist.users_auth.end()) {
                    nlohmann::json error = {{"type", "error"}, {"er_text", "User not registered: " + client.name}};
                    send_all(client.get_sock(), error.dump() + '\n');
                    continue;
                }

                std::string time_str = get_current_time_str();
                std::cout << "[" << time_str << "] [" << room << "] "
                          << client.name << ": "
                          << j.at("message").get<std::string>() << std::endl;

                out["type"] = "text";
                out["id"] = hist.next_id++;
                out["message"] = j.at("message").get<std::string>();
                out["nickname"] = client.name;
                out["room"] = room;
                out["time"] = time_str;
                if (j.contains("image")) {
                    out["image"] = j.at("image");
                }

                // Сохраняем сообщение комнаты в историю на сервере
                hist.rooms[room].push_back(out);
                if (hist.rooms[room].size() > MAX_ROOM_HISTORY) {
                    hist.rooms[room].erase(hist.rooms[room].begin());
                }
                save_history(HISTORY_FILE, hist);

                broadcast(rooms, clients, room, out.dump() + "\n");
                continue;
            }

            if (type == "leave"){
                const std::string room = j.at("room").get<std::string>();
                const std::string user_key = lowercase(client.name);
                auto room_it = rooms.find(room);
                if (room_it == rooms.end() || room_it->second.count(user_key) == 0) {
                    nlohmann::json error = {{"type", "error"}, {"er_text", "Join room before sending: " + room}};
                    send_all(client.get_sock(), error.dump() + '\n');
                    continue;
                }
                std::string time_str = get_current_time_str();
                std::cout << "[" << time_str << "] [" << room << "] "
                          << client.name << " left the room." << std::endl;

                rooms[room].erase(user_key);
                hist.users_rooms[user_key].erase(room);
                nlohmann::json out = {
                    {"type", "leave"},
                    {"user", user_key},
                    {"room", room},
                    {"time", time_str}
                };

                send_system(rooms, clients, room, client.name + " left");
                hist.rooms[room].push_back(out);
                if (hist.rooms[room].size() > MAX_ROOM_HISTORY) {
                    hist.rooms[room].erase(hist.rooms[room].begin());
                }
                save_history(HISTORY_FILE, hist);

                broadcast(rooms, clients, room, out.dump() + "\n");
                continue;
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
    ServerHistory history;
    load_history(HISTORY_FILE, history);
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
            clients.emplace(fd, Client(std::move(client_sock)));
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
                    keep = process_buffer(client, clients, rooms, history);
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
