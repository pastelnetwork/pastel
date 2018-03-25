#include <boost/test/unit_test.hpp>
#include <iostream>
#include <task/TestTaskWithAdditionalField.h>
#include "task/TestTask.h"
#include "network/protocol/JSONProtocol.h"
#include "network/publisher/BoostAsioTaskPublisher.h"

BOOST_AUTO_TEST_SUITE(test_BoostAsioestTaskPublisher)

    void OnResultRecieve(std::string* id, services::ITaskResult res) {
        *id = boost::uuids::to_string(res.GetId());
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

    BOOST_AUTO_TEST_CASE(test_send) {
        auto timeout = std::chrono::milliseconds(500);
        std::string receivedData;
        unsigned short port = 60000;
        std::thread testServer(SimpleListenServer, port, std::ref(receivedData));
//        std::this_thread::sleep_for(timeout);

        std::string id;
        services::ResponseCallback callback = std::bind(OnResultRecieve, &id, std::placeholders::_1);
        services::BoostAsioTaskPublisher publisher(std::make_unique<services::JSONProtocol>());
        publisher.StartService(callback, "127.0.0.1", port);
        std::string testValue = "TestValue 0123_#!";
        services::JSONProtocol jsonProtocol;
        auto task = std::make_shared<services::TestTaskWithAdditionalField>();
        task->SetAdditionalField(testValue);
        auto sendResult = publisher.Send(task);
        testServer.join();
        BOOST_CHECK_EQUAL(sendResult, services::SendResult::SendResult_Success);

        std::vector<services::byte>serialized;
        jsonProtocol.Serialize(serialized, task);
        std::string expectedResult(serialized.begin(), serialized.end());
        BOOST_CHECK_EQUAL(receivedData,  expectedResult);
        publisher.StopServer();
        std::this_thread::sleep_for(timeout);
    }

BOOST_AUTO_TEST_SUITE_END()
