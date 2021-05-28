#pragma once

#include <algorithm>
#include <istream>
#include <boost/enable_shared_from_this.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/bind/bind.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/asio/read.hpp>
#include "util/Exceptions.h"
#include "network/connection/ConnectionManager.h"
#include "network/connection/Connection.h"
#include "ITaskPublisher.h"

namespace services {
    class BoostAsioTaskPublisher : public ITaskPublisher {

    public:
        typedef std::shared_ptr<boost::asio::ip::tcp::socket> socket_ptr;

        BoostAsioTaskPublisher(std::unique_ptr<IProtocol> protocol) : ITaskPublisher(std::move(protocol)) {}

        SendResult Send(const std::shared_ptr<ITask>& task) {
            return ITaskPublisher::Send(task);
        }

        ITaskPublisher* Clone() const override {
            return new BoostAsioTaskPublisher(std::unique_ptr<IProtocol>(protocol->Clone()));
        }

        void StartService(ResponseCallback& onReceiveCallback) override {
            ITaskPublisher::StartService(onReceiveCallback);
            if (!serverThread.joinable()) {
                serverThread = std::thread(&BoostAsioTaskPublisher::StartServer, this);
            }
        }

        void StartService(ResponseCallback& onReceiveCallback, std::string ipAddress, unsigned short port) {
            SetRemoteEndPoint(ipAddress, port);
            ITaskPublisher::StartService(onReceiveCallback);
            if (!serverThread.joinable()) {
                serverThread = std::thread(&BoostAsioTaskPublisher::StartServer, this);
            }
        }

        void SetRemoteEndPoint(std::string ipAddress, unsigned short port) {
            remoteEndPoint = boost::asio::ip::tcp::endpoint(boost::asio::ip::address::from_string(ipAddress), port);
        }

        void SetListenPort(unsigned short port) {
            listenPort = port;
        }

        void StopServer() {
            ioService.stop();
            if (serverThread.joinable()) {
                serverThread.join();
            }
        }

        unsigned short GetListeningPort() {
            return listenPort;
        }

    protected:
        SendResult Send(const std::vector<byte>& buffer) override {
            auto sock = std::make_shared<boost::asio::ip::tcp::socket>(ioService);
            auto bufferCopy = std::make_shared<std::vector<byte>>(buffer);
            auto onConnect = [sock, bufferCopy](const boost::system::error_code& errorCode) {
                if (errorCode)
                    return;
                auto handler = [](
                        const boost::system::error_code& error, // Result of operation.
                        std::size_t bytes_transferred           // Number of bytes sent.
                ) {};
                sock->async_send(boost::asio::buffer(bufferCopy->data(), bufferCopy->size()), handler);
            };
            sock->async_connect(remoteEndPoint, onConnect);
        }

        bool CheckParams() const {
            return remoteEndPoint.port() > 0 && !remoteEndPoint.address().is_unspecified();
        }

        bool InitializeAcceptor() {
            listenPort = std::max(listenPort, static_cast<unsigned short >(1024));
            auto protocolIP4 = boost::asio::ip::tcp::v4();
            try {
                acceptor = std::make_unique<boost::asio::ip::tcp::acceptor>(ioService, boost::asio::ip::tcp::v4());
                acceptor->set_option(boost::asio::socket_base::reuse_address(true));
            } catch (...) {
                return false;
            }
            boost::system::error_code errorCode;
            while (listenPort < 65535) {
                acceptor->bind({boost::asio::ip::tcp::v4(), listenPort}, errorCode);
                if (!errorCode) {
                    try { acceptor->listen(); }
                    catch (...) { return false; }
                    return true;
                } else if (errorCode == boost::asio::error::address_in_use) {
                    listenPort++;
                    continue;
                } else {
                    break;
                }
            }
            return false;
        }

        bool StartServer() {
            if (!CheckParams() || !InitializeAcceptor()) {
                return false;
            }
            socket_ptr sock(new boost::asio::ip::tcp::socket(ioService));
            StartAccept(sock);
            ioService.run();
        }

        void StartAccept(const socket_ptr sock) {
            if (acceptor.get()) {
                auto handler = std::bind(&BoostAsioTaskPublisher::HandleConnection, this, sock, std::placeholders::_1);
                acceptor->async_accept(*sock, handler);
            }
        }

        void HandleConnection(socket_ptr sock, const boost::system::error_code& err) {
            socket_ptr newSock(new boost::asio::ip::tcp::socket(ioService));
            StartAccept(newSock);
            if (!err) {

                connectioManager.Start(std::make_shared<Connection>(std::move(sock), connectioManager),
                                       std::bind(&BoostAsioTaskPublisher::OnRecieve, this, std::placeholders::_1));

//                auto buf = std::make_shared<boost::asio::streambuf>();
//                auto handler = std::bind(&BoostAsioTaskPublisher::HandleReceivedMessage, this, buf, std::placeholders::_1, std::placeholders::_2);
//                auto handler = [buf, this](const boost::system::error_code& errorCode, size_t bytes) {
//                    if (!errorCode) {
//                        std::thread(&BoostAsioTaskPublisher::HandleReceivedMessage, this, buf, bytes).detach();
//                    }
//                };
//                boost::asio::async_read(*sock, *buf, boost::asio::transfer_at_least(sock->available()), handler);
            } else {
                auto msg = err.message();
                std::cout << msg;
            }
        }

        void HandleReceivedMessage(std::shared_ptr<boost::asio::streambuf> buf, const boost::system::error_code& errorCode, size_t bytes) {
            if (!errorCode) {
                ITaskResult result;
                // received data is "committed" from output sequence to input sequence
                buf->commit(bytes);
                std::vector<byte> receivedBytes(bytes);
                buffer_copy(boost::asio::buffer(receivedBytes), buf->data());
                if (IProtocol::DeserializeResult::DR_Success == protocol->Deserialize(result, receivedBytes)) {
                    callback(result);
                }
            }
        }

    private:
        unsigned short listenPort;
        boost::asio::io_service ioService;
        boost::asio::ip::tcp::endpoint remoteEndPoint;
        std::unique_ptr<boost::asio::ip::tcp::acceptor> acceptor;
        std::thread serverThread;
        ConnectionManager connectioManager;
    };
}

