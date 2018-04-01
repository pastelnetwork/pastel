#pragma once

#include <unordered_map>
#include "Connection.h"

namespace services {
    class ConnectionManager {
        typedef std::shared_ptr<Connection> ConnectionPtr;
        typedef std::function<void(std::vector<services::byte>)> MessageHandler;
    public:
        void Start(ConnectionPtr connectionPtr, MessageHandler handler) {
            connections.emplace(connectionPtr, handler);
            connectionPtr->Start();
        }

        void Stop(ConnectionPtr connectionPtr) {
            connections.erase(connectionPtr);
            connectionPtr->Stop();
        }

        void StopAll() {
            for (auto conn: connections)
                conn.first->Stop();
            connections.clear();
        }

        void Handle(ConnectionPtr connectionPtr, std::vector<services::byte>& message) {
            auto found = connections.find(connectionPtr);
            if (found != connections.end()) {
                found->second(message);
            }
        }

    private:
        /// The managed connections.
        std::unordered_map<ConnectionPtr, MessageHandler> connections;
    };
}

