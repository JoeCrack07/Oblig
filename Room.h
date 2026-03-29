#pragma once

#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include "Socket.h"

class Room {
private:
    std::string room_name;
    std::string owner;
    std::string owner_ip;
    int tcp_port;
    SOCKET server_socket;
    std::vector<SOCKET> client_sockets;
    std::mutex clients_mutex;
    bool is_running;

public:
    Room(const std::string& name, const std::string& owner_name, const std::string& owner_ip, int port);
    ~Room();

    void StartServer();
    void AcceptClients();
    void BroadcastMessage(const std::string& msg);

    std::string GetRoomName() const { return room_name; }
    std::string GetOwner() const { return owner; }
    std::string GetOwnerIP() const { return owner_ip; }
    int GetTCPPort() const { return tcp_port; }

    void Stop();
};