#include <boost/test/unit_test.hpp>
#include <task/TestTaskWithAdditionalField.h>
#include "network/protocol/JSONProtocol.h"

BOOST_AUTO_TEST_SUITE(TestJSONProtocol)

    BOOST_AUTO_TEST_CASE(serialization) {
        std::string testValue = "TestValue0123_#!";
        services::JSONProtocol jsonProtocol;
        auto task = std::make_shared<services::TestTaskWithAdditionalField>();
        task->SetAdditionalField(testValue);
        std::vector<services::byte> buf;
        auto serializeResult = jsonProtocol.Serialize(buf, task);
        BOOST_CHECK_EQUAL(serializeResult, services::IProtocol::SerializeResult::SR_Success);
    }

//    BOOST_AUTO_TEST_CASE(deserialization) {
//        services::JSONProtocol jsonProtocol;
//        services::ITaskResult taskResult;
//        std::vector<services::byte> buf;
//        auto serializeResult = jsonProtocol.Deserialize(buf, task);
//        BOOST_CHECK_EQUAL(serializeResult, services::IProtocol::SerializeResult::SR_Success);
//    }


BOOST_AUTO_TEST_SUITE_END()
