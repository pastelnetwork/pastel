

#pragma once

#include <thread>
#include <functional>
#include "task/task/ITask.h"

namespace nsfw {

    enum SendResult {

    };

    class ITaskPublisher {
    public:

        void StartService(ResponseCallback &onReceiveCallback) {
            callback = onReceiveCallback;
        }

        SendResult Send(const std::shared_ptr<nsfw::ITask> &task);

    protected:
//        virtual void ListenerRoutine() = 0;

        ResponseCallback callback;
//        std::thread listenerThread;
    };

}


