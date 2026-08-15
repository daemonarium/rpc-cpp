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

  struct record_config {
    // Threshold = event_buffer_snapshot_mult * anchor.memory.size(); only 1 or 2 allowed.
    std::uint8_t event_buffer_snapshot_mult = 1;
    bool record_mana = false;
    bool dump_on_error = true;
  };

  struct start_recording_request {
    std::uint64_t target_process = 0;
    std::uint64_t store_process = 0;
    std::string session_name;
    labels labels;
    record_config config;
    // Host-supplied target WASM bytes (empty for LocalProcess / soft-empty).
    binary code;
  };

  struct start_recording_result {
    std::uint64_t recording_id = 0;
    std::uint64_t monitor_socket = 0;
    std::uint64_t log_socket = 0;
    std::uint64_t store_session_id = 0;
    std::uint64_t store_socket = 0;
  };

  struct [[=serialization::exception()]] recorder_error : public std::exception {
    recorder_error() : message_() {}
    explicit recorder_error(const std::string& message) : message_(message) {}
    ~recorder_error() override {}
    const char* what() const noexcept override { return message_.c_str(); }
  private:
    std::string message_;
  };

  struct [[=serialization::exception()]] recorder_invalid_argument : public recorder_error {
    recorder_invalid_argument() = default;
    explicit recorder_invalid_argument(const std::string& message) : recorder_error(message) {}
  };

  struct [[=serialization::exception()]] recorder_not_found : public recorder_error {
    recorder_not_found() = default;
    explicit recorder_not_found(const std::string& message) : recorder_error(message) {}
  };

  struct [[=serialization::exception()]] recorder_conflict : public recorder_error {
    recorder_conflict() = default;
    explicit recorder_conflict(const std::string& message) : recorder_error(message) {}
  };

  struct [[=serialization::exception()]] recorder_closed : public recorder_error {
    recorder_closed() = default;
    explicit recorder_closed(const std::string& message) : recorder_error(message) {}
  };

  class recorder {
    public:
      recorder() = default;

      static std::shared_ptr<serialization::promise<start_recording_result>> start_recording(start_recording_request request);
      static std::shared_ptr<serialization::promise<void>> stop_recording(std::uint64_t recording_id);
      static std::shared_ptr<serialization::promise<void>> dump(std::uint64_t recording_id);
      /** Persist current window then clear io_bytes (unlike dump, which keeps the window). */
      static std::shared_ptr<serialization::promise<void>> dump_and_clear(std::uint64_t recording_id);
      static std::shared_ptr<serialization::promise<void>> rotate(std::uint64_t recording_id);
  };

}
