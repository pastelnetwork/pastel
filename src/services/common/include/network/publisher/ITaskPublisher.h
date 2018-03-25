

#pragma once

#include <thread>
#include <functional>
#include "consts/Enums.h"
#include "network/protocol/IProtocol.h"
#include "task/task/ITask.h"

namespace services {

    class ITaskPublisher {
    public:
        // Assume to call new ITaskPublisher (make_unique<IProtocolImpementation> ())
        ITaskPublisher(std::unique_ptr<IProtocol> protocol) {
            this->protocol = std::move(protocol);
        }

        virtual void StartService(ResponseCallback& onReceiveCallback) {
            callback = onReceiveCallback;
        }

        SendResult Send(const std::shared_ptr<ITask>& task) {
            std::vector<byte> buf;
            auto serializeResult = protocol->Serialize(buf, task);
            if (IProtocol::SerializeResult::SR_Success == serializeResult) {
                return Send(buf);
            } else {
                return SendResult_ProtocolError;
            }
        }

        virtual ITaskPublisher* Clone() const =0;

    protected:
        virtual SendResult Send(const std::vector<byte>& buffer) = 0;

        void OnRecieve(const std::vector<byte>& buffer) const {
            ITaskResult result;
            if (IProtocol::DeserializeResult::DR_Success == protocol->Deserialize(result, buffer)) {
                callback(result);
            }
        }

        std::unique_ptr<IProtocol> protocol;
        ResponseCallback callback;
    };

}


