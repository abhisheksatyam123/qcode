#pragma once

#include <qcode/core/generate_options.h>
#include <qcode/core/stream_options.h>
#include <qcode/core/stream_result.h>

#include <memory>
#include <string>
#include <vector>

namespace qcode {

enum class ToolExecutionModel {
  ClientSide,       // Standard HTTP models: model requests tool calls, client executes locally
  ServerSideDuplex  // Duplex stream agents (e.g. Cursor AgentService): tools execute on bidi channel
};

class Client {
 public:
  virtual ~Client() = default;

  Client() = default;

  explicit Client(std::unique_ptr<Client> impl) : pimpl_(std::move(impl)) {}

  Client(const Client&) = delete;
  Client& operator=(const Client&) = delete;

  Client(Client&& other) noexcept = default;

  Client& operator=(Client&& other) noexcept = default;

  virtual GenerateResult generate_text(const GenerateOptions& options) {
    if (pimpl_)
      return pimpl_->generate_text(options);
    return GenerateResult("Client not initialized");
  }

  virtual StreamResult stream_text(const StreamOptions& options) {
    if (pimpl_)
      return pimpl_->stream_text(options);
    return StreamResult();
  }

  virtual bool is_valid() const {
    if (pimpl_)
      return pimpl_->is_valid();
    return false;
  }

  virtual std::string provider_name() const {
    if (pimpl_)
      return pimpl_->provider_name();
    return "unknown";
  }

  virtual std::vector<std::string> supported_models() const {
    if (pimpl_)
      return pimpl_->supported_models();
    return {};
  }

  virtual bool supports_model(const std::string& model_name) const {
    if (pimpl_)
      return pimpl_->supports_model(model_name);
    return false;
  }

  virtual std::string config_info() const {
    if (pimpl_)
      return pimpl_->config_info();
    return "No configuration";
  }

  virtual std::string default_model() const {
    if (pimpl_) {
      return pimpl_->default_model();
    }
    return "";
  }

  virtual ToolExecutionModel tool_execution_model() const {
    if (pimpl_) {
      return pimpl_->tool_execution_model();
    }
    return ToolExecutionModel::ClientSide;
  }

  // True when stream_text() yields everything generate_text() returns for a
  // tool step (text, reasoning, complete tool calls, usage), so the tool loop
  // can stream steps instead of waiting for each one.
  virtual bool supports_tool_streaming() const {
    if (pimpl_) {
      return pimpl_->supports_tool_streaming();
    }
    return false;
  }

 private:
  std::unique_ptr<Client> pimpl_;
};

}  // namespace qcode
