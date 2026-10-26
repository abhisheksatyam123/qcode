#pragma once

#include <string>

namespace qcode {

// Abstract provider interface for AI services
class IProvider {
public:
    virtual ~IProvider() = default;
    virtual std::string name() const = 0;
    virtual std::string request(const std::string &prompt) = 0;
};

// Abstract logger interface
class ILogger {
public:
    virtual ~ILogger() = default;
    virtual void log(const std::string &msg) = 0;
};

} // namespace qcode
