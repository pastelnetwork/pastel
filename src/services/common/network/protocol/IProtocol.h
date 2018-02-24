
#pragma once


#include "task/task/ITask.h"
#include "util/Types.h"

namespace services {
    class IProtocol {
    public:
        enum Result {
            Successful,
        };

        virtual Result serialize(std::vector<byte> &dstBuffer, const std::shared_ptr<ITask> &srcTask) = 0;

        virtual Result deserialize(std::shared_ptr<ITask> &dstTask, const std::vector<byte> &srcBuffer) = 0;
    };
}

