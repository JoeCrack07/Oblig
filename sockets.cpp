// Socket.cpp
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include "Socket.h"
#include <iostream>

Socket::Socket()
    : broadcast_socket(INVALID_SOCKET),
    multicast_socket(INVALID_SOCKET) {
    InitWinsock();

    // ===== BROADCAST SOCKET (PORT 50000) =====
    broadcast_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (broadcast_socket == INVALID_SOCKET) {
        std::cerr << "Failed to create broadcast socket" << std::endl;
        return;
    }

    BOOL reuseAddr = TRUE;
    if (setsockopt(broadcast_socket, SOL_SOCKET, SO_REUSEADDR,
        (const char*)&reuseAddr, sizeof(reuseAddr)) == SOCKET_ERROR) {
        std::cerr << "setsockopt(broadcast, SO_REUSEADDR) failed: " << WSAGetLastError() << std::endl;
    }

    sockaddr_in localAddr{};
    localAddr.sin_family = AF_INET;
    localAddr.sin_port = htons(50000);
    localAddr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(broadcast_socket, (sockaddr*)&localAddr, sizeof(localAddr)) == SOCKET_ERROR) {
        std::cerr << "bind(broadcast) failed: " << WSAGetLastError() << std::endl;
        closesocket(broadcast_socket);
        return;
    }

    BOOL broadcastEnable = TRUE;
    if (setsockopt(broadcast_socket, SOL_SOCKET, SO_BROADCAST,
        (const char*)&broadcastEnable, sizeof(broadcastEnable)) == SOCKET_ERROR) {
        std::cerr << "setsockopt(broadcast, SO_BROADCAST) failed: " << WSAGetLastError() << std::endl;
    }

    u_long mode = 1;
    if (ioctlsocket(broadcast_socket, FIONBIO, &mode) == SOCKET_ERROR) {
        std::cerr << "Failed to set non-blocking mode on broadcast socket" << std::endl;
    }

    // ===== MULTICAST SOCKET (PORT 50000) =====
    multicast_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (multicast_socket == INVALID_SOCKET) {
        std::cerr << "Failed to create multicast socket" << std::endl;
        return;
    }

    BOOL reuse = TRUE;
    if (setsockopt(multicast_socket, SOL_SOCKET, SO_REUSEADDR,
        (const char*)&reuse, sizeof(reuse)) == SOCKET_ERROR) {
        std::cerr << "setsockopt(multicast, SO_REUSEADDR) failed: " << WSAGetLastError() << std::endl;
    }

    sockaddr_in mcastAddr{};
    mcastAddr.sin_family = AF_INET;
    mcastAddr.sin_port = htons(50000);
    mcastAddr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(multicast_socket, (sockaddr*)&mcastAddr, sizeof(mcastAddr)) == SOCKET_ERROR) {
        std::cerr << "bind(multicast) failed: " << WSAGetLastError() << std::endl;
        closesocket(multicast_socket);
        return;
    }

    DWORD loop = 1;
    if (setsockopt(multicast_socket, IPPROTO_IP, IP_MULTICAST_LOOP,
        (const char*)&loop, sizeof(loop)) == SOCKET_ERROR) {
        std::cerr << "setsockopt(IP_MULTICAST_LOOP) failed" << std::endl;
    }

    u_long mcast_mode = 1;
    if (ioctlsocket(multicast_socket, FIONBIO, &mcast_mode) == SOCKET_ERROR) {
        std::cerr << "Failed to set non-blocking mode on multicast socket" << std::endl;
    }
}

Socket::~Socket() {
    if (broadcast_socket != INVALID_SOCKET) {
        closesocket(broadcast_socket);
    }
    if (multicast_socket != INVALID_SOCKET) {
        closesocket(multicast_socket);
    }
    WSACleanup();
}

void Socket::InitWinsock() {
    WSADATA wsa{};
    int result = WSAStartup(MAKEWORD(2, 2), &wsa);
    if (result != 0) {
        std::cerr << "WSAStartup failed: " << result << std::endl;
        // Optionally, handle the error (e.g., throw, exit, or set a flag)
    }
}

void Socket::SendBroadcast(const std::string& msg) {
    sockaddr_in broadcastAddr{};
    broadcastAddr.sin_family = AF_INET;
    broadcastAddr.sin_addr.s_addr = inet_addr("192.168.41.255");
    broadcastAddr.sin_port = htons(50000);

    std::string message = msg + "\n";

    int result = sendto(broadcast_socket, message.c_str(),
        (int)message.length(), 0,
        (sockaddr*)&broadcastAddr, sizeof(broadcastAddr));

    if (result == SOCKET_ERROR) {
        std::cerr << "sendto(broadcast) failed: " << WSAGetLastError() << std::endl;
    }
}

