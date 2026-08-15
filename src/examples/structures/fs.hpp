#pragma once

#include <string>
#include <cstdint>
#include <vector>
#include <exception>
#include "serialization/annotations.hpp"
#include "serialization/promise.hpp"

namespace waos {

  using binary = std::vector<std::uint8_t>;

  enum class open_flag : std::uint32_t {
    NONE = 0,
    READ = 1u << 0,
    WRITE = 1u << 1,
    CREATE = 1u << 2,
    TRUNCATE = 1u << 3,
    APPEND = 1u << 4,
  };

  enum class seek_whence : std::uint8_t {
    SET = 0,
    CUR = 1,
    END = 2,
  };

  struct file_info {
    bool is_directory;
    std::uint64_t size;
    std::uint64_t mtime_ms;
  };

  struct dir_entry {
    std::string name;
    bool is_directory;
    std::uint64_t size;
  };

  struct [[=serialization::exception()]] fs_error : public std::exception {
    fs_error() : message_() {}
    explicit fs_error(const std::string& message) : message_(message) {}
    ~fs_error() override {}
    const char* what() const noexcept override { return message_.c_str(); }
  private:
    std::string message_;
  };

  struct [[=serialization::exception()]] fs_not_found : public fs_error {
    fs_not_found() = default;
    explicit fs_not_found(const std::string& message) : fs_error(message) {}
  };

  struct [[=serialization::exception()]] fs_already_exists : public fs_error {
    fs_already_exists() = default;
    explicit fs_already_exists(const std::string& message) : fs_error(message) {}
  };

  struct [[=serialization::exception()]] fs_not_a_directory : public fs_error {
    fs_not_a_directory() = default;
    explicit fs_not_a_directory(const std::string& message) : fs_error(message) {}
  };

  struct [[=serialization::exception()]] fs_is_a_directory : public fs_error {
    fs_is_a_directory() = default;
    explicit fs_is_a_directory(const std::string& message) : fs_error(message) {}
  };

  struct [[=serialization::exception()]] fs_invalid_path : public fs_error {
    fs_invalid_path() = default;
    explicit fs_invalid_path(const std::string& message) : fs_error(message) {}
  };

  struct [[=serialization::exception()]] fs_invalid_handle : public fs_error {
    fs_invalid_handle() = default;
    explicit fs_invalid_handle(const std::string& message) : fs_error(message) {}
  };

  struct [[=serialization::exception()]] fs_invalid_flag : public fs_error {
    fs_invalid_flag() = default;
    explicit fs_invalid_flag(const std::string& message) : fs_error(message) {}
  };

  struct [[=serialization::exception()]] fs_io_error : public fs_error {
    fs_io_error() = default;
    explicit fs_io_error(const std::string& message) : fs_error(message) {}
  };

  class fs {
    public:
      fs() = default;

      using file_id = size_t;
      using path = std::string;

      static std::shared_ptr<serialization::promise<void>> mkdir(path path) {
        return serialization::promise<void>::resolved();
      }
      static std::shared_ptr<serialization::promise<void>> rmdir(path path) {
        return serialization::promise<void>::resolved();
      }
      static std::shared_ptr<serialization::promise<std::vector<dir_entry>>> readdir(path path) {
        return serialization::promise<std::vector<dir_entry>>::resolved({});
      }

      static std::shared_ptr<serialization::promise<void>> unlink(path path) {
        return serialization::promise<void>::resolved();
      }
      static std::shared_ptr<serialization::promise<void>> rename(path from, path to) {
        return serialization::promise<void>::resolved();
      }
      static std::shared_ptr<serialization::promise<file_info>> stat(path path) {
        return serialization::promise<file_info>::resolved(file_info{});
      }
      static std::shared_ptr<serialization::promise<bool>> exists(path path) {
        return serialization::promise<bool>::resolved(false);
      }

      static std::shared_ptr<serialization::promise<file_id>> open(path path, std::uint32_t flags) {
        return serialization::promise<file_id>::resolved(0);
      }
      static std::shared_ptr<serialization::promise<void>> close(file_id id) {
        return serialization::promise<void>::resolved();
      }

      static std::shared_ptr<serialization::promise<size_t>> write(file_id id, binary data) {
        return serialization::promise<size_t>::resolved(0);
      }
      static std::shared_ptr<serialization::promise<binary>> read(file_id id, size_t max_bytes) {
        return serialization::promise<binary>::resolved(binary{});
      }
      static std::shared_ptr<serialization::promise<std::uint64_t>> seek(
          file_id id, std::int64_t offset, seek_whence whence) {
        return serialization::promise<std::uint64_t>::resolved(0);
      }
      static std::shared_ptr<serialization::promise<void>> sync(file_id id) {
        return serialization::promise<void>::resolved();
      }
  };

}
