#pragma once

#include <string>
#include <cstdint>
#include <vector>
#include <map>
#include <exception>
#include "serialization/annotations.hpp"
#include "serialization/promise.hpp"

namespace waos {

  using binary = std::vector<std::uint8_t>;
  using labels = std::map<std::string, std::string>;

  struct begin_session_request {
    std::string name;
    std::uint64_t process_id = 0;
    labels labels;
  };

  struct begin_session_result {
    std::uint64_t session_id = 0;
    std::uint64_t store_process = 0;
    std::uint64_t store_socket = 0;
  };

  struct [[=serialization::exception()]] debug_error : public std::exception {
    debug_error() : message_() {}
    explicit debug_error(const std::string& message) : message_(message) {}
    ~debug_error() override {}
    const char* what() const noexcept override { return message_.c_str(); }
  private:
    std::string message_;
  };

  struct [[=serialization::exception()]] debug_invalid_argument : public debug_error {
    debug_invalid_argument() = default;
    explicit debug_invalid_argument(const std::string& message) : debug_error(message) {}
  };

  struct [[=serialization::exception()]] debug_conflict : public debug_error {
    debug_conflict() = default;
    explicit debug_conflict(const std::string& message) : debug_error(message) {}
  };

  struct [[=serialization::exception()]] debug_closed : public debug_error {
    debug_closed() = default;
    explicit debug_closed(const std::string& message) : debug_error(message) {}
  };

  struct [[=serialization::exception()]] debug_io_error : public debug_error {
    debug_io_error() = default;
    explicit debug_io_error(const std::string& message) : debug_error(message) {}
  };

  struct [[=serialization::message()]] debug_snapshot_event {
    std::uint64_t snapshot_uid = 0;
    std::uint64_t mana_used = 0;
    std::uint64_t mana_limit = 0;
  };

  struct [[=serialization::message()]] debug_io_event {
    std::uint32_t reserved = 0;
  };

  struct [[=serialization::message()]] debug_logs_event {
    std::uint32_t count = 0;
  };

  struct [[=serialization::message()]] debug_spans_event {
    std::uint32_t count = 0;
  };

  struct [[=serialization::message()]] debug_metrics_event {
    std::uint32_t count = 0;
  };

  struct [[=serialization::message()]] debug_bindings_event {
    std::uint32_t reserved = 0;
  };

  struct [[=serialization::message()]] debug_registry_event {
    std::uint64_t process_id = 0;
    std::uint32_t socket_id = 0;
  };

  class debug_store {
    public:
      debug_store() = default;

      static std::shared_ptr<serialization::promise<begin_session_result>> begin(begin_session_request request);
  };

  class debug_session {
    public:
      debug_session() = default;

      static std::shared_ptr<serialization::promise<void>> set_labels(labels labels);
      static std::shared_ptr<serialization::promise<void>> sync();
      static std::shared_ptr<serialization::promise<void>> seal();
      static std::shared_ptr<serialization::promise<void>> fail(std::string error);
  };

}
