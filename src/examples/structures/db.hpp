#pragma once

#include <string>
#include <cstdint>
#include <vector>
#include <exception>
#include "serialization/annotations.hpp"
#include "serialization/promise.hpp"

namespace waos {

  using binary = std::vector<std::uint8_t>;

  enum class db_write_mode : std::uint8_t {
    durable = 0,
    instant = 1,
  };

  // present=false ⇒ missing key; present=true + empty value ⇒ tombstone.
  struct db_get_result {
    bool present = false;
    binary value;
    std::uint64_t timestamp = 0;
  };

  struct db_write_options {
    db_write_mode mode = db_write_mode::durable;
    bool notify_visible = false;
    bool notify_durable = false;
  };

  // empty value ⇒ tombstone (same as store set / set_bulk).
  struct db_write_entry {
    binary key;
    binary value;
    std::uint64_t timestamp = 0;
  };

  // none ⇒ bound absent (key ignored); exclusive ⇒ open (gt/lt); inclusive ⇒ closed (gte/lte).
  enum class db_bound_mode : std::uint8_t {
    none = 0,
    exclusive = 1,
    inclusive = 2,
  };

  struct db_scan_query {
    binary lower;
    db_bound_mode lower_mode = db_bound_mode::none;
    binary upper;
    db_bound_mode upper_mode = db_bound_mode::none;
    std::uint64_t limit = 0;
    bool reverse = false;
    bool keys_only = false;
  };

  struct [[=serialization::exception()]] db_error : public std::exception {
    db_error() : message_() {}
    explicit db_error(const std::string& message) : message_(message) {}
    ~db_error() override {}
    const char* what() const noexcept override { return message_.c_str(); }
  private:
    std::string message_;
  };

  struct [[=serialization::exception()]] db_invalid_argument : public db_error {
    db_invalid_argument() = default;
    explicit db_invalid_argument(const std::string& message) : db_error(message) {}
  };

  struct [[=serialization::exception()]] db_closed : public db_error {
    db_closed() = default;
    explicit db_closed(const std::string& message) : db_error(message) {}
  };

  struct [[=serialization::exception()]] db_io_error : public db_error {
    db_io_error() = default;
    explicit db_io_error(const std::string& message) : db_error(message) {}
  };

  // Empty readiness signal on a newly bound private channel (no db_socket_id).
  struct [[=serialization::message()]] db_socket_opened {
  };

  struct [[=serialization::message()]] db_visible_event {
    std::uint64_t request_id = 0;
  };

  struct [[=serialization::message()]] db_durable_event {
    std::uint64_t request_id = 0;
  };

  struct [[=serialization::message()]] db_scan_item {
    std::uint64_t scan_uid = 0;
    binary key;
    binary value;
    std::uint64_t timestamp = 0;
  };

  // keys_only scan item (no value / timestamp).
  struct [[=serialization::message()]] db_scan_key {
    std::uint64_t scan_uid = 0;
    binary key;
  };

  struct [[=serialization::message()]] db_scan_done {
    std::uint64_t scan_uid = 0;
  };

  struct [[=serialization::message()]] db_scan_error {
    std::uint64_t scan_uid = 0;
    std::string message;
  };

  // Full API on control USER socket and on every private channel (open_socket).
  class db {
    public:
      db() = default;

      static std::shared_ptr<serialization::promise<db_get_result>> get(binary key) {
        return serialization::promise<db_get_result>::resolved(db_get_result{});
      }

      // empty value ⇒ tombstone.
      static std::shared_ptr<serialization::promise<void>> set(
          binary key, binary value, std::uint64_t timestamp, db_write_options opts) {
        return serialization::promise<void>::resolved();
      }

      static std::shared_ptr<serialization::promise<void>> batch(
          std::vector<db_write_entry> entries, db_write_options opts) {
        return serialization::promise<void>::resolved();
      }

      static std::shared_ptr<serialization::promise<void>> open_socket(
          std::uint64_t client_pid, std::uint32_t client_socket) {
        return serialization::promise<void>::resolved();
      }

      static std::shared_ptr<serialization::promise<void>> close_socket(
          std::uint64_t client_pid, std::uint32_t client_socket) {
        return serialization::promise<void>::resolved();
      }

      // scan_uid for messages = RPC framing request_id of this call.
      static std::shared_ptr<serialization::promise<void>> start_scan(
          std::uint64_t client_pid, std::uint32_t client_socket, db_scan_query query) {
        return serialization::promise<void>::resolved();
      }

      static std::shared_ptr<serialization::promise<void>> cancel_scan(std::uint64_t scan_uid) {
        return serialization::promise<void>::resolved();
      }
  };

}