std::string Socket::ReceiveBroadcast(int timeout_ms) {
    char buffer[4096];
    sockaddr_in from{};
    int from_len = sizeof(from);

    int received = recvfrom(broadcast_socket, buffer, sizeof(buffer) - 1, 0,
        (sockaddr*)&from, &from_len);

    if (received == SOCKET_ERROR) {
        int error = WSAGetLastError();
        if (error != WSAEWOULDBLOCK) {
            // Silently ignore
        }
        return "";
    }

    buffer[received] = '\0';
    std::string result(buffer);

    if (!result.empty() && result.back() == '\n') {
        result.pop_back();
    }

    return result;
}
BroadcastMessage Socket::ReceiveBroadcastWithIP(int timeout_ms) {
    BroadcastMessage result{ "", "" };

    char buffer[4096];
    sockaddr_in from{};
    int from_len = sizeof(from);

    int received = recvfrom(broadcast_socket, buffer, sizeof(buffer) - 1, 0,
        (sockaddr*)&from, &from_len);

    if (received == SOCKET_ERROR) {
        int error = WSAGetLastError();
        if (error != WSAEWOULDBLOCK) {
            // Silently ignore
        }
        return result;
    }

    buffer[received] = '\0';
    result.message = std::string(buffer);

    if (!result.message.empty() && result.message.back() == '\n') {
        result.message.pop_back();
    }

    // Get sender IP
    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &from.sin_addr, ip_str, sizeof(ip_str));
    result.sender_ip = std::string(ip_str);

    return result;
}

void Socket::JoinMulticast(const std::string& group) {
    ip_mreq mreq{};

    if (inet_pton(AF_INET, group.c_str(), &mreq.imr_multiaddr.s_addr) != 1) {
        std::cerr << "Invalid multicast group address" << std::endl;
        return;
    }

    std::string localIP = GetLocalIP();
    if (inet_pton(AF_INET, localIP.c_str(), &mreq.imr_interface.s_addr) != 1) {
        std::cerr << "Invalid local IP address" << std::endl;
        return;
    }

    if (setsockopt(multicast_socket, IPPROTO_IP, IP_ADD_MEMBERSHIP,
        (const char*)&mreq, sizeof(mreq)) == SOCKET_ERROR) {
        std::cerr << "Failed to join multicast group: " << WSAGetLastError() << std::endl;
        return;
    }

    int ttl = 32;
    if (setsockopt(multicast_socket, IPPROTO_IP, IP_MULTICAST_TTL,
        (const char*)&ttl, sizeof(ttl)) == SOCKET_ERROR) {
        std::cerr << "setsockopt(IP_MULTICAST_TTL) failed: " << WSAGetLastError() << std::endl;
    }

    in_addr localInterface{};
    inet_pton(AF_INET, localIP.c_str(), &localInterface);

    if (setsockopt(multicast_socket, IPPROTO_IP, IP_MULTICAST_IF,
        (const char*)&localInterface, sizeof(localInterface)) == SOCKET_ERROR) {
        std::cerr << "setsockopt(IP_MULTICAST_IF) failed: " << WSAGetLastError() << std::endl;
    }

    BOOL loop = TRUE;
    if (setsockopt(multicast_socket, IPPROTO_IP, IP_MULTICAST_LOOP,
        (const char*)&loop, sizeof(loop)) == SOCKET_ERROR) {
        std::cerr << "setsockopt(IP_MULTICAST_LOOP) failed: " << WSAGetLastError() << std::endl;
    }
}

void Socket::LeaveMulticast(const std::string& group) {
    std::string localIP = GetLocalIP();

    ip_mreq mreq{};
    mreq.imr_multiaddr.s_addr = inet_addr(group.c_str());
    mreq.imr_interface.s_addr = inet_addr(localIP.c_str());

    if (setsockopt(multicast_socket, IPPROTO_IP, IP_DROP_MEMBERSHIP,
        (const char*)&mreq, sizeof(mreq)) == SOCKET_ERROR) {
        std::cerr << "Failed to leave multicast group: " << WSAGetLastError() << std::endl;
    }
}

void Socket::SendMulticast(const std::string& msg, const std::string& group) {
    if (multicast_socket == INVALID_SOCKET) {
        std::cerr << "multicast_socket is INVALID!" << std::endl;
        return;
    }

    sockaddr_in groupAddr{};
    groupAddr.sin_family = AF_INET;
    groupAddr.sin_addr.s_addr = inet_addr(group.c_str());
    groupAddr.sin_port = htons(50000);

    int ttl = 32;
    if (setsockopt(multicast_socket, IPPROTO_IP, IP_MULTICAST_TTL,
        (const char*)&ttl, sizeof(ttl)) == SOCKET_ERROR) {
        std::cerr << "Failed to set TTL: " << WSAGetLastError() << std::endl;
    }

    std::string message = msg + "\n";
    int result = sendto(multicast_socket, message.c_str(),
        (int)message.length(), 0,
        (sockaddr*)&groupAddr, sizeof(groupAddr));

    if (result == SOCKET_ERROR) {
        std::cerr << "sendto(multicast) failed: " << WSAGetLastError() << std::endl;
    }
}

