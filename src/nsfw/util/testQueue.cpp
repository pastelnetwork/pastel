#include <iostream>
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
    std::thread producer(ProducerFunction, std::ref(queue));
    std::thread consumer(ConsumerFunction, std::ref(queue));
    consumer.join();
    producer.join();
    return 0;
}