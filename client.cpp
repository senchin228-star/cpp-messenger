#include <string>
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <nlohmann/json.hpp>
#include <cerrno>
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
int main(){
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
    std::string buffer;
    while (std::getline(std::cin, buffer)) {
        std::cout << "Enter message: " << buffer << std::endl;
        buffer = trim(buffer);
        if (buffer.empty()) {
            std::cout << "Skipping empty message." << std::endl;
            continue;
        }
        nlohmann::json json_message;
        json_message["message"] = buffer;
        json_message["type"] = "text";

        std::string message = json_message.dump() + "\n";
        send_all(sock.get(), message);
    }
    return 0;
}