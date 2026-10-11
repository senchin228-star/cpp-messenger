#include <string>
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <cstring>
#include <cerrno>
#include <nlohmann/json.hpp>
#include "socket.hpp"

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

    struct sockaddr_in client_addr = {};
    socklen_t client_addr_len = sizeof(client_addr);

    Socket client_sock(accept(listen_sock.get(),
     reinterpret_cast<struct sockaddr*>(&client_addr), &client_addr_len));
    if (client_sock.get() < 0) {
        std::cerr << "Error accepting connection: " << strerror(errno) << std::endl;
        return 1;
    }

    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
    std::cout << "Accepted connection from " << client_ip << ":" << ntohs(client_addr.sin_port) << std::endl;

    constexpr std::size_t MAX_BUFFER = 4096;
    std::string buffer;
    char recv_buffer[4096];
    while (true) {
        ssize_t bytes_received = recv(client_sock.get(), recv_buffer, sizeof(recv_buffer), 0);
        if (bytes_received < 0) {
            if (errno == EINTR) {
                continue; // Interrupted by signal, retry
            }
            return 1;
        }
        if (bytes_received == 0) {
            std::cout << "Client disconnected" << std::endl;
            break;
        }
        buffer.append(recv_buffer, static_cast<std::size_t>(bytes_received));
        std::size_t pos;
        while ((pos = buffer.find('\n')) != std::string::npos) {
            std::string line = buffer.substr(0, pos);
            buffer.erase(0, pos + 1);
            if (line.empty()) continue;
            try{
                auto j = nlohmann::json::parse(line);
                std::cout << j.at("type").get<std::string>() << ": ";
                std::cout << j.at("message").get<std::string>() << std::endl;
            }catch (const nlohmann::json::exception& e){
                std::cerr <<"bad message" << e.what() << std::endl;
            }
        }
        if (buffer.size() > MAX_BUFFER) {
            std::cerr << "Message too long, droping clinet" << std::endl;
            break;
        }
    }
    return 0;
}