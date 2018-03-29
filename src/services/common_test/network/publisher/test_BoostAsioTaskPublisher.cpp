#include <boost/test/unit_test.hpp>
#include <iostream>
#include <sstream>
#include <task/TestTaskWithAdditionalField.h>
#include <unordered_set>
#include "task/TestTask.h"
#include "network/protocol/JSONProtocol.h"
#include "network/publisher/BoostAsioTaskPublisher.h"

BOOST_AUTO_TEST_SUITE(test_BoostAsioestTaskPublisher)

    void OnResultRecieve(services::ITaskResult& toOut, services::ITaskResult res) {
        toOut = res;
    }

    void SimpleListenServer(unsigned short port, std::string& receivedData) {
        boost::asio::io_service service;
        boost::asio::ip::tcp::endpoint ep(boost::asio::ip::tcp::v4(), port);
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

    void SimpleMultipleListenServer(unsigned short port, size_t connectionsCount, std::vector<std::string>& allReceived) {
        typedef std::shared_ptr<boost::asio::ip::tcp::socket> socket_ptr;
        boost::asio::io_service service;
        boost::asio::ip::tcp::endpoint ep(boost::asio::ip::tcp::v4(), port);
        boost::asio::ip::tcp::acceptor acc(service, ep);
        std::vector<std::thread> threads;
        std::vector<std::string> strings(connectionsCount);
        auto ConnectionHandler = [](socket_ptr sock, std::string& receivedData) {
            char data[512];
            size_t len = sock->read_some(boost::asio::buffer(data));
            if (len > 0) {
                data[len] = '\0';
                receivedData = data;
            }
        };
        socket_ptr sock;
        for (size_t i = 0; i < connectionsCount; ++i) {
            socket_ptr sock(new boost::asio::ip::tcp::socket(service));
            acc.accept(*sock);
            threads.push_back(std::thread(ConnectionHandler, sock, std::ref(strings[i])));
        }
        for (auto& thread : threads) {
            if (thread.joinable())
                thread.join();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        allReceived = strings;
    }

    BOOST_AUTO_TEST_CASE(test_multiple_send) {
        auto timeout = std::chrono::milliseconds(500);
        std::string receivedData;
        unsigned short port = 60000;
        const size_t SENDERS_NUMBER = 10;
        const size_t SENDS_PER_THREAD = 10;
        std::vector<std::thread> threads;
        std::vector<std::string> received;
        std::thread testServer(SimpleMultipleListenServer, port, SENDERS_NUMBER *SENDS_PER_THREAD,  std::ref(received));

        services::ITaskResult responseResult;
        services::ResponseCallback callback = std::bind(OnResultRecieve, std::ref(responseResult),
                                                        std::placeholders::_1);
        services::BoostAsioTaskPublisher publisher(std::make_unique<services::JSONProtocol>());
        publisher.StartService(callback, "127.0.0.1", port);


        auto senderRoutine = [SENDS_PER_THREAD](services::BoostAsioTaskPublisher& server, size_t threadIndex){
            for (size_t i = 0; i < SENDS_PER_THREAD; ++i){
                std::stringstream ss;
                ss << i + threadIndex;
                auto task = std::make_shared<services::TestTaskWithAdditionalField>();
                task->SetAdditionalField(ss.str());
                auto sendResult = server.Send(task);
                BOOST_CHECK_EQUAL(sendResult, services::SendResult::SendResult_Success);
            }
        };
        for (int i = 0; i < SENDERS_NUMBER; ++i) {
            threads.emplace_back(senderRoutine, std::ref(publisher), i);
        }
        for (auto& thread : threads) thread.join();
        std::this_thread::sleep_for(timeout);
        testServer.join();

        auto getTestField = [](std::string s){
            return s.substr(s.find("\"test_field"));
        };
        std::unordered_set<std::string> expectedResults;
        services::JSONProtocol jsonProtocol;
        for (size_t i = 0; i < SENDS_PER_THREAD * SENDERS_NUMBER; ++i){
            std::stringstream ss;
            ss << i;
            auto task = std::make_shared<services::TestTaskWithAdditionalField>();
            task->SetAdditionalField(ss.str());
            std::vector<services::byte> serialized;
            jsonProtocol.Serialize(serialized, task);
            expectedResults.emplace(getTestField(std::string (serialized.begin(), serialized.end())));
        }

        BOOST_CHECK_EQUAL(received.size(), SENDS_PER_THREAD * SENDERS_NUMBER);
        for (const auto& str : received){
            if(!expectedResults.count(getTestField(str))){
                BOOST_CHECK(expectedResults.count(getTestField(str)));
            }
        }
        publisher.StopServer();
        std::this_thread::sleep_for(timeout);
    }


    void SimpleClient(std::string sendData, unsigned short port) {
        boost::asio::io_service service;
        boost::asio::ip::tcp::endpoint ep(boost::asio::ip::address::from_string("127.0.0.1"), port);
        boost::asio::ip::tcp::socket sock(service);
        try {
            sock.connect(ep);
            sock.write_some(boost::asio::buffer(sendData));
            sock.close();
            // BOOST_TEST_MESSAGE(sendData);
        } catch (boost::system::system_error e) {
            BOOST_TEST_MESSAGE(e.what());
        }
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

        BOOST_CHECK_EQUAL("d4e39cdd-5b50-4305-8bce-bd8a762f1711", boost::uuids::to_string(responseResult.GetId()));
        BOOST_CHECK_EQUAL(1, responseResult.GetStatus());
        BOOST_CHECK_EQUAL("42 %", responseResult.GetResult());

        publisher.StopServer();
        std::this_thread::sleep_for(timeout);
    }

    BOOST_AUTO_TEST_CASE(test_multiple_senders) {
        auto timeout = std::chrono::milliseconds(500);
        const size_t SENDERS_NUMBER = 10;
        const size_t PACKAGES_PER_THREAD = 20;
        unsigned char* met = new unsigned char[SENDERS_NUMBER * PACKAGES_PER_THREAD];
        memset(met, 0, SENDERS_NUMBER * PACKAGES_PER_THREAD);
        std::vector<std::thread> threads;


        services::ITaskResult responseResult;
        services::ResponseCallback callback = [met](services::ITaskResult res) {
            try {
                // BOOST_TEST_MESSAGE(res.GetResult());
                int index = std::stoi(res.GetResult());
                met[index] += 1;
            } catch (...) {}
        };
        services::BoostAsioTaskPublisher publisher(std::make_unique<services::JSONProtocol>());
        publisher.StartService(callback, "127.0.0.1", 12345);
        std::this_thread::sleep_for(timeout);
        auto routine = [PACKAGES_PER_THREAD](int result, unsigned short port) {
            for (size_t index = 0; index < PACKAGES_PER_THREAD; ++index) {
                std::stringstream ss;
                ss << R"({"id":"d4e39cdd-5b50-4305-8bce-bd8a762f1711","status":"1","result":")" << index + result
                   << "\"}";
                SimpleClient(ss.str(), port);
            }
        };
        for (int i = 0; i < SENDERS_NUMBER; ++i) {
            threads.emplace_back(routine, i * PACKAGES_PER_THREAD, publisher.GetListeningPort());
        }
        for (auto& thread : threads) thread.join();

        std::this_thread::sleep_for(2 * timeout);

        for (size_t i = 0; i < PACKAGES_PER_THREAD * SENDERS_NUMBER; ++i) {
            if (1 != met[i]) {
                BOOST_CHECK(1 == met[i]);
                BOOST_TEST_MESSAGE("At " << i << " found: " << (unsigned short) met[i]);
            }
        }
        publisher.StopServer();
        std::this_thread::sleep_for(timeout);
    }

BOOST_AUTO_TEST_SUITE_END()
