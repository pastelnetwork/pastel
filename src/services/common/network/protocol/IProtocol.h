
#pragma once


#include <task/task/ITask.h>
#include <util/Types.h>

namespace nsfw {
    enum Result {
        Successful,
    };

    class IProtocol {
    public:
        virtual Result serialize(std::vector<byte> &dstBuffer, const std::shared_ptr<ITask> &srcTask) = 0;

        virtual Result deserialize(std::shared_ptr<ITask> &dstTask, const std::vector<byte> &srcBuffer) = 0;
    };
}

