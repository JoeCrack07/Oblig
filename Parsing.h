#pragma once
#include <string>
#include <vector>
#include <sstream>

class ParseMessage {
public:
    enum class Type {
        PRESENCE,
        ROOM_ANNOUNCE,
        INVITE,
        CHAT,
        UNKNOWN
    };

private:
    std::string rawMessage_;
    bool valid_ = false;
    Type type_ = Type::UNKNOWN;

    std::string typeString_;
    std::string room_;
    std::string username_;
    std::string payload_;

public:
    ParseMessage(const std::string& message) : rawMessage_(message) {
        parse();
    }

    bool isValid() const {
        return valid_;
    }

    Type getType() const {
        return type_;
    }

    const std::string& getTypeString() const {
        return typeString_;
    }

    const std::string& getRoom() const {
        return room_;
    }

    const std::string& getUsername() const {
        return username_;
    }

    const std::string& getPayload() const {
        return payload_;
    }

    bool isPresence() const {
        return type_ == Type::PRESENCE;
    }

    bool isRoomAnnounce() const {
        return type_ == Type::ROOM_ANNOUNCE;
    }

    bool isInvite() const {
        return type_ == Type::INVITE;
    }

    bool isChat() const {
        return type_ == Type::CHAT;
    }

private:
    void parse() {
        std::string msg = rawMessage_;

        if (!msg.empty() && msg.back() == '\n') {
            msg.pop_back();
        }

        std::vector<std::string> parts = split(msg, '|');

        if (parts.size() != 4) {
            valid_ = false;
            return;
        }

        typeString_ = parts[0];
        room_ = parts[1];
        username_ = parts[2];
        payload_ = parts[3];

        if (typeString_.empty() || username_.empty()) {
            valid_ = false;
            return;
        }

        type_ = parseType(typeString_);

        if (type_ == Type::UNKNOWN) {
            valid_ = false;
            return;
        }

        valid_ = true;
    }

    static std::vector<std::string> split(const std::string& text, char delimiter) {
        std::vector<std::string> parts;
        std::stringstream ss(text);
        std::string item;

        while (std::getline(ss, item, delimiter)) {
            parts.push_back(item);
        }

        return parts;
    }

    static Type parseType(const std::string& typeStr) {
        if (typeStr == "PRESENCE") return Type::PRESENCE;
        if (typeStr == "ROOM_ANNOUNCE") return Type::ROOM_ANNOUNCE;
        if (typeStr == "INVITE") return Type::INVITE;
        if (typeStr == "CHAT") return Type::CHAT;
        return Type::UNKNOWN;
    }
};