
#pragma once


#include <string>
#include "ITaskScheduler.h"

namespace services {
    class FactoryException : public std::exception {
        std::string message;
    public:
        FactoryException(const std::string &msg) throw() : message(msg) {}

        virtual ~ObjectFactoryException() throw() {}

        virtual const char *what() const throw() {
            return message.c_str();
        }
    };

    class SchedulerFactory {
    public:
        SchedulerFactory(std::unique_ptr<ITaskScheduler> proto) {
            if (!proto.get())
                throw FactoryException("No ITaskScheduler object provided");
            prototype = std::move(proto);
        }

        ITaskScheduler *MakeScheduler(){
            return prototype->Clone();
        }


    protected:
        std::unique_ptr<ITaskScheduler> prototype;
//        std::unordered_map<std::string, ITaskScheduler>
    };
}

