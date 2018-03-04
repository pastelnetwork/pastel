
#pragma once


#include <task/task/ITask.h>
#include <unordered_map>

namespace services {
    class TestTask : public ITask {
    public:
        TaskType GetType() const override {
            return TaskType::TT_Test;
        }

        std::unordered_map<std::string, std::vector<byte>> AdditionalFieldsToSerialize() override {
            return std::unordered_map<std::string, std::vector<byte>>();
        }

        bool ParseAdditionalFields(std::unordered_map<std::string, std::vector<byte>> map) override {
            return true;
        }
    };
}