std::string Socket::ReceiveMulticast(int timeout_ms) {
    char buffer[4096];
    sockaddr_in from{};
    int from_len = sizeof(from);

    int received = recvfrom(multicast_socket, buffer, sizeof(buffer) - 1, 0,
        (sockaddr*)&from, &from_len);

    if (received == SOCKET_ERROR) {
        int error = WSAGetLastError();
        if (error != WSAEWOULDBLOCK) {
            // Silently ignore
        }
        return "";
    }

    buffer[received] = '\0';
    std::string result(buffer);

    if (!result.empty() && result.back() == '\n') {
        result.pop_back();
    }

    return result;
}

// ===== TCP METHODS =====

SOCKET Socket::CreateTCPServer(int port) {
    SOCKET server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server == INVALID_SOCKET) {
        std::cerr << "Failed to create TCP server socket: " << WSAGetLastError() << std::endl;
        return INVALID_SOCKET;
    }

    BOOL reuseAddr = TRUE;
    if (setsockopt(server, SOL_SOCKET, SO_REUSEADDR,
        (const char*)&reuseAddr, sizeof(reuseAddr)) == SOCKET_ERROR) {
        std::cerr << "setsockopt failed: " << WSAGetLastError() << std::endl;
    }

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serverAddr.sin_port = htons(port);

    if (bind(server, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cerr << "bind failed: " << WSAGetLastError() << std::endl;
        closesocket(server);
        return INVALID_SOCKET;
    }

    if (listen(server, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "listen failed: " << WSAGetLastError() << std::endl;
        closesocket(server);
        return INVALID_SOCKET;
    }

    std::cout << "TCP Server listening on port " << port << std::endl;
    return server;
}

SOCKET Socket::AcceptTCPConnection(SOCKET server_socket) {
    sockaddr_in clientAddr{};
    int clientAddrLen = sizeof(clientAddr);

    SOCKET client = accept(server_socket, (sockaddr*)&clientAddr, &clientAddrLen);
    if (client == INVALID_SOCKET) {
        int error = WSAGetLastError();
        if (error != WSAEWOULDBLOCK) {
            std::cerr << "accept failed: " << error << std::endl;
        }
        return INVALID_SOCKET;
    }

    std::cout << "Client connected: " << inet_ntoa(clientAddr.sin_addr) << std::endl;
    return client;
}

SOCKET Socket::ConnectToTCPServer(const std::string& ip, int port) {
    SOCKET client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (client == INVALID_SOCKET) {
        std::cerr << "Failed to create TCP client socket: " << WSAGetLastError() << std::endl;
        return INVALID_SOCKET;
    }

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = inet_addr(ip.c_str());
    serverAddr.sin_port = htons(port);

    if (connect(client, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cerr << "Failed to connect to server: " << WSAGetLastError() << std::endl;
        closesocket(client);
        return INVALID_SOCKET;
    }

    std::cout << "Connected to server at " << ip << ":" << port << std::endl;
    return client;
}

bool Socket::SendTCP(SOCKET sock, const std::string& msg) {
    std::string message = msg + "\n";
    int result = send(sock, message.c_str(), (int)message.length(), 0);

    if (result == SOCKET_ERROR) {
        std::cerr << "send failed: " << WSAGetLastError() << std::endl;
        return false;
    }

    return true;
}

std::string Socket::ReceiveTCP(SOCKET sock) {
    char buffer[4096];
    int result = recv(sock, buffer, sizeof(buffer) - 1, 0);

    if (result == SOCKET_ERROR) {
        std::cerr << "recv failed: " << WSAGetLastError() << std::endl;
        return "";
    }

    if (result == 0) {
        return "";
    }

    buffer[result] = '\0';
    std::string msg(buffer);

    if (!msg.empty() && msg.back() == '\n') {
        msg.pop_back();
    }

    return msg;
}

void Socket::CloseTCPSocket(SOCKET sock) {
    if (sock != INVALID_SOCKET) {
        closesocket(sock);
    }
}

std::string Socket::GetLocalIP() {
    WSADATA wsa{};
    int result = WSAStartup(MAKEWORD(2, 2), &wsa);
    if (result != 0) {
        return "192.168.41.47";
    }

    SOCKET s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s == INVALID_SOCKET) {
        return "192.168.41.47";
    }

    sockaddr_in remote{};
    remote.sin_family = AF_INET;
    remote.sin_port = htons(53);
    inet_pton(AF_INET, "8.8.8.8", &remote.sin_addr);

    if (connect(s, (sockaddr*)&remote, sizeof(remote)) == SOCKET_ERROR) {
        closesocket(s);
        return "192.168.41.47";
    }

    sockaddr_in local{};
    int local_len = sizeof(local);
    if (getsockname(s, (sockaddr*)&local, &local_len) == SOCKET_ERROR) {
        closesocket(s);
        return "192.168.41.47";
    }

    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &local.sin_addr, ip, sizeof(ip));

    closesocket(s);
    WSACleanup();

    if (strncmp(ip, "127.", 4) == 0) {
        return "192.168.41.47";
    }

    return std::string(ip);
}