#include <iostream>
#include <thread>
#include "util/AsynchronousQueue.h"

void ConsumerFunction(AsynchronousQueue<int> &queue) {
    for (int i = 0; i < 10; ++i) {
        std::cout << "Consumer: " << queue.Pop() << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds());
    }
}

void ProducerFunction(AsynchronousQueue<int> &queue) {
    for (int i = 0; i < 10; ++i) {
        std::cout << "Producer: " << i + 1 << std::endl;
        queue.Push(i + 1);
        std::this_thread::sleep_for(std::chrono::milliseconds());
    }
}

int testQueue() {
    AsynchronousQueue<int> queue;
    auto producer = std::thread (ProducerFunction, std::ref(queue));
    auto consumer = std::thread (ConsumerFunction, std::ref(queue));
    consumer.join();
    producer.join();
    return 0;
}