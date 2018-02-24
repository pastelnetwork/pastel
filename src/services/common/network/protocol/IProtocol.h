
#pragma once


#include "task/task/ITask.h"
#include "util/Types.h"

namespace services {
    class IProtocol {
    public:
        enum SerializeResult {
            Success,
            NullTaskPtr,
            SerializationError
        };
        enum DeserializeResult {
            Success,
        };

        virtual SerializeResult Serialize(std::vector<byte> &dstBuffer, const std::shared_ptr<ITask> &srcTask) = 0;

        virtual DeserializeResult Deserialize(ITaskResult &dstTaskResult, const std::vector<byte> &srcBuffer) = 0;
    };
}

