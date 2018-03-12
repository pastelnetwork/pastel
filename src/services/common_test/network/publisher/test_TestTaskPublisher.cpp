#include <boost/test/unit_test.hpp>
#include <iostream>
#include "task/TestTaskWithAdditionalField.h"
#include "network/protocol/JSONProtocol.h"
#include "TestTaskPublisher.h"

BOOST_AUTO_TEST_SUITE(test_TestTaskPublisher)

    void OnResultRecieve(std::string* id, services::ITaskResult res) {
        *id = boost::uuids::to_string(res.GetId());
    }


    BOOST_AUTO_TEST_CASE(test_response) {
        services::TestTaskPublisher publisher(std::make_unique<services::JSONProtocol>());
        publisher.SetSendStatus(services::SendResult::SendResult_Success);

        std::string requestStr = R"({"header":{"type":0,"id":"d4e39cdd-5b50-4305-8bce-bd8a762f1711"},"test_field":"VGVzdFZhbHVlIDAxMjNfIyE="})";
        std::vector<services::byte> request(requestStr.begin(), requestStr.end());
        std::string responseStr = R"({"id":"d4e39cdd-5b50-4305-8bce-bd8a762f1711","status":"1","result":"42 %"})";
        std::vector<services::byte> response(responseStr.begin(), responseStr.end());
        auto timeout = std::chrono::milliseconds(500);
        publisher.SetAnswer(request, timeout, response);

        std::string id;
        services::ResponseCallback callback = std::bind(OnResultRecieve, &id, std::placeholders::_1);
        publisher.StartService(callback);
        publisher.TestSend(request, services::SendResult::SendResult_Success);
        std::this_thread::sleep_for(2*timeout);

        BOOST_CHECK_EQUAL("d4e39cdd-5b50-4305-8bce-bd8a762f1711", id);
    }

BOOST_AUTO_TEST_SUITE_END()
