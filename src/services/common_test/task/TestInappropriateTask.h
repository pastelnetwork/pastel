
#pragma once


#include <task/task/ITask.h>
#include <unordered_map>

namespace services {
    class TestInappropriateTask : public ITask {
    public:
        TestInappropriateTask(const TaskHeader &hdr) : ITask(hdr) {}

        TaskType GetType() const override {
            return TaskType::TT_TestInappropriate;
        }

        std::unordered_map<std::string, std::vector<byte>> AdditionalFieldsToSerialize() override {
            return std::unordered_map<std::string, std::vector<byte>>();
        }

        bool ParseAdditionalFields(std::unordered_map<std::string, std::vector<byte>> map) override {
            return true;
        }
    };
}
