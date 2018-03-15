
#include <boost/test/unit_test.hpp>
#include <network/protocol/JSONProtocol.h>
#include <network/publisher/TestTaskPublisher.h>
#include "TestTaskScheduler.h"

BOOST_AUTO_TEST_SUITE(TestTaskScheduler)

    BOOST_AUTO_TEST_CASE(inappropriative_task) {
        auto publisher = std::make_unique<services::TestTaskPublisher>(std::make_unique<services::JSONProtocol>());
        services::TestTaskScheduler scheduler(std::move(publisher));
        scheduler.Run();
        BOOST_CHECK_EQUAL(scheduler.AddTask(std::make_shared<services::FinishTask>()),
                          services::AddTaskResult::ATR_Success);

        int i = 1;
        BOOST_TEST(i);
//        BOOST_TEST(i == 2);
    }

    BOOST_AUTO_TEST_CASE(no_callback_set) {
        auto publisher = std::make_unique<services::TestTaskPublisher>(std::make_unique<services::JSONProtocol>());
        services::TestTaskScheduler scheduler(std::move(publisher));
        scheduler.Run();
        BOOST_CHECK_EQUAL(scheduler.AddTask(std::make_shared<services::FinishTask>()),
                          services::AddTaskResult::ATR_ResponseCallbackNotSet);
    }

BOOST_AUTO_TEST_SUITE_END()

