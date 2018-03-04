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
            return result ? SerializeResult::SR_Success : SerializeResult::SR_SerializationError;
        }

        virtual DeserializeResult Deserialize(ITaskResult &dstTaskResult, const std::vector<byte> &srcBuffer) = 0;

    protected:
        bool SerializeTaskHeader(UniValue &uniValue, const TaskHeader &taskHeader) {
            bool result = true;
            UniValue hdrUniValue(UniValue::VOBJ);
            result &= hdrUniValue.push_back(Pair("type", taskHeader.GetType()));
            result &= hdrUniValue.push_back(Pair("id", boost::uuids::to_stringg(taskHeader.GetId())));
            result &= uniValue.push_back(Pair("header", hdrUniValue));
            return result;
        }
    };
}