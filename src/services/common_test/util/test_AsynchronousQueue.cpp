#include <boost/test/unit_test.hpp>
#include <unordered_set>
#include "util/AsynchronousQueue.h"

BOOST_AUTO_TEST_SUITE(TestAsynchronousQueue)

    BOOST_AUTO_TEST_CASE(push_popnowait) {
        const size_t PRODUCERS_NUMBER = 30;
        const size_t CONSUMERS_NUMBER = 10;
        const size_t TIME_TO_WAIT = 10;
        const size_t TASKS_PER_THREAD = 100;
        auto queue = std::make_shared<AsynchronousQueue<size_t>>();
        char* met = new char[PRODUCERS_NUMBER * TASKS_PER_THREAD];
        memset(met, 0, PRODUCERS_NUMBER * TASKS_PER_THREAD);
        std::vector<std::thread> threads;

        auto producerRoutine = [queue, PRODUCERS_NUMBER](size_t id) {
            for (size_t i = 0; i < TASKS_PER_THREAD; ++i) {
                queue->Push(id + i);
                std::this_thread::sleep_for(std::chrono::milliseconds(TIME_TO_WAIT));
            }
        };
        auto consumerRoutine = [queue, met]() {
            size_t emptyCounter = 0;
            size_t item;
            while (true) {
                if (queue->PopNoWait(item)) {
                    met[item] = 1;
                    emptyCounter = 0;
                } else {
                    emptyCounter++;
                    if (emptyCounter >= 5) {
                        break;
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(TIME_TO_WAIT));
            }
        };

        for (int i = 0; i < PRODUCERS_NUMBER; ++i) threads.emplace_back(producerRoutine, i * TASKS_PER_THREAD);
        for (int i = 0; i < CONSUMERS_NUMBER; ++i) threads.emplace_back(consumerRoutine);

        for (auto& thread : threads) thread.join();

        for (size_t i = 0; i < PRODUCERS_NUMBER * TASKS_PER_THREAD; ++i) {
            if (!met[i]) {
                BOOST_CHECK(met[i]);
            }
        }
    }

    BOOST_AUTO_TEST_CASE(push_pop) {
        const size_t THREADS_NUMBER = 10;
        const size_t TIME_TO_WAIT = 10;
        const size_t TASKS_PER_THREAD = 100;
        auto queue = std::make_shared<AsynchronousQueue<size_t>>();
        char* met = new char[THREADS_NUMBER * TASKS_PER_THREAD];
        memset(met, 0, THREADS_NUMBER * TASKS_PER_THREAD);
        std::vector<std::thread> threads;

        auto producerRoutine = [queue](size_t id) {
            for (size_t i = 0; i < TASKS_PER_THREAD; ++i) {
                queue->Push(id + i);
                std::this_thread::sleep_for(std::chrono::milliseconds(TIME_TO_WAIT));
            }
        };
        auto consumerRoutine = [queue, met]() {
            for (int i = 0; i < TASKS_PER_THREAD; ++i) {
                met[queue->Pop()] = 1;
                std::this_thread::sleep_for(std::chrono::milliseconds(TIME_TO_WAIT));
            }
        };

        for (int i = 0; i < THREADS_NUMBER; ++i) threads.emplace_back(producerRoutine, i * TASKS_PER_THREAD);
        for (int i = 0; i < THREADS_NUMBER; ++i) threads.emplace_back(consumerRoutine);

        for (auto& thread : threads) thread.join();

        for (size_t i = 0; i < THREADS_NUMBER * TASKS_PER_THREAD; ++i) {
            if (!met[i]) {
                BOOST_CHECK(met[i]);
            }
        }
    }

BOOST_AUTO_TEST_SUITE_END()

