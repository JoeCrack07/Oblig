#pragma once
#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>
#pragma comment(lib, "ws2_32.lib")

struct BroadcastMessage {
    std::string message;
    std::string sender_ip;
};

class Socket {
public:
    Socket();
    ~Socket();

    // Broadcast port 50000
    void SendBroadcast(const std::string& msg);
    std::string ReceiveBroadcast(int timeout_ms = 100);

    // Multicast port 50000,239.0.0.1
    void JoinMulticast(const std::string& group);
    void LeaveMulticast(const std::string& group);
    void SendMulticast(const std::string& msg, const std::string& group);
    std::string ReceiveMulticast(int timeout_ms = 100);

    // TCP port 50001
    SOCKET CreateTCPServer(int port);
    SOCKET AcceptTCPConnection(SOCKET server_socket);
    SOCKET ConnectToTCPServer(const std::string& ip, int port);
    bool SendTCP(SOCKET sock, const std::string& msg);
    std::string ReceiveTCP(SOCKET sock);
    void CloseTCPSocket(SOCKET sock);

    static std::string GetLocalIP();

    BroadcastMessage ReceiveBroadcastWithIP(int timeout_ms = 100);

private:
    SOCKET broadcast_socket;
    SOCKET multicast_socket;

    void InitWinsock();
};