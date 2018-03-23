#include <boost/test/unit_test.hpp>
#include <iostream>
#include <sstream>
#include "task/TestTaskWithAdditionalField.h"
#include "network/protocol/JSONProtocol.h"

BOOST_AUTO_TEST_SUITE(TestJSONProtocol)

    BOOST_AUTO_TEST_CASE(serialization_success) {
        std::string testValue = "TestValue 0123_#!";
        services::JSONProtocol jsonProtocol;
        auto task = std::make_shared<services::TestTaskWithAdditionalField>();
        task->SetAdditionalField(testValue);
        std::vector<services::byte> buf;
        auto serializeResult = jsonProtocol.Serialize(buf, task);
//        std::cout << std::string(buf.begin(), buf.end()) << std::endl;
        BOOST_CHECK_EQUAL(serializeResult, services::IProtocol::SerializeResult::SR_Success);
    }

    BOOST_AUTO_TEST_CASE(serialization_null_task_ptr) {
        services::JSONProtocol jsonProtocol;
        auto task = std::shared_ptr<services::TestTaskWithAdditionalField>();
        std::vector<services::byte> buf;
        auto serializeResult = jsonProtocol.Serialize(buf, task);
        BOOST_CHECK_EQUAL(serializeResult, services::IProtocol::SerializeResult::SR_NullTaskPtr);
    }

    BOOST_AUTO_TEST_CASE(deserialization_success) {
        services::JSONProtocol jsonProtocol;
        services::ITaskResult taskResult;
        std::stringstream ss;
        std::string id("d4e39cdd-5b50-4305-8bce-bd8a762f1711");
        services::TaskResultStatus status = services::TaskResultStatus::TRS_InappropriateTask;
        std::string result("42 %");
        std::string message("No additional message");

        ss << R"({"id":")" << id << R"(","status":")" << status << R"(","result":")" << result << R"(","message":")"
           << message << "\"}";
        std::string rawStr = ss.str();
        std::vector<services::byte> buf(rawStr.begin(), rawStr.end());
        auto deserializeResult = jsonProtocol.Deserialize(taskResult, buf);
        BOOST_CHECK_EQUAL(deserializeResult, services::IProtocol::DeserializeResult::DR_Success);
        BOOST_CHECK_EQUAL(boost::uuids::to_string(taskResult.GetId()), id);
        BOOST_CHECK_EQUAL(taskResult.GetStatus(), status);
        BOOST_CHECK_EQUAL(taskResult.GetResult(), result);
        BOOST_CHECK_EQUAL(taskResult.GetMessage(), message);
    }

    BOOST_AUTO_TEST_CASE(deserialization_success_no_message) {
        services::JSONProtocol jsonProtocol;
        services::ITaskResult taskResult;
        std::stringstream ss;
        std::string id("d4e39cdd-5b50-4305-8bce-bd8a762f1711");
        services::TaskResultStatus status = services::TaskResultStatus::TRS_InappropriateTask;
        std::string result("42 %");
        ss << R"({"id":")" << id << R"(","status":")" << status << R"(","result":")" << result << "\"}";
        std::string rawStr = ss.str();
//        std::cout<<rawStr<<std::endl;
        std::vector<services::byte> buf(rawStr.begin(), rawStr.end());
        auto deserializeResult = jsonProtocol.Deserialize(taskResult, buf);
        BOOST_CHECK_EQUAL(deserializeResult, services::IProtocol::DeserializeResult::DR_Success);
        BOOST_CHECK_EQUAL(boost::uuids::to_string(taskResult.GetId()), id);
        BOOST_CHECK_EQUAL(taskResult.GetStatus(), status);
        BOOST_CHECK_EQUAL(taskResult.GetResult(), result);
    }

    BOOST_AUTO_TEST_CASE(deserialization_err_no_result) {
        services::JSONProtocol jsonProtocol;
        services::ITaskResult taskResult;
        std::stringstream ss;
        std::string id("d4e39cdd-5b50-4305-8bce-bd8a762f1711");
        services::TaskResultStatus status = services::TaskResultStatus::TRS_InappropriateTask;
        std::string message("No additional message");
        ss << R"({"id":")" << id << R"(","status":")" << status << R"(","message":")" << message << "\"}";
        std::string rawStr = ss.str();
        std::vector<services::byte> buf(rawStr.begin(), rawStr.end());
        auto deserializeResult = jsonProtocol.Deserialize(taskResult, buf);
        BOOST_CHECK_EQUAL(deserializeResult, services::IProtocol::DeserializeResult::DR_InvalidFormatJSON);
    }


    BOOST_AUTO_TEST_CASE(deserialization_err_no_id) {
        services::JSONProtocol jsonProtocol;
        services::ITaskResult taskResult;
        std::stringstream ss;
        std::string id("d4e39cdd-5b50-4305-8bce-bd8a762f1711");
        services::TaskResultStatus status = services::TaskResultStatus::TRS_InappropriateTask;
        std::string result("42 %");
        ss << R"({"status":")" << status << R"(","result":")" << result << "\"}";
        std::string rawStr = ss.str();
        std::vector<services::byte> buf(rawStr.begin(), rawStr.end());
        auto deserializeResult = jsonProtocol.Deserialize(taskResult, buf);
        BOOST_CHECK_EQUAL(deserializeResult, services::IProtocol::DeserializeResult::DR_InvalidFormatJSON);
    }

    BOOST_AUTO_TEST_CASE(deserialization_err_invalid_json) {
        services::JSONProtocol jsonProtocol;
        services::ITaskResult taskResult;
        std::stringstream ss;
        std::string id("d4e39cdd-5b50-4305-8bce-bd8a762f1711");
        services::TaskResultStatus status = services::TaskResultStatus::TRS_InappropriateTask;
        std::string result("42 %");
        ss << R"({[]"id":")" << id << R"(","status":")" << status << R"(","result":")" << result << "\"}";
        std::string rawStr = ss.str();
        std::vector<services::byte> buf(rawStr.begin(), rawStr.end());
        auto deserializeResult = jsonProtocol.Deserialize(taskResult, buf);
        BOOST_CHECK_EQUAL(deserializeResult, services::IProtocol::DeserializeResult::DR_InvalidJSON);
    }


BOOST_AUTO_TEST_SUITE_END()
