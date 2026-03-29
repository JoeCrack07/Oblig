#include <iostream>
#include <thread>
#include <chrono>
#include <map>
#include <vector>
#include <mutex>
#include <fstream>
#include "Socket.h"
#include "Parsing.h"
#include "Room.h"

// Global variabler
std::shared_ptr<Socket> g_socket;
std::string g_username;
std::string g_ip;
bool g_running = true;

// Presence
std::map<std::string, std::string> g_active_users;
std::mutex g_users_mutex;

// Broadcast
//std::vector<std::pair<std::string, std::string>> g_broadcast_messages;
//std::mutex g_broadcast_mutex;
bool g_broadcast_active = false;

// Multicast
const std::string MULTICAST_GROUP = "239.0.0.1"; //RFC
std::map<std::string, std::vector<std::pair<std::string, std::string>>> g_rooms;
std::vector<std::pair<std::string, std::string>> g_discovered_rooms;
std::mutex g_rooms_mutex;
std::string g_current_multicast_room;
bool g_multicast_active = false;

// Oblig 1, Presence
void static BroadcastPresence() {
    while (g_running) {
        std::string message = "PRESENCE|-|" + g_username + "|" + g_ip;
        g_socket->SendBroadcast(message);
        std::this_thread::sleep_for(std::chrono::seconds(10));  // 10 sek for RFC
    }
}

