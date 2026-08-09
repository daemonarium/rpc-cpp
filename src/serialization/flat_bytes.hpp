#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace serialization {

  // Contiguous string bytes: borrowed from underlying storage, or owned copy (RAII).
  class flat_string {
    public:
      flat_string() = default;

      flat_string(const flat_string& other)
        : len_(other.len_), owns_(other.owns_) {
        if(owns_) {
          owned_ = other.owned_;
          ptr_ = owned_.data();
        } else {
          ptr_ = other.ptr_;
        }
      }

      flat_string& operator=(const flat_string& other) {
        if(this == &other) return *this;
        len_ = other.len_;
        owns_ = other.owns_;
        if(owns_) {
          owned_ = other.owned_;
          ptr_ = owned_.data();
        } else {
          owned_.clear();
          ptr_ = other.ptr_;
        }
        return *this;
      }

      flat_string(flat_string&& other) noexcept
        : owned_(std::move(other.owned_)), len_(other.len_), owns_(other.owns_) {
        ptr_ = owns_ ? owned_.data() : other.ptr_;
        other.ptr_ = nullptr;
        other.len_ = 0;
        other.owns_ = false;
      }

      flat_string& operator=(flat_string&& other) noexcept {
        if(this == &other) return *this;
        owned_ = std::move(other.owned_);
        len_ = other.len_;
        owns_ = other.owns_;
        ptr_ = owns_ ? owned_.data() : other.ptr_;
        other.ptr_ = nullptr;
        other.len_ = 0;
        other.owns_ = false;
        return *this;
      }

      static flat_string borrow(const char* p, std::size_t n) {
        flat_string s;
        s.ptr_ = p;
        s.len_ = n;
        s.owns_ = false;
        return s;
      }

      static flat_string own(std::string value) {
        flat_string s;
        s.owned_ = std::move(value);
        s.ptr_ = s.owned_.data();
        s.len_ = s.owned_.size();
        s.owns_ = true;
        return s;
      }

      std::string_view view() const {
        return std::string_view(ptr_ ? ptr_ : "", len_);
      }

      const char* data() const { return ptr_; }
      std::size_t size() const { return len_; }
      bool empty() const { return len_ == 0; }
      bool owns() const { return owns_; }

      operator std::string_view() const { return view(); }

    private:
      std::string owned_;
      const char* ptr_ = nullptr;
      std::size_t len_ = 0;
      bool owns_ = false;
  };

  // Contiguous binary bytes: borrowed or owned copy (RAII).
  class flat_bytes {
    public:
      flat_bytes() = default;

      flat_bytes(const flat_bytes& other)
        : len_(other.len_), owns_(other.owns_) {
        if(owns_) {
          owned_ = other.owned_;
          ptr_ = owned_.data();
        } else {
          ptr_ = other.ptr_;
        }
      }

      flat_bytes& operator=(const flat_bytes& other) {
        if(this == &other) return *this;
        len_ = other.len_;
        owns_ = other.owns_;
        if(owns_) {
          owned_ = other.owned_;
          ptr_ = owned_.data();
        } else {
          owned_.clear();
          ptr_ = other.ptr_;
        }
        return *this;
      }

      flat_bytes(flat_bytes&& other) noexcept
        : owned_(std::move(other.owned_)), len_(other.len_), owns_(other.owns_) {
        ptr_ = owns_ ? owned_.data() : other.ptr_;
        other.ptr_ = nullptr;
        other.len_ = 0;
        other.owns_ = false;
      }

      flat_bytes& operator=(flat_bytes&& other) noexcept {
        if(this == &other) return *this;
        owned_ = std::move(other.owned_);
        len_ = other.len_;
        owns_ = other.owns_;
        ptr_ = owns_ ? owned_.data() : other.ptr_;
        other.ptr_ = nullptr;
        other.len_ = 0;
        other.owns_ = false;
        return *this;
      }

      static flat_bytes borrow(const std::uint8_t* p, std::size_t n) {
        flat_bytes b;
        b.ptr_ = p;
        b.len_ = n;
        b.owns_ = false;
        return b;
      }

      static flat_bytes own(std::vector<std::uint8_t> value) {
        flat_bytes b;
        b.owned_ = std::move(value);
        b.ptr_ = b.owned_.data();
        b.len_ = b.owned_.size();
        b.owns_ = true;
        return b;
      }

      std::span<const std::uint8_t> view() const {
        return std::span<const std::uint8_t>(ptr_ ? ptr_ : empty_ptr(), len_);
      }

      const std::uint8_t* data() const { return ptr_; }
      std::size_t size() const { return len_; }
      bool empty() const { return len_ == 0; }
      bool owns() const { return owns_; }

      operator std::span<const std::uint8_t>() const { return view(); }

    private:
      static const std::uint8_t* empty_ptr() {
        static const std::uint8_t z = 0;
        return &z;
      }

      std::vector<std::uint8_t> owned_;
      const std::uint8_t* ptr_ = nullptr;
      std::size_t len_ = 0;
      bool owns_ = false;
  };

} // namespace serialization
