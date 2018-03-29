#pragma once

#include <string>
#include <exception>

namespace services {
    class BaseException : public std::exception {
        std::string message;
    public:
        BaseException(const std::string& msg) noexcept : message(msg) {}

        virtual ~BaseException() {}

        virtual const char* what() const noexcept {
            return message.c_str();
        }
    };

    class NotImplementedException : public std::exception {
    public:
        const char* what() const noexcept override { return "Function not yet implemented."; }
    };
}