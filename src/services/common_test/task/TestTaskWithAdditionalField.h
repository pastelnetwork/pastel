
#pragma once


#include <task/task/ITask.h>
#include <unordered_map>

namespace services {


    class TestTaskWithAdditionalField : public ITask {
    public:
        TaskType GetType() const override {
            return TaskType::TT_Test;
        }

        const std::string &GetAdditionalField() const {
            return additionalField;
        }

        void SetAdditionalField(const std::string &additionalField) {
            TestTaskWithAdditionalField::additionalField = additionalField;
        }

        std::unordered_map<std::string, std::vector<byte>> AdditionalFieldsToSerialize() override {
            std::unordered_map<std::string, std::vector<byte>> map;
            std::vector<byte> data(additionalField.begin(), additionalField.end());
            map["test_field"] = data;
            return map;
        }

        bool ParseAdditionalFields(std::unordered_map<std::string, std::vector<byte>> map) override {
            if (map.count("test_field") > 0) {
                additionalField = std::string(map["test_field"].begin(), map["test_field"].end());
                return true;
            }
            return false;
        }

    private:
        std::string additionalField;
    };
}