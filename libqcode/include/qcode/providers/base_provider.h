#pragma once

#include <qcode/types/client.h>
#include <qcode/types/stream_options.h>
#include <qcode/types/stream_result.h>
#include <memory>
#include <string>

namespace qcode {
namespace providers {

class BaseProviderClient {
public:
    virtual ~BaseProviderClient() = default;
    virtual std::unique_ptr<StreamResult> stream(const StreamOptions& options) = 0;
    virtual std::string name() const = 0;
};

} // namespace providers
} // namespace qcode
