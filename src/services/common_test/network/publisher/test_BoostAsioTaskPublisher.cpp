#include <boost/test/unit_test.hpp>
#include <iostream>
#include <task/TestTaskWithAdditionalField.h>
#include "task/TestTask.h"
#include "network/protocol/JSONProtocol.h"
#include "network/publisher/BoostAsioTaskPublisher.h"

BOOST_AUTO_TEST_SUITE(test_BoostAsioestTaskPublisher)

    void OnResultRecieve(services::ITaskResult& toOut, services::ITaskResult res) {
        toOut = res;
    }

    void SimpleListenServer(unsigned short port, std::string& receivedData) {
        boost::asio::io_service service;
        boost::asio::ip::tcp::endpoint ep(boost::asio::ip::tcp::v4(), port); // listen on 2001
        boost::asio::ip::tcp::acceptor acc(service, ep);
        auto sock = std::make_shared<boost::asio::ip::tcp::socket>(service);
        acc.accept(*sock);
        char data[512];
        size_t len = sock->read_some(boost::asio::buffer(data));
        if (len > 0) {
            data[len] = '\0';
            receivedData = data;
        }
    }

    void SimpleClient(std::string sendData, unsigned short port) {
        boost::system::error_code ec;
        boost::asio::io_service service;
        boost::asio::ip::tcp::endpoint ep(boost::asio::ip::address::from_string("127.0.0.1"), port);
        boost::asio::ip::tcp::socket sock(service);
        sock.connect(ep, ec);
        sock.write_some(boost::asio::buffer(sendData), ec);
        sock.close(ec);
    }

    BOOST_AUTO_TEST_CASE(test_send) {
        auto timeout = std::chrono::milliseconds(500);
        std::string receivedData;
        unsigned short port = 60000;
        std::thread testServer(SimpleListenServer, port, std::ref(receivedData));

        services::ITaskResult responseResult;
        services::ResponseCallback callback = std::bind(OnResultRecieve, std::ref(responseResult),
                                                        std::placeholders::_1);
        services::BoostAsioTaskPublisher publisher(std::make_unique<services::JSONProtocol>());
        publisher.StartService(callback, "127.0.0.1", port);
        std::string testValue = "TestValue 0123_#!";
        services::JSONProtocol jsonProtocol;
        auto task = std::make_shared<services::TestTaskWithAdditionalField>();
        task->SetAdditionalField(testValue);
        auto sendResult = publisher.Send(task);
        testServer.join();
        BOOST_CHECK_EQUAL(sendResult, services::SendResult::SendResult_Success);

        std::vector<services::byte> serialized;
        jsonProtocol.Serialize(serialized, task);
        std::string expectedResult(serialized.begin(), serialized.end());
        BOOST_CHECK_EQUAL(receivedData, expectedResult);
        publisher.StopServer();
        std::this_thread::sleep_for(timeout);
    }

    BOOST_AUTO_TEST_CASE(test_receive) {
        auto timeout = std::chrono::milliseconds(500);
        std::string responseStr = R"({"id":"d4e39cdd-5b50-4305-8bce-bd8a762f1711","status":"1","result":"42 %"})";

        services::ITaskResult responseResult;
        services::ResponseCallback callback = std::bind(OnResultRecieve, std::ref(responseResult),
                                                        std::placeholders::_1);
        services::BoostAsioTaskPublisher publisher(std::make_unique<services::JSONProtocol>());
        publisher.StartService(callback, "127.0.0.1", 12345);

        std::this_thread::sleep_for(timeout);
        SimpleClient(responseStr, publisher.GetListeningPort());
        std::this_thread::sleep_for(timeout);

        BOOST_CHECK_EQUAL("d4e39cdd-5b50-4305-8bce-bd8a762f1711",  boost::uuids::to_string(responseResult.GetId()));
        BOOST_CHECK_EQUAL(1, responseResult.GetStatus());
        BOOST_CHECK_EQUAL("42 %", responseResult.GetResult());

        publisher.StopServer();
        std::this_thread::sleep_for(timeout);
    }

BOOST_AUTO_TEST_SUITE_END()
