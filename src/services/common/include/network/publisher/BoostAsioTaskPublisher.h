#pragma once

#include <algorithm>
#include <istream>
#include <boost/enable_shared_from_this.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/bind.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/streambuf.hpp>
#include <util/Exceptions.h>
#include "ITaskPublisher.h"

namespace services {
    class BoostAsioTaskPublisher
            : public ITaskPublisher, public boost::enable_shared_from_this<BoostAsioTaskPublisher> {

    public:
        typedef std::shared_ptr<boost::asio::ip::tcp::socket> socket_ptr;

        BoostAsioTaskPublisher(std::unique_ptr<IProtocol> protocol) : ITaskPublisher(std::move(protocol)) {}

        ITaskPublisher* Clone() const override {
            return new BoostAsioTaskPublisher(std::unique_ptr<IProtocol>(protocol->Clone()));
        }

        void StartService(ResponseCallback& onReceiveCallback) override {
            ITaskPublisher::StartService(onReceiveCallback);
            StartServer();
        }

        void SetRemoteEndPoint(std::string ipAddress, unsigned short port) {
            remoteEndPoint = boost::asio::ip::tcp::endpoint(boost::asio::ip::address::from_string(ipAddress), port);
        }

        void SetListenPort(unsigned short port) {
            listenPort = port;
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

        void StopServer() {
            ioService.stop();
            throw NotImplementedException();
        }

        bool CheckParams() const {
            throw NotImplementedException();
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
            if (!err) {
                // at this point, you can read/write to the socket
                auto buf = std::make_shared<boost::asio::streambuf>();
                // reserve 1024 bytes in output sequence
                boost::asio::streambuf::mutable_buffers_type preparedBuf = buf->prepare(1024);
                auto handler = [buf, this](const boost::system::error_code& e, size_t bytes) {
                    ITaskResult result;
                    std::vector<byte> receivedBytes(buf->size());
                    buffer_copy(boost::asio::buffer(receivedBytes), buf->data());
                    if (IProtocol::DeserializeResult::DR_Success == protocol->Deserialize(result, receivedBytes)) {
                        callback(result);
                    }
                };
                sock->async_receive(preparedBuf, handler);
            }
            socket_ptr newSock(new boost::asio::ip::tcp::socket(ioService));
            StartAccept(newSock);
        }


    private:
        unsigned short listenPort;
        boost::asio::io_service ioService;
        boost::asio::ip::tcp::endpoint remoteEndPoint;
        std::unique_ptr<boost::asio::ip::tcp::acceptor> acceptor;
    };
}

