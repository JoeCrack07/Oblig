#include "Room.h"
#include <iostream>
#include <thread>
#include <algorithm>

Room::Room(const std::string& name, const std::string& owner_name, const std::string& owner_ip, int port)
    : room_name(name), owner(owner_name), owner_ip(owner_ip), tcp_port(port),
    server_socket(INVALID_SOCKET), is_running(false) {
}

Room::~Room() {
    Stop();
}

void Room::StartServer() {
    Socket sock;
    server_socket = sock.CreateTCPServer(tcp_port);

    if (server_socket == INVALID_SOCKET) {
        std::cerr << "Failed to start server for room: " << room_name << std::endl;
        return;
    }

    is_running = true;

    std::thread accept_thread([this]() { AcceptClients(); });
    accept_thread.detach();
}

void Room::AcceptClients() {
    Socket sock;

    while (is_running) {
        SOCKET client = sock.AcceptTCPConnection(server_socket);

        if (client == INVALID_SOCKET) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(clients_mutex);
            client_sockets.push_back(client);
        }

        std::thread client_thread([this, client]() {
            Socket sock;
            while (is_running) {
                std::string msg = sock.ReceiveTCP(client);

                if (msg.empty()) {
                    break;
                }

                // Parse og vis melding for room-owner
                std::cout << "\n" << msg << std::endl;
                std::cout << "[" << room_name << "]> ";
                std::cout.flush();

                // Broadcast til andre klienter
                BroadcastMessage(msg);
            }

            sock.CloseTCPSocket(client);

            {
                std::lock_guard<std::mutex> lock(clients_mutex);
                auto it = std::find(client_sockets.begin(), client_sockets.end(), client);
                if (it != client_sockets.end()) {
                    client_sockets.erase(it);
                }
            }
            });
        client_thread.detach();
    }
}

void Room::BroadcastMessage(const std::string& msg) {
    std::lock_guard<std::mutex> lock(clients_mutex);

    Socket sock;
    for (SOCKET client : client_sockets) {
        sock.SendTCP(client, msg);  // Send til ALLE klienter, ikke bare andre
    }
}

void Room::Stop() {
    is_running = false;

    if (server_socket != INVALID_SOCKET) {
        closesocket(server_socket);
        server_socket = INVALID_SOCKET;
    }

    {
        std::lock_guard<std::mutex> lock(clients_mutex);
        for (SOCKET client : client_sockets) {
            closesocket(client);
        }
        client_sockets.clear();
    }
}