void static ReceivePresence() {
    while (g_running) {
        std::string message = g_socket->ReceiveBroadcast(500);

        if (message.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        ParseMessage msg(message);

        if (!msg.isValid() || !msg.isPresence()) {
            continue;
        }

        std::string username = msg.getUsername();
        std::string ip = msg.getPayload();

        if (username == g_username) {
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(g_users_mutex);
            g_active_users[username] = ip;
        }
    }
}

void static DisplayActiveUsers() {
    std::lock_guard<std::mutex> lock(g_users_mutex);

    std::cout << "\nActive Users (" << g_active_users.size() << ")" << std::endl;

    if (g_active_users.empty()) {
        std::cout << "No other users online" << std::endl;
    }
    else {
        for (const auto& user : g_active_users) {
            std::cout << "  [" << user.first << "] " << user.second << std::endl;
        }
    }
    std::cout << "\n" << std::endl;
}

/*void static DisplayUsers() {
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::seconds(30));

        std::lock_guard<std::mutex> lock(g_users_mutex);

        std::cout << "\nActive Users (" << g_active_users.size() << ")" << std::endl;

        if (g_active_users.empty()) {
            std::cout << "No other users online" << std::endl;
        }
        else {
            for (const auto& user : g_active_users) {
                std::cout << "  [" << user.first << "] " << user.second << std::endl;
            }
        }
        std::cout << "\n" << std::endl;
    }
}*/

// Oblig 1 Broadcast UsnChat

void static ReceiveMessages() {
    while (g_broadcast_active) {
        std::string raw_message = g_socket->ReceiveBroadcast(500);

        if (raw_message.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        ParseMessage msg(raw_message);

        if (!msg.isValid() || !msg.isChat()) {
            continue;
        }

        std::string room = msg.getRoom();
        if (room != "USN Chat") continue;

        std::string sender = msg.getUsername();
        if (sender == g_username) continue;

        std::string content = msg.getPayload();

        /* {
            std::lock_guard<std::mutex> lock(g_broadcast_mutex);
            g_broadcast_messages.push_back({ sender, content });
        }*/

        std::cout << "\n[" << sender << "]: " << content << std::endl;
        std::cout << "> ";
        std::cout.flush();
    }
}

void static UserInput() {
    std::cout << "\nBroadcast Chat (USN Chat)" << std::endl;
    std::cout << "For å gå tilbake: back" << std::endl;
    std::cout << "> ";

    std::string input;
    while (std::getline(std::cin, input)) {
        if (input == "back") {
            g_broadcast_active = false;
            break;
        }

        /*if (input == "/messages") {
            std::lock_guard<std::mutex> lock(g_broadcast_mutex);
            std::cout << "\n=== Messages ===" << std::endl;
            for (const auto& msg : g_broadcast_messages) {
                std::cout << "[" << msg.first << "]: " << msg.second << std::endl;
            }
            std::cout << "\n> ";
            continue;
        }*/

        if (!input.empty()) {
            std::string chat_message = "CHAT|USN Chat|" + g_username + "|" + input;
            g_socket->SendBroadcast(chat_message);

            /* {
                std::lock_guard<std::mutex> lock(g_broadcast_mutex);
                g_broadcast_messages.push_back({ g_username, input });
            }*/
        }

        std::cout << "> ";
    }
}

// Oblig 1 Multicast

// Annonserer rommet via broadcast hver 10sek
void static AdvertiseRoom(const std::string& room_name) {
    int count = 0;
    while (g_multicast_active && count < 100) {
        std::string announce = "ROOM_ANNOUNCE|" + room_name + "|" + g_username + "|OPEN";
        g_socket->SendBroadcast(announce);  // broadcast til alle
        std::this_thread::sleep_for(std::chrono::seconds(10)); //RFC 10 sekunder
        count++;
    }
}

// Mottar rom annonseringer via broadcast fra andre
void static ReceiveRoomAnnouncements() {
    while (g_multicast_active) {
        std::string raw_message = g_socket->ReceiveBroadcast(500);

        if (raw_message.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        ParseMessage msg(raw_message);

        if (!msg.isValid() || !msg.isRoomAnnounce()) {
            continue;
        }

        std::string room_name = msg.getRoom();
        std::string owner = msg.getUsername();

        {
            std::lock_guard<std::mutex> lock(g_rooms_mutex);

            bool found = false;
            for (const auto& room : g_discovered_rooms) {
                if (room.first == room_name) {
                    found = true;
                    break;
                }
            }

            if (!found) {
                g_discovered_rooms.push_back({ room_name, owner });
                std::cout << "\n Room discovered " << room_name << " (owner: " << owner << ")" << std::endl;
                std::cout << "> ";
                std::cout.flush();
            }
        }
    }
}

// Motta meldinger i multicast gruppen
void static ReceiveMessagesMulti() {
    /*std::cout << "[DEBUG] Joining multicast group: " << MULTICAST_GROUP << std::endl;*/
    g_socket->JoinMulticast(MULTICAST_GROUP); // 239.0.0.1

    while (g_multicast_active) {
        std::string raw_message = g_socket->ReceiveMulticast(500);

        if (raw_message.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        /*std::cout << "[DEBUG] Raw message: " << raw_message << std::endl;*/

        ParseMessage msg(raw_message);

        // Ignorer PRESENCE-meldinger på multicast
        /*if (!msg.isValid() || !msg.isChat()) {
            std::cout << "[DEBUG] Not a CHAT message, skipping" << std::endl;
            continue;
        }*/

        std::string room = msg.getRoom();
        std::string sender = msg.getUsername();
        std::string content = msg.getPayload();

        /*std::cout << "[DEBUG] Room: '" << room << "', Current: '" << g_current_multicast_room << "'" << std::endl;
        std::cout << "[DEBUG] Sender: " << sender << ", Username: " << g_username << std::endl;*/

        {
            std::lock_guard<std::mutex> lock(g_rooms_mutex);
            g_rooms[room].push_back({ sender, content });
        }

        if (room == g_current_multicast_room && sender != g_username) {
            std::cout << "\n[" << sender << "]: " << content << std::endl;
            std::cout << "> ";
            std::cout.flush();
        }
    }

    g_socket->LeaveMulticast(MULTICAST_GROUP);
}

// Håndtere brukerinput multicast chat
void static HandleChat() {
    std::cout << "\nOpen Group Rooms" << std::endl;
    std::cout << "Commands: /create <name>, /join <name>, /list, /back" << std::endl;

    g_multicast_active = true;
    std::thread announce_thread(ReceiveRoomAnnouncements);
    std::thread receive_thread(ReceiveMessages);
    announce_thread.detach();
    receive_thread.detach();

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    std::string input;
    while (g_multicast_active) {
        if (!g_current_multicast_room.empty()) {
            std::cout << "[" << g_current_multicast_room << "]> ";
        }
        else {
            std::cout << "> ";
        }
        std::cout.flush();

        if (!std::getline(std::cin, input)) {
            break;
        }

        if (input == "/back") {
            g_multicast_active = false;
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            break;
        }

        if (input == "/list") {
            std::lock_guard<std::mutex> lock(g_rooms_mutex);
            std::cout << "\n=== Available Rooms ===" << std::endl;
            if (g_discovered_rooms.empty()) {
                std::cout << "No rooms discovered yet" << std::endl;
            }
            else {
                for (const auto& room : g_discovered_rooms) {
                    std::cout << "  [" << room.first << "] (owner: " << room.second << ")" << std::endl;
                }
            }
            std::cout << std::endl;
            continue;
        }

        if (input.substr(0, 8) == "/create ") {
            std::string room_name = input.substr(8);
            if (room_name.empty()) {
                std::cout << "Room name cannot be empty!" << std::endl;
                continue;
            }

            g_current_multicast_room = room_name;

            {
                std::lock_guard<std::mutex> lock(g_rooms_mutex);
                g_discovered_rooms.push_back({ room_name, g_username });
                g_rooms[room_name];
            }

            // Annonsere created rom via BROADCAST
            std::thread advertise(AdvertiseRoom, room_name);
            advertise.detach();

            std::cout << "Room '" << room_name << "' created! (broadcasting to others...)" << std::endl;
            continue;
        }

        if (input.substr(0, 6) == "/join ") {
            std::string room_name = input.substr(6);
            if (room_name.empty()) {
                std::cout << "Room name cannot be empty!" << std::endl;
                continue;
            }
            g_current_multicast_room = room_name;
            std::cout << "Joined room: " << room_name << std::endl;
            continue;
        }

        // Send melding via MULTICAST hvis vi er i et rom
        if (!input.empty() && !g_current_multicast_room.empty()) {
            std::string chat_message = "CHAT|" + g_current_multicast_room + "|" + g_username + "|" + input;
            /*std::cout << "[DEBUG] Sending: " << chat_message << std::endl; */ // Debug
            g_socket->SendMulticast(chat_message, MULTICAST_GROUP);

            {
                std::lock_guard<std::mutex> lock(g_rooms_mutex);
                g_rooms[g_current_multicast_room].push_back({ g_username, input });
            }
        }
        else if (!input.empty()) {
            std::cout << "Join or create a room first with /join or /create!" << std::endl;
        }
    }
}

// Tcp Garantied Room

std::map<std::string, std::pair<std::string, int>> g_tcp_discovered_rooms;
std::mutex g_tcp_rooms_mutex;
bool g_krav4_active = false;

void static AnnounceRoom(const std::string& room_name, int port) {
    int count = 0;
    while (g_krav4_active && count < 100) {
        // RFC Format: INVITE|room-name|owner|TCP
        std::string announce = "INVITE|" + room_name + "|" + g_username + "|TCP";
        /*std::cout << "[DEBUG] Announcing: " << announce << std::endl;*/
        g_socket->SendBroadcast(announce);
        std::this_thread::sleep_for(std::chrono::seconds(10));
        count++;
    }
}

void static ReceiveRoomAnnouncementsTPC() {
    while (g_krav4_active) {
        BroadcastMessage bcast = g_socket->ReceiveBroadcastWithIP(500);

        if (bcast.message.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        ParseMessage msg(bcast.message);

        if (!msg.isValid() || !msg.isInvite()) {
            continue;
        }

        if (msg.getPayload() != "TCP") {
            continue;
        }

        std::string room_name = msg.getRoom();
        std::string owner = msg.getUsername();
        std::string owner_ip = bcast.sender_ip;  // Fra broadcast-pakken

        /*std::cout << "[DEBUG] Raw message: " << bcast.message << std::endl;
        std::cout << "[DEBUG] Room: " << room_name << ", Owner: " << owner << std::endl;
        std::cout << "[DEBUG] Owner IP (from packet): " << owner_ip << ":50001" << std::endl; */

        {
            std::lock_guard<std::mutex> lock(g_tcp_rooms_mutex);

            if (g_tcp_discovered_rooms.find(room_name) == g_tcp_discovered_rooms.end()) {
                g_tcp_discovered_rooms[room_name] = { owner_ip, 50001 };
                std::cout << "\n[TCP ROOM DISCOVERED] " << room_name << " (owner: " << owner << ", " << owner_ip << ":50001)" << std::endl;
                std::cout << "> ";
                std::cout.flush();
            }
        }
    }
}

void static CreateRoom() {
    std::string room_name;
    std::cout << "Enter room name: ";
    std::getline(std::cin, room_name);

    if (room_name.empty() || room_name.find('|') != std::string::npos) {
        std::cout << "Room name cannot be empty or contain |" << std::endl;
        return;
    }

    Room room(room_name, g_username, g_ip, 50001);
    room.StartServer();

    std::thread announce_thread(AnnounceRoom, room_name, 50001);
    announce_thread.detach();

    std::cout << "Room '" << room_name << "' created!" << std::endl;
    std::cout << "Type messages (or /back to exit):" << std::endl;

    std::string input;
    while (g_krav4_active) {
        std::cout << "[" << room_name << "]> ";
        std::cout.flush();

        if (!std::getline(std::cin, input)) {
            break;
        }

        if (input == "/back") {
            break;
        }

        if (!input.empty()) {
            std::string chat_msg = "CHAT|" + room_name + "|" + g_username + "|" + input;
            room.BroadcastMessage(chat_msg);
        }
    }

    room.Stop();
}

void static JoinRoom() {
    std::cout << "\nAvailable TCP Guaranteed Rooms" << std::endl;

    {
        std::lock_guard<std::mutex> lock(g_tcp_rooms_mutex);
        if (g_tcp_discovered_rooms.empty()) {
            std::cout << "No rooms discovered yet" << std::endl;
            return;
        }

        int i = 1;
        for (const auto& room : g_tcp_discovered_rooms) {
            std::cout << i << ". " << room.first << " (owner: " << room.second.first << ":" << room.second.second << ")" << std::endl;
            i++;
        }
    }

    std::cout << "\nEnter room name to join: ";
    std::string room_name;
    std::getline(std::cin, room_name);

    std::pair<std::string, int> room_info;
    {
        std::lock_guard<std::mutex> lock(g_tcp_rooms_mutex);
        if (g_tcp_discovered_rooms.find(room_name) == g_tcp_discovered_rooms.end()) {
            std::cout << "Room not found!" << std::endl;
            return;
        }
        room_info = g_tcp_discovered_rooms[room_name];
    }

    std::cout << "[DEBUG] Connecting to: " << room_info.first << ":" << room_info.second << std::endl;

    Socket sock;
    SOCKET client = sock.ConnectToTCPServer(room_info.first, room_info.second);

    if (client == INVALID_SOCKET) {
        std::cout << "[DEBUG] Failed to connect to server " << room_info.first << ":" << room_info.second << std::endl;
        return;
    }

    std::cout << "Joined room '" << room_name << "'!" << std::endl;
    std::cout << "Type messages (or /back to exit):" << std::endl;

    std::thread receive_thread([client, room_name]() {
        Socket sock;
        while (g_krav4_active) {
            std::string msg = sock.ReceiveTCP(client);
            if (msg.empty()) break;
            std::cout << "\n" << msg << std::endl;
            std::cout << "[" << room_name << "]> ";
            std::cout.flush();
        }
        sock.CloseTCPSocket(client);
        });
    receive_thread.detach();

    std::string input;
    while (g_krav4_active) {
        std::cout << "[" << room_name << "]> ";
        std::cout.flush();

        if (!std::getline(std::cin, input)) {
            break;
        }

        if (input == "/back") {
            break;
        }

        if (!input.empty()) {
            std::string chat_msg = "CHAT|" + room_name + "|" + g_username + "|" + input;
            sock.SendTCP(client, chat_msg);
        }
    }

    sock.CloseTCPSocket(client);
}
void static HandleChatTCP() {
    std::cout << "\nTCP Guaranteed Rooms" << std::endl;
    std::cout << "Commands: /create <name>, /join <name>, /list, /back" << std::endl;

    g_krav4_active = true;
    std::thread announce_thread(ReceiveRoomAnnouncements);
    announce_thread.detach();

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::string input;
    while (g_krav4_active) {
        std::cout << "> ";
        std::cout.flush();

        if (!std::getline(std::cin, input)) {
            break;
        }

        if (input == "/back") {
            g_krav4_active = false;
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            break;
        }

        if (input == "/list") {
            std::lock_guard<std::mutex> lock(g_tcp_rooms_mutex);
            std::cout << "\n=== Available TCP Rooms ===" << std::endl;
            if (g_tcp_discovered_rooms.empty()) {
                std::cout << "No rooms discovered yet" << std::endl;
            }
            else {
                int i = 1;
                for (const auto& room : g_tcp_discovered_rooms) {
                    std::cout << i << ". " << room.first << " (owner: " << room.second.first << ")" << std::endl;
                    i++;
                }
            }
            std::cout << std::endl;
            continue;
        }

        if (input.substr(0, 8) == "/create ") {
            CreateRoom();
            continue;
        }

        if (input.substr(0, 6) == "/join ") {
            JoinRoom();
            continue;
        }
    }
}
// Meny
void static DisplayMainMenu() {
    std::cout << "\nMeny" << std::endl;
    std::cout << "User: " << g_username << " | IP: " << g_ip << std::endl;
    std::cout << "=====================================" << std::endl;
    std::cout << "1. View Active Users" << std::endl;
    std::cout << "2. Broadcast Chat" << std::endl;
    std::cout << "3. Multicast Rooms" << std::endl;
    std::cout << "4. TCP Open Rooms" << std::endl;
    std::cout << "0. Exit" << std::endl;
    std::cout << "=====================================" << std::endl;
    std::cout << "> ";
}


int main() {
    std::cout << "NettverksOblig" << std::endl;

    // Initialize socket
    g_socket = std::make_shared<Socket>();
    std::cout << "Socket initialized" << std::endl;

    // Get username
    std::cout << "Enter your username: ";
    std::getline(std::cin, g_username);
    if (g_username.empty()) {
        g_username = "User_" + std::to_string(rand() % 10000);
    }

    // Get IP
    g_ip = Socket::GetLocalIP();
    std::cout << "Your IP: " << g_ip << std::endl;

    // Presence kjører alltid
    std::cout << "Starting Presence system..." << std::endl;
    std::thread t1(BroadcastPresence);
    std::thread t2(ReceivePresence);
    //std::thread t3(Krav1_DisplayUsers);
    t1.detach();
    t2.detach();
    //t3.detach();

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Main menu loop
    std::string choice;
    while (g_running) {
        DisplayMainMenu();
        std::getline(std::cin, choice);

        if (choice == "0") {
            g_running = false;
            break;
        }
        else if (choice == "1") {
            DisplayActiveUsers();
            /*std::cout << "\n(KRAV 1 is running in background. Press Enter to go back)" << std::endl;
            std::cin.get();*/
        }
        else if (choice == "2") {
            g_broadcast_active = true;
            std::thread t(ReceiveMessages);
            t.detach();
            UserInput();
        }
        else if (choice == "3") {
            g_multicast_active = true;
            HandleChat();
        }
        else if (choice == "4") {
            g_krav4_active = true;
            HandleChatTCP();
        }

    }


    std::cout << "Shutting down..." << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    return 0;
}