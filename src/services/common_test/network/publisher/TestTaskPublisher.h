
#pragma once

#include <thread>
#include <network/publisher/ITaskPublisher.h>
#include <consts/Enums.h>

namespace services {
    struct HashVector {
        size_t operator()(const std::vector<byte>& vec) const {
            std::size_t seed = vec.size();
            for (auto& i : vec) {
                seed ^= i + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            }
            return seed;
        }
    };

    class TestTaskPublisher : public ITaskPublisher {
    public:
        TestTaskPublisher(std::unique_ptr<IProtocol> protocol) : ITaskPublisher(std::move(protocol)) {}

        void SetSendStatus(SendResult status) {
            this->sendStatus = status;
        }

        void SetAnswer(const std::vector<byte>& request, std::chrono::milliseconds& timeout,
                       const std::vector<byte>& response) {
            answers[request] = {timeout, response};
        }


        SendResult TestSend(const std::vector<byte>& vector, SendResult result) {
            SetSendStatus(result);
            return Send(vector);
        }

        ITaskPublisher* Clone() const override {
            return new TestTaskPublisher(std::unique_ptr<IProtocol>(protocol->Clone()));
        }

    protected:
        SendResult Send(const std::vector<byte>& buffer) override {
            auto response = answers.find(buffer);
            if (response != answers.end()) {
                auto val = response->second;
                std::thread([this, response] {
                    std::this_thread::sleep_for(response->second.first);
                    this->OnRecieve(response->second.second);
                }).detach();
            }
            return sendStatus;
        }

    private:
        std::unordered_map<std::vector<byte>, std::pair<std::chrono::milliseconds, std::vector<byte>>, HashVector> answers;
        SendResult sendStatus = SendResult::SendResult_Success;
    };
}

