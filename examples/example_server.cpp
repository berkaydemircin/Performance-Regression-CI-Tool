#include <array>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

volatile std::sig_atomic_t stopping = 0;

extern "C" void stopServer(int) {
    stopping = 1;
}

std::uint16_t parsePort(const int argc, char** argv) {
    for (int index = 1; index + 1 < argc; ++index) {
        if (std::string{argv[index]} == "--port") {
            return static_cast<std::uint16_t>(std::stoul(argv[index + 1]));
        }
    }
    return 19090;
}

bool candidateMode(const int argc, char** argv) {
    for (int index = 1; index + 1 < argc; ++index) {
        if (std::string{argv[index]} == "--mode") {
            return std::string{argv[index + 1]} == "candidate";
        }
    }
    return false;
}

void sendAll(const int descriptor, const std::byte* data, const std::size_t size) {
    std::size_t sent = 0;
    while (sent < size) {
        const ssize_t count = send(descriptor, data + sent, size - sent, MSG_NOSIGNAL);
        if (count <= 0) {
            return;
        }
        sent += static_cast<std::size_t>(count);
    }
}

}

int main(const int argc, char** argv) {
    struct sigaction action{};
    action.sa_handler = stopServer;
    sigemptyset(&action.sa_mask);
    sigaction(SIGTERM, &action, nullptr);
    sigaction(SIGINT, &action, nullptr);

    const bool candidate = candidateMode(argc, argv);
    const int listener = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (listener == -1) {
        throw std::runtime_error(std::strerror(errno));
    }
    const int reuse = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(parsePort(argc, argv));
    if (bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == -1 ||
        listen(listener, 8) == -1) {
        close(listener);
        throw std::runtime_error(std::strerror(errno));
    }

    while (!stopping) {
        const int client = accept4(listener, nullptr, nullptr, SOCK_CLOEXEC);
        if (client == -1) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        std::array<std::byte, 8> message{};
        while (!stopping) {
            const ssize_t count = recv(client, message.data(), message.size(), MSG_WAITALL);
            if (count <= 0) {
                break;
            }
            if (candidate) {
                std::uint64_t value = 7;
                for (std::uint64_t index = 0; index < 25'000; ++index) {
                    value = value * 2862933555777941757ULL + 3037000493ULL;
                }
                std::memcpy(message.data(), &value, sizeof(value));
            }
            sendAll(client, message.data(), message.size());
        }
        close(client);
    }
    close(listener);
}
