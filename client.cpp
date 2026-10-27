#include <string>
#include <sstream>
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <nlohmann/json.hpp>
#include <cerrno>
#include <poll.h>
#include "socket.hpp"
#include "net.hpp"


std::string trim(const std::string& str) {
    auto first = str.find_first_not_of(" \t\n\r");
    if (first == std::string::npos) {
        return ""; // String is all whitespace
    }
    auto last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, last - first + 1);
}
bool handle_server_data(std::string& buffer)
{
    std::size_t pos;
    while ((pos = buffer.find('\n')) != std::string::npos){
        std::string line = buffer.substr(0,pos);
        buffer.erase(0, pos + 1);
        try{
            auto j = nlohmann::json::parse(line);
            std::string type = j.at("type").get<std::string>();
            if (type.empty()) continue;
            if (type == "text" && j.contains("nickname")){
                std::cout << j.at("nickname").get<std::string>() << ": " <<  j.at("message").get<std::string>() << std::endl;
            }
            else if (type == "private") {
                std::cout << "Private from " << j.at("from").get<std::string>()
                          << ": " << j.at("message").get<std::string>() << std::endl;
            }
            else if (type == "login_result"){
                if (j.at("ok").get<bool>()){
                    std::cout << "Login success" << std::endl;
                }
                else{
                    std::cout << "Login unsucces. Reason:" << j.at("reason").get<std::string>() << '\n' << std::endl;
                    return false;
                }
            }
            else if (type == "error"){
                std::cout << j.at("er_text") << std::endl;
            }
            else std::cout << "Unknown type" << std::endl;
        }
        catch(const nlohmann::json::exception& e){
            std::cerr << "Bad message: " << e.what() << std::endl;
        }
    }
    return true;
}
int main(int argc, char *argv[]){
    if (argc < 2){
        std::cout << "A nickname is required as the first argument." << std::endl;
        return 1;
    }
    if (strlen(argv[1]) > 16){
        std::cout << "Nickname is too long" << std::endl;
        return 1;
    }
    Socket sock(socket(AF_INET, SOCK_STREAM, 0));
    if (sock.get() < 0) {
        std::cerr << "Error creating socket" << std::endl;
        return 1;
    }

    struct sockaddr_in server_addr = {};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(5050);
    if (inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr) <= 0) {
        std::cerr << "Invalid address/ Address not supported" << std::endl;
        return 1;
    }
    if (connect(sock.get(), (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "Error connecting to server" << '\n';
        return 1;
    }
    std::cout << "Connected to server!" << std::endl;

    char recv_buffer[4096];
    std::string buffer;
    nlohmann::json join_request_js = {
        {"type", "login"},
        {"user", argv[1]}
    };
    std::string join_request_str = join_request_js.dump() + '\n';
    if (!send_all(sock.get(), join_request_str)) return 1;
    while (buffer.find('\n') == std::string::npos){
        ssize_t n = recv(sock.get(), recv_buffer, sizeof(recv_buffer), 0);
        if (n < 0){
            if (errno == EINTR) continue;
            std::cerr << "Error receiving data: " << strerror(errno) << std::endl;
            return 1;
        }
        if (n == 0){
            std::cout << "Server closed the connection" << std::endl;
            return 1;
        }
        buffer.append(recv_buffer, static_cast<std::size_t>(n));
    }
    if (!handle_server_data(buffer)) return 1;

    std::string input;

    pollfd fds[2];
    fds[0] = {STDIN_FILENO, POLLIN, 0};
    fds[1] = {sock.get(), POLLIN, 0};
    std::cout << "Enter message: " << std::endl;
    while (true){
        if (poll(fds, 2, -1)  < 0){
            if (errno == EINTR) continue;
            std::cerr << "poll error: " << strerror(errno) << std::endl;
            return 1;
        }
        if (fds[1].revents &  (POLLIN | POLLHUP | POLLERR)) {
            ssize_t n = recv(sock.get(), recv_buffer, sizeof(recv_buffer), 0);
            if  (n < 0){
                if (errno == EINTR) continue;
                std::cerr << "Error receiving data: " << strerror(errno) << std::endl;
                return 1;
            }
            if  (n ==  0){
                std::cout << "Server closed the connection" << std::endl;
                break;
            }
            buffer.append(recv_buffer, static_cast<std::size_t>(n));
            handle_server_data(buffer);
        }
        if (fds[0].revents  & POLLIN){
            if (!std::getline(std::cin, input)) break;
            std::cout << "Enter message: " << buffer << std::endl;
            input = trim(input);
            if (input.empty()) {
                std::cout << "Skipping empty message." << std::endl;
                continue;
            }
            nlohmann::json json_message;
            std::istringstream input_stream(input);
            std::string command;
            input_stream >> command;
            if (command == "/msg") {
                std::string recipient;
                if (!(input_stream >> recipient)) {
                    std::cout << "Usage: /msg <nickname> <message>" << std::endl;
                    continue;
                }
                std::string private_message;
                std::getline(input_stream, private_message);
                private_message = trim(private_message);
                if (private_message.empty()) {
                    std::cout << "Private message cannot be empty." << std::endl;
                    continue;
                }
                json_message["type"] = "private";
                json_message["to"] = recipient;
                json_message["message"] = private_message;
            } else {
                json_message["type"] = "text";
                json_message["message"] = input;
            }
            std::string message = json_message.dump() + "\n";
            send_all(sock.get(), message);
        }
    }
    return 0;
}