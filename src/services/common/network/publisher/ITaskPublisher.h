

#pragma once

#include <thread>
#include <functional>
#include <network/protocol/IProtocol.h>
#include "task/task/ITask.h"

namespace services {

    enum SendResult {
        Successful,
        ProtocolError,
    };

    class ITaskPublisher {
    public:

        void StartService(ResponseCallback &onReceiveCallback) {
            callback = onReceiveCallback;
        }

        SendResult Send(const std::shared_ptr<ITask> &task) {
            std::vector buf;
            auto serializeResult = protocol.serialize(buf, task);
            if (Result::Successful == serializeResult) {
                return Send(buf);
            } else {
                return ProtocolError;
            }
        }

    protected:
        virtual SendResult Send(const std::vector<byte> &buffer) = 0;

        IProtocol protocol;
        ResponseCallback callback;
    };

}


