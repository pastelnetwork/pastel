//
// Created by artem on 10.02.18.
//

#pragma once


#include <thread>
#include <unordered_map>
#include <mutex>
#include <boost/functional/hash.hpp>
#include "consts/Enums.h"
#include "task/task/common_tasks/FinishTask.h"
#include "util/AsynchronousQueue.h"
#include "task/task_result/common_task_results/InappropriateTaskResult.h"
#include "task/task_result/common_task_results/AttemptsExhaustedResult.h"
#include "network/publisher/ITaskPublisher.h"

namespace services {
    class ITaskScheduler {
    public:
        const double SECONDS_BETWEEN_ATTEMPTS = 20.0;
        const size_t MAX_NUMBER_OF_ATTEMPTS = 5;
        const std::chrono::milliseconds SCHEDULER_SLEEP_TIME = std::chrono::milliseconds(100);

        // Assume to call new ITaskScheduler (make_unique<ITaskPublisher> (make_unique<IProtocol>()))
        ITaskScheduler(std::unique_ptr<ITaskPublisher> publisher) {
            this->publisher = std::move(publisher);
        }

        bool Run() {
            ResponseCallback callback = (ResponseCallback)
                    std::bind(&ITaskScheduler::OnTaskCompleted, this, std::placeholders::_1);
            if (!publisher.get()){
                return false;
            } else{
                publisher->StartService(callback);
                schedulerThread = std::thread(&ITaskScheduler::SchedulerRoutine, this);
                workQueue = std::make_unique<AsynchronousQueue<std::shared_ptr<ITask>>>();
                pendingQueue = std::make_unique<AsynchronousQueue<std::shared_ptr<ITask>>>();

            }
        }

        bool Stop() {
            if (schedulerThread.joinable()){
                AddTask(std::shared_ptr<ITask>(new FinishTask()));
                schedulerThread.join();
                return true;
            }
            return false;
        }

        virtual ITaskScheduler* Clone() const = 0;

        ~ITaskScheduler() {
            Stop();
        }

        AddTaskResult AddTask(const std::shared_ptr<ITask>& task) {
            if (!task->GetResponseCallback())
                return AddTaskResult::ATR_ResponseCallbackNotSet; // there is no one who want to get the result of task
            std::lock_guard<std::mutex> mlock(mapMutex);
            tasksInWork.emplace(task->GetId(), task);
            workQueue->Push(task);
            return AddTaskResult::ATR_Success;
        }

        void DeleteTask(const boost::uuids::uuid& id) {
            std::lock_guard<std::mutex> mlock(mapMutex);
            tasksInWork.erase(id);
        }

        size_t TasksCount() {
            std::lock_guard<std::mutex> mlock(mapMutex);
            return workQueue->Size();
        }

        bool IsTaskInWork(const boost::uuids::uuid& id) const {
            std::lock_guard<std::mutex> mlock(mapMutex);
            bool result = tasksInWork.count(id) > 0;
            return result;
        }

        void OnTaskCompleted(ITaskResult taskResult) {
            std::lock_guard<std::mutex> mlock(mapMutex);
            auto found = tasksInWork.find(taskResult.GetId());
            if (found != tasksInWork.end()){
                found->second->GetResponseCallback()(taskResult);
                tasksInWork.erase(taskResult.GetId());
            }
        }

    protected:
        void SchedulerRoutine() {
            while (true){
                std::shared_ptr<ITask> task;
                if (workQueue->PopNoWait(task)){
                    if (task->GetType() == TaskType::TT_FinishWork)
                        break;
                    if (!IsAppropriateTask(task)){
                        task->GetResponseCallback()(InappropriateTaskResult(task->GetId()));
                        continue;
                    }
                    if (MAX_NUMBER_OF_ATTEMPTS > task->GetAttemptsCount()){
                        task->GetResponseCallback()(AttemptsExhaustedResult(task->GetId()));
                        continue;
                    }
                    if (SECONDS_BETWEEN_ATTEMPTS > task->GetSecondsFromLastAttempt()){
                        pendingQueue->Push(task);
                        continue;
                    }
                    if (!IsTaskInWork(task->GetId())){
                        // we already processed this task and answered in OnTaskCompleted
                        continue;
                    }
                    HandleTask(task);
                    task->MakeAttempt();
                    pendingQueue->Push(task);
                } else{
                    std::lock_guard<std::mutex> mlock(mapMutex);
                    std::swap(workQueue, pendingQueue);
                    std::this_thread::sleep_for(SCHEDULER_SLEEP_TIME);
                }
            }
        }

//        ResponseCallback MakeResponseCallback() {
//            ResponseCallback callback = std::bind(&ITaskScheduler::OnTaskCompleted, *this);
//            return callback;
//        }

        virtual bool IsAppropriateTask(std::shared_ptr<ITask>& task) = 0;

        virtual void HandleTask(std::shared_ptr<ITask>& task) = 0;

        std::unique_ptr<ITaskPublisher> publisher;
        std::thread schedulerThread;
        std::unordered_map<boost::uuids::uuid, std::shared_ptr<ITask>, boost::hash<boost::uuids::uuid>> tasksInWork;
        mutable std::mutex mapMutex;
        std::unique_ptr<AsynchronousQueue<std::shared_ptr<ITask>>> workQueue;
        std::unique_ptr<AsynchronousQueue<std::shared_ptr<ITask>>> pendingQueue;
    };
}

