//
// Created by artem on 10.02.18.
//

#pragma once


#include <thread>
#include <unordered_map>
#include <mutex>
#include <boost/functional/hash.hpp>
#include <task/task/FinishTask.h>
#include <util/AsynchronousQueue.h>
#include <task/task_result/InappropriateTaskResult.h>
#include <task/task_result/AttemptsExhaustedResult.h>
#include <network/ITaskPublisher.h>
#include <functional>

namespace nsfw {
    class ITaskScheduler {
    public:
        const double SECONDS_BETWEEN_ATTEMPTS = 20.0;
        const size_t MAX_NUMBER_OF_ATTEMPTS = 5;
        const std::chrono::milliseconds SCHEDULER_SLEEP_TIME = std::chrono::milliseconds(100);

        ITaskScheduler() {
            ResponseCallback callback = (ResponseCallback)
                    std::bind(&ITaskScheduler::OnTaskCompleted, this, std::placeholders::_1);
            publisher.StartService(callback);
            schedulerThread = std::thread(&ITaskScheduler::SchedulerRoutine, this);
            workQueue = std::unique_ptr<AsynchronousQueue<std::shared_ptr<ITask>>>(
                    new AsynchronousQueue<std::shared_ptr<ITask>>());
            nextQueue = std::unique_ptr<AsynchronousQueue<std::shared_ptr<ITask>>>(
                    new AsynchronousQueue<std::shared_ptr<ITask>>());
        }

        ~ITaskScheduler() {
            AddTask(std::shared_ptr<ITask>(new FinishTask()));
            schedulerThread.join();
        }

        void AddTask(const std::shared_ptr<ITask> &task) {
            if (!task->GetResponseCallback())
                return; // there is no one who want to get the result of task
            std::lock_guard<std::mutex> mlock(mapMutex);
            tasksInWork.emplace(task->GetId(), task);
            workQueue->Push(task);
        }

        void DeleteTask(const boost::uuids::uuid &id) {
            std::lock_guard<std::mutex> mlock(mapMutex);
            tasksInWork.erase(id);
        }

        bool IsTaskInWork(const boost::uuids::uuid &id) const {
            std::lock_guard<std::mutex> mlock(mapMutex);
            bool result = tasksInWork.count(id) > 0;
            return result;
        }

        void OnTaskCompleted(ITaskResult taskResult) {
            auto found = tasksInWork.find(taskResult.GetId());
            if (found != tasksInWork.end()) {
                found->second->GetResponseCallback()(taskResult);
                DeleteTask(taskResult.GetId());
            }
        }

    protected:
        void SchedulerRoutine() {
            while (true) {
                std::shared_ptr<ITask> task;
                if (workQueue->PopNoWait(task)) {
                    if (task->GetType() == TaskType::FinishWork)
                        break;
                    if (!IsAppropriateTask(task)) {
                        task->GetResponseCallback()(InappropriateTaskResult(task->GetId()));
                        continue;
                    }
                    if (MAX_NUMBER_OF_ATTEMPTS > task->GetAttemptsCount()) {
                        task->GetResponseCallback()(AttemptsExhaustedResult(task->GetId()));
                        continue;
                    }
                    if (SECONDS_BETWEEN_ATTEMPTS > task->GetSecondsFromLastAttempt()) {
                        nextQueue->Push(task);
                        continue;
                    }
                    if (!IsTaskInWork(task->GetId())) {
                        // we already processed this task and answered in OnTaskCompleted
                        continue;
                    }
                    HandleTask(task);
                    task->MakeAttempt();
                    nextQueue->Push(task);
                } else {
                    std::swap(workQueue, nextQueue);    // TODO: check operation for thread safe
                    std::this_thread::sleep_for(SCHEDULER_SLEEP_TIME);
                }
            }
        }

//        ResponseCallback MakeResponseCallback() {
//            ResponseCallback callback = std::bind(&ITaskScheduler::OnTaskCompleted, *this);
//            return callback;
//        }

        virtual bool IsAppropriateTask(std::shared_ptr<ITask> &task) = 0;

        virtual void HandleTask(std::shared_ptr<ITask> &task) = 0;

        ITaskPublisher publisher;
        std::thread schedulerThread;
        std::unordered_map<boost::uuids::uuid, std::shared_ptr<ITask>, boost::hash<boost::uuids::uuid>> tasksInWork;
        mutable std::mutex mapMutex;
        std::unique_ptr<AsynchronousQueue<std::shared_ptr<ITask>>> workQueue;
        std::unique_ptr<AsynchronousQueue<std::shared_ptr<ITask>>> nextQueue;
    };
}

