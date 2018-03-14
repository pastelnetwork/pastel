
#include <boost/test/unit_test.hpp>
#include <network/protocol/JSONProtocol.h>
#include <network/publisher/TestTaskPublisher.h>
#include "TestTaskScheduler.h"

BOOST_AUTO_TEST_SUITE(TestTaskScheduler)

    BOOST_AUTO_TEST_CASE(inappropriative_task) {
        auto publisher = std::make_unique<services::TestTaskPublisher>(std::make_unique<services::JSONProtocol>());
        services::TestTaskScheduler scheduler(std::move(publisher));

        int i = 1;
        BOOST_TEST(i);
//        BOOST_TEST(i == 2);
    }

BOOST_AUTO_TEST_SUITE_END()

