
#include <boost/test/unit_test.hpp>
#include <network/protocol/JSONProtocol.h>
#include <network/publisher/TestTaskPublisher.h>
#include "task/TestInappropriateTask.h"
#include "TestTaskScheduler.h"

BOOST_AUTO_TEST_SUITE(TestTaskScheduler)

    void OnResultRecieve(services::ITaskResult* forExternalSaving, services::ITaskResult res) {
        *forExternalSaving = res;
    }

    BOOST_AUTO_TEST_CASE(inappropriative_task) {
        auto publisher = std::make_unique<services::TestTaskPublisher>(std::make_unique<services::JSONProtocol>());
        services::TestTaskScheduler scheduler(std::move(publisher));
        scheduler.Run();
        services::ITaskResult res;
        services::ResponseCallback callback = std::bind(OnResultRecieve, &res, std::placeholders::_1);
        services::TaskHeader header(services::TaskType::TT_TestInappropriate, callback);
        auto task = std::make_shared<services::TestInappropriateTask>(header);
        BOOST_CHECK_EQUAL(scheduler.AddTask(task),
                          services::AddTaskResult::ATR_Success);
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
//        scheduler.Stop();
        BOOST_CHECK_EQUAL(res.GetId(),  task->GetId() );
        BOOST_CHECK_EQUAL(res.GetStatus(), services::TaskResultStatus::TRS_InappropriateTask);
    }

    BOOST_AUTO_TEST_CASE(no_callback_set) {
        auto publisher = std::make_unique<services::TestTaskPublisher>(std::make_unique<services::JSONProtocol>());
        services::TestTaskScheduler scheduler(std::move(publisher));
        scheduler.Run();
        BOOST_CHECK_EQUAL(scheduler.AddTask(std::make_shared<services::FinishTask>()),
                          services::AddTaskResult::ATR_ResponseCallbackNotSet);
    }

BOOST_AUTO_TEST_SUITE_END()
