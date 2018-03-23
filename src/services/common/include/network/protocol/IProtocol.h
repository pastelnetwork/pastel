
#pragma once


#include "task/task/ITask.h"
#include "util/Types.h"

namespace services {
    class IProtocol {
    public:
        enum SerializeResult {
            SR_Success,
            SR_NullTaskPtr,
            SR_SerializationError
        };
        enum DeserializeResult {
            DR_Success, DR_InvalidJSON, DR_InvalidFormatJSON
        };

        virtual SerializeResult
        Serialize(std::vector<byte>& dstBuffer, const std::shared_ptr<ITask>& srcTask) const = 0;

        virtual DeserializeResult Deserialize(ITaskResult& dstTaskResult, const std::vector<byte>& srcBuffer) const = 0;

        virtual IProtocol* Clone() const = 0;
    };
}

