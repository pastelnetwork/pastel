#pragma once

#include <boost/asio.hpp>
#include <iostream>
#include <memory>
#include <utility>
#include <vector>
#include <util/Types.h>


namespace services {
    class ConnectionManager;

    class Connection : public std::enable_shared_from_this<Connection> {
    public:

        Connection(std::shared_ptr<boost::asio::ip::tcp::socket> sock, ConnectionManager& manager)
                : socket(std::move(sock)),
                  connectionManager(manager) {
        }

        void Start() {
            DoRead();
        }

        void Stop() {
            if (socket) {
                socket->close();
            }
        }

        void DoRead();

    private:
        /// Socket for the connection.
        std::shared_ptr<boost::asio::ip::tcp::socket> socket;

        /// The manager for this connection.
        ConnectionManager& connectionManager;

        /// Buffer for incoming data.
        std::array<services::byte, 1024> buffer;

        std::vector<services::byte> message;
    };
}

