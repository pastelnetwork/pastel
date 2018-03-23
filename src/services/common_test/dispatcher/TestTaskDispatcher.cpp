#include <boost/test/unit_test.hpp>

#include "dispatcher/TaskDispatcher.h"
#include "task/TestTask.h"

BOOST_AUTO_TEST_SUITE(TestTaskDispatcher)

//    BOOST_AUTO_TEST_CASE(mutability) {
//        services::TaskDispatcher dispatcher;
//        auto executorDispatcher = std::make_unique<services::ExecutorDispatcher>(10u, 5u);
//        dispatcher.Register(services::TaskType::TT_Test, std::move(executorDispatcher));
//        int i = 1;
//        BOOST_TEST(i);
//        BOOST_TEST(i == 2);
//    }

BOOST_AUTO_TEST_SUITE_END()