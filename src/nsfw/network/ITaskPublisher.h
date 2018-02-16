//
// Created by artem on 09.02.18.
//

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

        SendResult Send(ITask);

    protected:
//        virtual void ListenerRoutine() = 0;

        ResponseCallback callback;
//        std::thread listenerThread;
    };

}


