#pragma once

#include <boost/uuid/uuid_io.hpp>
#include <unordered_map>
#include "network/protocol/IProtocol.h"
#include "../../../../../univalue/include/univalue.h" // TODO:include UniValue
#include "../../../../../utilstrencodings.h" // TODO:include Encode/Decode Base64

namespace services {
    class JSONProtocol : public IProtocol {
    public:
        virtual SerializeResult
        Serialize(std::vector<byte> &dstBuffer, const std::shared_ptr<ITask> &srcTask) override {
            if (!srcTask.get())
                return SerializeResult::SR_NullTaskPtr;

            bool result = true;
            UniValue uniValue(UniValue::VOBJ);
            result &= SerializeTaskHeader(uniValue, srcTask->GetHeader());
            std::unordered_map<std::string, std::vector<byte>> additionalFields = srcTask->AdditionalFieldsToSerialize();
            for (auto item : additionalFields) {
                result &= uniValue.push_back(
                        Pair(item.first,
                             EncodeBase64(item.second.data(), item.second.size())
                        )
                );
            }
            std::string str = uniValue.write();
            dstBuffer = std::vector<byte>(str.begin(), str.end());
            return result ? SerializeResult::SR_Success : SerializeResult::SR_SerializationError;
        }

        virtual DeserializeResult Deserialize(ITaskResult &dstTaskResult, const std::vector<byte> &srcBuffer) override {
            UniValue uniValue;
            if (!uniValue.read(reinterpret_cast<const char *>(srcBuffer.data()), srcBuffer.size())) {
                return DeserializeResult::DR_InvalidJSON;
            }
            if (!uniValue.isObject()) {
                return DeserializeResult::DR_InvalidFormatJSON;
            }

            ITaskResult result;
            auto idVal = find_value(uniValue, "id");
            if (!idVal.isStr()) {
                return DeserializeResult::DR_InvalidFormatJSON;
            }
            boost::uuids::string_generator string_gen;
            result.SetId(string_gen(idVal.get_str()));

            if (!ParseStatusField(uniValue, result)) {
                return DeserializeResult::DR_InvalidFormatJSON;
            }
        };

    protected:
        bool SerializeTaskHeader(UniValue &uniValue, const TaskHeader &taskHeader) {
            bool result = true;
            UniValue hdrUniValue(UniValue::VOBJ);
            result &= hdrUniValue.push_back(Pair("type", taskHeader.GetType()));
            result &= hdrUniValue.push_back(Pair("id", boost::uuids::to_string(taskHeader.GetId())));
            result &= uniValue.push_back(Pair("header", hdrUniValue));
            return result;
        }

        bool ParseStatusField(const UniValue &uniValue, ITaskResult &result) {
            auto statusVal = find_value(uniValue, "status");
            if (!statusVal.isStr() || !statusVal.isNum()) {
                return false;
            }
            int status;
            if (statusVal.isStr()) {
                try {
                    status = std::stoi(statusVal.get_str());
                } catch (int e) {
                    return false;
                }
            } else {
                status = statusVal.get_int();
            }

            if (TaskResultStatus::TRS_Last > status && status >= 0) {
                result.SetStatus(static_cast<TaskResultStatus >(status));
            };

            return false;
        }
    };
}