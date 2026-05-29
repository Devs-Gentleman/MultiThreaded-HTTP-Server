#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

namespace {
constexpr int kPort = 8080;    // TCP port number for the server
constexpr int kBacklog = 16;   // Max queued connections (listen backlog)

bool send_all(int fd, const char* data, size_t size) {  // fd = file descriptor (socket handle)
    size_t total_sent = 0;
    while (total_sent < size) {
        // send() is the POSIX system call that sends bytes on a socket.
        ssize_t sent = ::send(fd, data + total_sent, size - total_sent, 0);
        if (sent < 0) {
            if (errno == EINTR) {  // EINTR = Interrupted system call, try again.
                continue;
            }
            return false;
        }
        if (sent == 0) {
            return false;
        }
        total_sent += static_cast<size_t>(sent);  // sent is signed; we already know it's > 0.
    }
    return true;
}
}  // namespace

int main() {
    // socket() creates a TCP/IPv4 socket: AF_INET = IPv4, SOCK_STREAM = TCP.
    int server_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::cerr << "socket failed: " << std::strerror(errno) << "\n";
        return 1;
    }

    int opt = 1;
    // setsockopt() = set socket option, SOL_SOCKET = socket-level option.
    // SO_REUSEADDR lets us rebind quickly after restart.
    if (::setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        std::cerr << "setsockopt(SO_REUSEADDR) failed: " << std::strerror(errno) << "\n";
    }   
#ifdef SO_REUSEPORT
    // SO_REUSEPORT allows multiple sockets to bind the same port (if supported).
    if (::setsockopt(server_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)) < 0) {
        std::cerr << "setsockopt(SO_REUSEPORT) failed: " << std::strerror(errno) << "\n";
    }
#endif
    
    sockaddr_in addr{};               // sockaddr_in = IPv4 socket address struct
    addr.sin_family = AF_INET;        // AF_INET = IPv4
    addr.sin_addr.s_addr = htonl(INADDR_ANY);  // INADDR_ANY = bind all interfaces
    addr.sin_port = htons(kPort);     // htons = host-to-network short (byte order)

    // bind() assigns the address/port to the socket.
    if (::bind(server_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "bind failed: " << std::strerror(errno) << "\n";
        ::close(server_fd);
        return 1;
    }

    // listen() turns the socket into a server that can accept connections.
    if (::listen(server_fd, kBacklog) < 0) {
        std::cerr << "listen failed: " << std::strerror(errno) << "\n";
        ::close(server_fd);
        return 1;
    }

    std::cout << "Listening on port " << kPort << "...\n";

    const std::string response =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: 12\r\n"
        "Connection: close\r\n"
        "\r\n"
        "Hello World\n";

    while (true) {
        sockaddr_in client_addr{};  // Client IPv4 address info
        socklen_t client_len = sizeof(client_addr);  // socklen_t = length type for sockets
        // accept() waits for a client and returns a new socket for that connection.
        int client_fd = ::accept(server_fd, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
        if (client_fd < 0) {
            if (errno == EINTR) {  // Interrupted system call
                continue;
            }
            std::cerr << "accept failed: " << std::strerror(errno) << "\n";
            continue;
        }

        if (!send_all(client_fd, response.data(), response.size())) {
            std::cerr << "send failed: " << std::strerror(errno) << "\n";
        }

        ::close(client_fd);  // close() releases the client socket
    }

    ::close(server_fd);  // close() releases the server socket
    return 0;
}
