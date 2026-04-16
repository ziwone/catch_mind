#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {

void print_usage(const char* prog) {
    std::cout << "Usage:\n";
    std::cout << "  " << prog
              << " send <port> <target_ip1> [target_ip2 ...]\n";
}

bool parse_port(const std::string& text, int& port) {
    try {
        int value = std::stoi(text);
        if (value < 1 || value > 65535) {
            return false;
        }
        port = value;
        return true;
    } catch (...) {
        return false;
    }
}

int make_udp_socket_bound(int port) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        std::perror("socket");
        return -1;
    }

    int reuse = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        std::perror("setsockopt(SO_REUSEADDR)");
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(static_cast<uint16_t>(port));

    if (bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::perror("bind");
        close(sock);
        return -1;
    }

    return sock;
}

int run_send(int port, const std::vector<std::string>& target_ips) {
    int sock = make_udp_socket_bound(port);
    if (sock < 0) {
        return 1;
    }

    const std::string ping_msg = "PING";
    int ok_count = 0;
    int fail_count = 0;

    for (const auto& ip : target_ips) {
        sockaddr_in dst{};
        dst.sin_family = AF_INET;
        dst.sin_port = htons(static_cast<uint16_t>(port));
        if (inet_pton(AF_INET, ip.c_str(), &dst.sin_addr) != 1) {
            std::cerr << "[send] invalid ip: " << ip << "\n";
            fail_count++;
            continue;
        }

        ssize_t sent = sendto(
            sock,
            ping_msg.data(),
            ping_msg.size(),
            0,
            reinterpret_cast<sockaddr*>(&dst),
            sizeof(dst));

        if (sent < 0) {
            std::cerr << "[send] FAILED -> " << ip << " : "
                      << std::strerror(errno) << "\n";
            fail_count++;
        } else {
            std::cout << "[send] OK -> " << ip << " (" << sent << " bytes)\n";
            ok_count++;
        }
    }

    std::cout << "\n[send] summary: OK=" << ok_count << ", FAIL=" << fail_count << "\n";
    std::cout << "[send] one-way mode: no response(wait) check\n";

    close(sock);
    return (fail_count == 0) ? 0 : 2;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        print_usage(argv[0]);
        return 1;
    }

    std::string mode = argv[1];
    int port = 0;
    if (!parse_port(argv[2], port)) {
        std::cerr << "invalid port: " << argv[2] << "\n";
        return 1;
    }

    if (mode == "send") {
        if (argc < 4) {
            print_usage(argv[0]);
            return 1;
        }
        std::vector<std::string> ips;
        for (int i = 3; i < argc; ++i) {
            ips.emplace_back(argv[i]);
        }
        return run_send(port, ips);
    }

    print_usage(argv[0]);
    return 1;
}
