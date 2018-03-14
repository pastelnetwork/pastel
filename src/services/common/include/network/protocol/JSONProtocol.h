#pragma once

#include <boost/uuid/uuid_io.hpp>
#include <unordered_map>
#include "network/protocol/IProtocol.h"
#include "util/univalue.h" // TODO:include UniValue
#include "util/utilstrencodings.h" // TODO:include Encode/Decode Base64

namespace services {
    class JSONProtocol : public IProtocol {
    public:
        virtual SerializeResult
        Serialize(std::vector<byte>& dstBuffer, const std::shared_ptr<ITask>& srcTask) const override {
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
                if (!result) {
                    break;
                }
            }

            if (!result) {
                return SerializeResult::SR_SerializationError;
            } else {
                std::string str = uniValue.write();
                dstBuffer = std::vector<byte>(str.begin(), str.end());
                return SerializeResult::SR_Success;
            }
        }

        virtual DeserializeResult
        Deserialize(ITaskResult& dstTaskResult, const std::vector<byte>& srcBuffer) const override {
            UniValue uniValue;
            if (!uniValue.read(reinterpret_cast<const char*>(srcBuffer.data()), srcBuffer.size())) {
                return DeserializeResult::DR_InvalidJSON;
            }
            if (!uniValue.isObject()) {
                return DeserializeResult::DR_InvalidFormatJSON;
            }

            ITaskResult result;

            if (!ParseIdField(uniValue, result)) {
                return DeserializeResult::DR_InvalidFormatJSON;
            }
            if (!ParseStatusField(uniValue, result)) {
                return DeserializeResult::DR_InvalidFormatJSON;
            }
            if (!ParseResultField(uniValue, result)) {
                return DeserializeResult::DR_InvalidFormatJSON;
            }
            if (!ParseMessageField(uniValue, result)) {
                return DeserializeResult::DR_InvalidFormatJSON;
            }

            dstTaskResult = result;
            return DeserializeResult::DR_Success;
        };

        IProtocol* Clone() const override {
            return new JSONProtocol();
        }

    protected:
        bool SerializeTaskHeader(UniValue& uniValue, const TaskHeader& taskHeader) const {
            bool result = true;
            UniValue hdrUniValue(UniValue::VOBJ);
            result &= hdrUniValue.push_back(Pair("type", taskHeader.GetType()));
            result &= hdrUniValue.push_back(Pair("id", boost::uuids::to_string(taskHeader.GetId())));
            result &= uniValue.push_back(Pair("header", hdrUniValue));
            return result;
        }

        bool ParseStatusField(const UniValue& uniValue, ITaskResult& result) const {
            auto statusVal = find_value(uniValue, "status");
            if (!statusVal.isStr() && !statusVal.isNum()) {
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
                return true;
            };

            return false;
        }

        bool ParseIdField(const UniValue& uniValue, ITaskResult& result) const {
            auto idVal = find_value(uniValue, "id");
            if (!idVal.isStr()) {
                return false;
            }
            boost::uuids::string_generator string_gen;
            try {
                result.SetId(string_gen(idVal.get_str()));
                return true;
            } catch (int e) {
                return false;
            }
        }

        bool ParseResultField(const UniValue& uniValue, ITaskResult& result) const {
            auto resultVal = find_value(uniValue, "result");
            if (resultVal.isStr()) {
                result.SetResult(resultVal.get_str());
                return true;
            }
            return false;
        }

        bool ParseMessageField(const UniValue& uniValue, ITaskResult& result) const {
            auto messageVal = find_value(uniValue, "message");
            if (messageVal.isStr()) {
                result.SetMessage(messageVal.get_str());
                return true;
            } else if (messageVal.isNull()) {    // may be zero (if no error occur)
                result.SetMessage(std::string());
                return true;
            }
            return false;
        }
    };
}