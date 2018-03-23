#pragma once

#include <functional>
#include <time.h>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid.hpp>
#include "task/task_result/ITaskResult.h"

namespace services {
    enum TaskType {
        TT_Test,
        TT_TestInappropriate,
        TT_FinishWork,
        TT_CheckNSFW,
    };

    class TaskHeader {
    public:
        TaskHeader() {
            boost::uuids::random_generator uuidGenerator;
            id = uuidGenerator();
            time(&createTime);  // get current time
            attemptsCount = 0;
        }

        TaskHeader(TaskType taskType, ResponseCallback &callbackForResponse) : TaskHeader() {
            type = taskType;
            callback = callbackForResponse;
        }

        boost::uuids::uuid GetId() const { return id; }

        TaskType GetType() const { return type; }

        ResponseCallback GetResponseCallback() const { return callback; }

        time_t GetCreateTime() const {
            return createTime;
        }

        time_t GetLastAttemptTime() const {
            return lastAttemptTime;
        }

        size_t GetAttemptsCount() const {
            return attemptsCount;
        }

        bool operator==(TaskHeader const &another) const {
            return id == another.GetId();
        }

        void MakeAttempt() {
            time(&lastAttemptTime);
            attemptsCount++;
        }


    protected:
        time_t createTime;
        time_t lastAttemptTime;
        size_t attemptsCount;
        ResponseCallback callback;
        TaskType type;
        boost::uuids::uuid id;
    };
}

