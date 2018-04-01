#include "network/connection/Connection.h"
#include "network/connection/ConnectionManager.h"

void services::Connection::DoRead() {
    auto self(shared_from_this());
    auto handler = [this, self](boost::system::error_code errorCode, std::size_t bytes_transferred) {
        if (!errorCode) {
            message.insert(message.end(), buffer.data(), buffer.data() + bytes_transferred);
            DoRead();
        } else if (errorCode != boost::asio::error::operation_aborted) {
            connectionManager.Handle(shared_from_this(), message);
            connectionManager.Stop(shared_from_this());
        } else {
            auto what = errorCode.message();
            std::cout << what;
        }
    };
    socket->async_read_some(boost::asio::buffer(buffer), handler);
}
