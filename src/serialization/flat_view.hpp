#pragma once

#include "annotations.hpp"
#include "deserialize.hpp"
#include "flat_encoding.hpp"
#include "flat_bytes_writer.hpp"
#include "flat_source.hpp"
#include "flat_stream_reader.hpp"
#include "meta_expand.hpp"
#include "serialize.hpp"
#include "type_concepts.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace serialization {

  template <std::size_t N>
  struct flat_fixed_string {
    char data[N]{};
    consteval flat_fixed_string(const char (&s)[N]) {
      for(std::size_t i = 0; i < N; ++i) data[i] = s[i];
    }
    consteval std::string_view view() const {
      return std::string_view(data, N > 0 ? N - 1 : 0);
    }
  };

  template <class T>
  class flat_view;

  template <class E>
  class flat_vector_view;

  template <class K, class V>
  class flat_map_view;

  class flat_session : public std::enable_shared_from_this<flat_session> {
    public:
      explicit flat_session(std::shared_ptr<std::vector<std::uint8_t>> storage)
        : storage_(std::move(storage)), source_(*storage_) {}

      span_flat_source source() const { return source_; }
      const std::vector<std::uint8_t>& bytes() const { return *storage_; }

      std::vector<std::shared_ptr<void>>& later_slots() { return later_slots_; }
      std::vector<std::shared_ptr<void>>& resolved() { return resolved_; }

      void bind_pointer_at(std::size_t abs_offset, std::shared_ptr<void> view) {
        pointer_at_[abs_offset] = std::move(view);
      }

      template <class T>
      std::shared_ptr<flat_view<T>> pointer_at(std::size_t abs_offset) const {
        auto it = pointer_at_.find(abs_offset);
        if(it == pointer_at_.end()) {
          return nullptr;
        }
        return std::static_pointer_cast<flat_view<T>>(it->second);
      }

      template <class T>
      std::shared_ptr<flat_view<T>> resolve_delta(std::int32_t delta,
          std::vector<std::shared_ptr<flat_view<T>>>& pool, std::size_t& pool_i) {
        if(delta == 0) {
          return nullptr;
        }
        if(delta < 0) {
          std::size_t index = static_cast<std::size_t>(
            static_cast<std::ptrdiff_t>(resolved_.size()) + delta);
          return std::static_pointer_cast<flat_view<T>>(resolved_.at(index));
        }
        std::size_t later_index = static_cast<std::size_t>(delta - 1);
        while(later_slots_.size() <= later_index) {
          if(pool_i >= pool.size()) {
            throw std::runtime_error("flat pointer pool exhausted");
          }
          later_slots_.push_back(pool[pool_i++]);
        }
        if(!later_slots_[later_index]) {
          if(pool_i >= pool.size()) {
            throw std::runtime_error("flat pointer pool exhausted");
          }
          later_slots_[later_index] = pool[pool_i++];
        }
        return std::static_pointer_cast<flat_view<T>>(later_slots_[later_index]);
      }

    private:
      std::shared_ptr<std::vector<std::uint8_t>> storage_;
      span_flat_source source_;
      std::vector<std::shared_ptr<void>> resolved_;
      std::vector<std::shared_ptr<void>> later_slots_;
      std::unordered_map<std::size_t, std::shared_ptr<void>> pointer_at_;
  };

  namespace flat_detail {

    template <class T, class B>
    void count_variable_members(std::size_t& count) {
      [:expand(std::meta::bases_of(^^B, std::meta::access_context::unchecked())):]
        >> [&]<std::meta::info base> {
          count_variable_members<T, typename [:std::meta::type_of(base):]>(count);
        };
      [:expand(std::meta::nonstatic_data_members_of(^^B, std::meta::access_context::unchecked())):]
        >> [&]<auto dm> {
          if constexpr(!is_member_ignored(dm)) {
            using MT = typename [:std::meta::type_of(dm):];
            if constexpr (is_flat_variable_v<MT>) {
              ++count;
            }
          }
        };
    }

    template <class T>
    std::size_t variable_count() {
      std::size_t count = 0;
      count_variable_members<T, T>(count);
      return count;
    }

    inline std::vector<std::uint32_t> load_checkpoints(span_flat_source source, std::size_t start,
        std::size_t end, std::size_t k) {
      std::vector<std::uint32_t> cps;
      if(k == 0) {
        return cps;
      }
      std::uint8_t format_end = 0;
      source.peek(end - 1, &format_end, 1);
      const std::size_t width = flat_unpack_width(format_end);
      const std::size_t trailer = flat_trailer_size(k, width);
      if(end < start || end - start < trailer) {
        throw std::runtime_error("flat_view frame too small for trailer");
      }
      const std::size_t trailer_start = end - trailer;
      std::uint8_t format = 0;
      source.peek(trailer_start, &format, 1);
      if(format != format_end) {
        throw std::runtime_error("flat_view format mismatch");
      }
      cps.resize(k);
      for(std::size_t i = 0; i < k; ++i) {
        std::uint8_t buf[4]{};
        source.peek(trailer_start + 1 + i * width, buf, width);
        cps[i] = flat_load_cp(buf, width);
      }
      return cps;
    }

    template <class T>
    std::size_t measure_frame(std::span<const std::uint8_t> bytes) {
      std::string chunk(reinterpret_cast<const char*>(bytes.data()), bytes.size());
      std::istringstream iss(chunk);
      flat_stream_reader sr(iss);
      sr.set_skip_pointers(true);
      T tmp{};
      deserialize(sr, tmp);
      std::streamoff told = iss.tellg();
      return told < 0 ? bytes.size() : static_cast<std::size_t>(told);
    }

  } // namespace flat_detail

  template <class E>
  class flat_vector_view {
    public:
      flat_vector_view(std::shared_ptr<flat_session> session, std::size_t start, std::size_t end)
        : session_(std::move(session)), start_(start), end_(end) {
        std::uint8_t buf[4];
        session_->source().peek(start_, buf, 4);
        size_ = flat_load_le<std::uint32_t>(buf);
        elem_start_ = start_ + 4;
        if constexpr (is_flat_variable_v<E>) {
          cps_ = flat_detail::load_checkpoints(session_->source(), start_, end_, size_);
        }
      }

      std::size_t size() const { return size_; }

      auto operator[](std::size_t i) const {
        if(i >= size_) {
          throw std::out_of_range("flat_vector_view index");
        }
        if constexpr (SharedPointer<E>) {
          using U = typename E::element_type;
          return session_->template pointer_at<U>(elem_start_ + i * 4);
        } else if constexpr (is_flat_variable_v<E>) {
          std::size_t abs_begin = (i == 0) ? elem_start_ : (start_ + cps_[i - 1]);
          std::size_t abs_end = start_ + cps_[i];
          if constexpr (String<E>) {
            std::uint8_t buf[4];
            session_->source().peek(abs_begin, buf, 4);
            std::uint32_t len = flat_load_le<std::uint32_t>(buf);
            const auto& bytes = session_->bytes();
            return std::string_view(reinterpret_cast<const char*>(bytes.data() + abs_begin + 4), len);
          } else {
            return flat_view<E>(session_, abs_begin, abs_end);
          }
        } else {
          constexpr std::size_t stride = flat_fixed_size<E>();
          std::uint8_t buf[8]{};
          session_->source().peek(elem_start_ + i * stride, buf, stride);
          return flat_load_le<E>(buf);
        }
      }

    private:
      std::shared_ptr<flat_session> session_;
      std::size_t start_ = 0;
      std::size_t end_ = 0;
      std::size_t size_ = 0;
      std::size_t elem_start_ = 0;
      std::vector<std::uint32_t> cps_;
  };

  namespace flat_detail {

    template <class MT>
    auto read_value_at(const std::shared_ptr<flat_session>& session, std::size_t abs,
        std::size_t abs_end) {
      if constexpr (SharedPointer<MT>) {
        using U = typename MT::element_type;
        return session->template pointer_at<U>(abs);
      } else if constexpr (String<MT>) {
        std::uint8_t buf[4];
        session->source().peek(abs, buf, 4);
        std::uint32_t len = flat_load_le<std::uint32_t>(buf);
        const auto& bytes = session->bytes();
        return std::string_view(reinterpret_cast<const char*>(bytes.data() + abs + 4), len);
      } else if constexpr (Binary<MT>) {
        std::uint8_t buf[4];
        session->source().peek(abs, buf, 4);
        std::uint32_t len = flat_load_le<std::uint32_t>(buf);
        return std::span<const std::uint8_t>(session->bytes().data() + abs + 4, len);
      } else if constexpr (ContainerButNotString<MT> && !Binary<MT>) {
        using Elem = typename MT::value_type;
        return flat_vector_view<Elem>(session, abs, abs_end);
      } else if constexpr (Map<MT>) {
        return flat_map_view<typename MT::key_type, typename MT::mapped_type>(session, abs, abs_end);
      } else if constexpr (UserDefinedType<MT> && !String<MT> && !Pointer<MT> && !SimpleException<MT>) {
        return flat_view<MT>(session, abs, abs_end);
      } else if constexpr (Boolean<MT>) {
        std::uint8_t b = 0;
        session->source().peek(abs, &b, 1);
        return static_cast<MT>(b != 0);
      } else if constexpr (Enum<MT>) {
        std::uint8_t buf[8];
        session->source().peek(abs, buf, 8);
        return static_cast<MT>(flat_load_le<std::uint64_t>(buf));
      } else {
        constexpr std::size_t n = flat_fixed_size<MT>();
        std::uint8_t buf[8]{};
        session->source().peek(abs, buf, n);
        return flat_load_le<MT>(buf);
      }
    }

  } // namespace flat_detail

  template <class K, class V>
  class flat_map_view {
    public:
      flat_map_view(std::shared_ptr<flat_session> session, std::size_t start, std::size_t end)
        : session_(std::move(session)), start_(start), end_(end) {
        std::uint8_t nbuf[4];
        session_->source().peek(start_, nbuf, 4);
        size_ = flat_load_le<std::uint32_t>(nbuf);
        if(size_ == 0) {
          keys_base_ = start_ + 4;
          values_base_ = keys_base_;
          return;
        }
        std::uint8_t format_k = 0;
        session_->source().peek(start_ + 4, &format_k, 1);
        const std::size_t wk = flat_unpack_width(format_k);
        key_offs_.resize(size_ + 1);
        for(std::size_t i = 0; i < size_ + 1; ++i) {
          std::uint8_t buf[4]{};
          session_->source().peek(start_ + 5 + i * wk, buf, wk);
          key_offs_[i] = flat_load_cp(buf, wk);
        }
        keys_base_ = start_ + 5 + (size_ + 1) * wk;
        values_base_ = keys_base_ + key_offs_[size_];

        std::uint8_t format_v = 0;
        session_->source().peek(end_ - 1, &format_v, 1);
        val_width_ = flat_unpack_width(format_v);
        const std::size_t trailer = flat_trailer_size(size_, val_width_);
        const std::size_t trailer_start = end_ - trailer;
        std::uint8_t format_v0 = 0;
        session_->source().peek(trailer_start, &format_v0, 1);
        if(format_v0 != format_v) {
          throw std::runtime_error("flat_map_view value trailer format mismatch");
        }
        val_offs_.resize(size_);
        for(std::size_t i = 0; i < size_; ++i) {
          std::uint8_t buf[4]{};
          session_->source().peek(trailer_start + 1 + i * val_width_, buf, val_width_);
          val_offs_[i] = flat_load_cp(buf, val_width_);
        }
      }

      std::size_t size() const { return size_; }

      bool contains(const K& key) const {
        return find_index(key).has_value();
      }

      auto find(const K& key) const {
        auto idx = find_index(key);
        if(!idx) {
          throw std::runtime_error("flat_map_view::find key not found");
        }
        return at_index(*idx);
      }

      auto operator[](const K& key) const {
        return find(key);
      }

      auto at_index(std::size_t i) const {
        if(i >= size_) {
          throw std::out_of_range("flat_map_view index");
        }
        const std::size_t abs = values_base_ + val_offs_[i];
        const std::size_t abs_end = value_end(i);
        return flat_detail::read_value_at<V>(session_, abs, abs_end);
      }

    private:
      std::shared_ptr<flat_session> session_;
      std::size_t start_ = 0;
      std::size_t end_ = 0;
      std::size_t size_ = 0;
      std::size_t keys_base_ = 0;
      std::size_t values_base_ = 0;
      std::size_t val_width_ = 1;
      std::vector<std::uint32_t> key_offs_;
      std::vector<std::uint32_t> val_offs_;

      std::size_t value_end(std::size_t i) const {
        if(i + 1 < size_) {
          return values_base_ + val_offs_[i + 1];
        }
        return end_ - flat_trailer_size(size_, val_width_);
      }

      std::optional<std::size_t> find_index(const K& key) const {
        if(size_ == 0) {
          return std::nullopt;
        }
        std::vector<std::uint8_t> needle;
        flat_bytes_writer w(needle);
        serialize(w, key);
        const auto& bytes = session_->bytes();
        std::size_t lo = 0;
        std::size_t hi = size_;
        while(lo < hi) {
          const std::size_t mid = lo + (hi - lo) / 2;
          const std::uint32_t off = key_offs_[mid];
          const std::uint32_t len = key_offs_[mid + 1] - off;
          const std::size_t cmp_len = std::min<std::size_t>(len, needle.size());
          const int cmp = std::memcmp(bytes.data() + keys_base_ + off, needle.data(), cmp_len);
          if(cmp < 0 || (cmp == 0 && len < needle.size())) {
            lo = mid + 1;
          } else if(cmp > 0 || (cmp == 0 && len > needle.size())) {
            hi = mid;
          } else {
            return mid;
          }
        }
        return std::nullopt;
      }
  };

  template <class T>
  class flat_view {
    public:
      flat_view(std::shared_ptr<flat_session> session, std::size_t start, std::size_t end)
        : session_(std::move(session)), start_(start), end_(end) {
        if(end_ > start_) {
          reload_cps();
        }
      }

      std::size_t start() const { return start_; }
      std::size_t end() const { return end_; }
      std::shared_ptr<flat_session> session() const { return session_; }

      void rebind(std::size_t start, std::size_t end) {
        start_ = start;
        end_ = end;
        reload_cps();
      }

      template <std::meta::info Wanted>
      auto get() const {
        using MT = typename [:std::meta::type_of(Wanted):];
        std::size_t pos = 0;
        std::size_t c = 0;
        bool found = false;
        std::size_t field_start = 0;
        std::optional<std::uint32_t> field_cp;

        walk_base<T>([&]<auto dm> {
          if(found) return;
          if constexpr(!is_member_ignored(dm)) {
            using M = typename [:std::meta::type_of(dm):];
            if constexpr (dm == Wanted) {
              field_start = pos;
              found = true;
              if constexpr (is_flat_variable_v<M>) {
                field_cp = cps_.at(c);
              }
            } else if constexpr (is_flat_variable_v<M>) {
              pos = cps_.at(c++);
            } else {
              pos += flat_fixed_size<M>();
            }
          }
        });

        if(!found) {
          throw std::runtime_error("flat_view::get member not found");
        }
        return read_field_at<MT>(field_start, field_cp);
      }

      template <flat_fixed_string Name>
      static consteval std::meta::info member_named() {
        for(std::meta::info dm :
            std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked())) {
          if(is_member_ignored(dm)) continue;
          if(!std::meta::has_identifier(dm)) continue;
          if(std::meta::identifier_of(dm) == Name.view()) {
            return dm;
          }
        }
        return ^^void;
      }

      template <flat_fixed_string Name>
      auto get() const {
        constexpr auto dm = member_named<Name>();
        return get<dm>();
      }

      // Used by flat_root_shared to bind pointer slots
      template <class PoolT>
      void bind_pointers(std::vector<std::shared_ptr<flat_view<PoolT>>>& pool, std::size_t& pool_i) {
        walk_base<T>([&]<auto dm> {
          if constexpr(!is_member_ignored(dm)) {
            using MT = typename [:std::meta::type_of(dm):];
            if constexpr (SharedPointer<MT>) {
              using U = typename MT::element_type;
              static_assert(std::is_same_v<U, PoolT>);
              std::size_t abs = field_abs_offset<dm>();
              std::uint8_t buf[4];
              session_->source().peek(abs, buf, 4);
              std::int32_t delta = flat_load_le<std::int32_t>(buf);
              auto target = session_->template resolve_delta<PoolT>(delta, pool, pool_i);
              session_->bind_pointer_at(abs, target);
            } else if constexpr (ContainerButNotString<MT> && !Binary<MT>) {
              using E = typename MT::value_type;
              if constexpr (SharedPointer<E>) {
                using U = typename E::element_type;
                static_assert(std::is_same_v<U, PoolT>);
                auto vec_info = field_abs_and_end<dm>();
                std::uint8_t lbuf[4];
                session_->source().peek(vec_info.first, lbuf, 4);
                std::uint32_t n = flat_load_le<std::uint32_t>(lbuf);
                std::size_t elem = vec_info.first + 4;
                for(std::uint32_t i = 0; i < n; ++i) {
                  std::uint8_t buf[4];
                  session_->source().peek(elem + i * 4, buf, 4);
                  std::int32_t delta = flat_load_le<std::int32_t>(buf);
                  auto target = session_->template resolve_delta<PoolT>(delta, pool, pool_i);
                  session_->bind_pointer_at(elem + i * 4, target);
                }
              }
            }
          }
        });
      }

    private:
      std::shared_ptr<flat_session> session_;
      std::size_t start_ = 0;
      std::size_t end_ = 0;
      std::vector<std::uint32_t> cps_;

      void reload_cps() {
        cps_ = flat_detail::load_checkpoints(
          session_->source(), start_, end_, flat_detail::variable_count<T>());
      }

      template <class B, class F>
      void walk_base(F&& visit) const {
        [:expand(std::meta::bases_of(^^B, std::meta::access_context::unchecked())):]
          >> [&]<std::meta::info base> {
            walk_base<typename [:std::meta::type_of(base):]>(visit);
          };
        [:expand(std::meta::nonstatic_data_members_of(^^B, std::meta::access_context::unchecked())):]
          >> [&]<auto dm> {
            visit.template operator()<dm>();
          };
      }

      template <std::meta::info Wanted>
      std::size_t field_abs_offset() const {
        std::size_t pos = 0;
        std::size_t c = 0;
        std::size_t field_start = 0;
        bool found = false;
        walk_base<T>([&]<auto dm> {
          if(found) return;
          if constexpr(!is_member_ignored(dm)) {
            using M = typename [:std::meta::type_of(dm):];
            if constexpr (dm == Wanted) {
              field_start = pos;
              found = true;
            } else if constexpr (is_flat_variable_v<M>) {
              pos = cps_.at(c++);
            } else {
              pos += flat_fixed_size<M>();
            }
          }
        });
        return start_ + field_start;
      }

      template <std::meta::info Wanted>
      std::pair<std::size_t, std::size_t> field_abs_and_end() const {
        std::size_t pos = 0;
        std::size_t c = 0;
        std::size_t field_start = 0;
        std::optional<std::uint32_t> field_cp;
        bool found = false;
        walk_base<T>([&]<auto dm> {
          if(found) return;
          if constexpr(!is_member_ignored(dm)) {
            using M = typename [:std::meta::type_of(dm):];
            if constexpr (dm == Wanted) {
              field_start = pos;
              found = true;
              if constexpr (is_flat_variable_v<M>) {
                field_cp = cps_.at(c);
              }
            } else if constexpr (is_flat_variable_v<M>) {
              pos = cps_.at(c++);
            } else {
              pos += flat_fixed_size<M>();
            }
          }
        });
        std::size_t abs = start_ + field_start;
        std::size_t abs_end = field_cp ? (start_ + *field_cp) : end_;
        return {abs, abs_end};
      }

      template <class MT>
      auto read_field_at(std::size_t rel_pos, std::optional<std::uint32_t> cp_after) const {
        const std::size_t abs = start_ + rel_pos;
        std::size_t abs_end = cp_after ? (start_ + *cp_after) : end_;
        return flat_detail::read_value_at<MT>(session_, abs, abs_end);
      }
  };

  template <class T>
  flat_view<T> flat_root(std::shared_ptr<std::vector<std::uint8_t>> storage) {
    auto session = std::make_shared<flat_session>(std::move(storage));
    return flat_view<T>(session, 0, session->bytes().size());
  }

  template <class T>
  flat_view<T> flat_root(std::vector<std::uint8_t> bytes) {
    return flat_root<T>(std::make_shared<std::vector<std::uint8_t>>(std::move(bytes)));
  }

  template <class T>
  flat_view<T> flat_root(const std::string& bytes) {
    auto storage = std::make_shared<std::vector<std::uint8_t>>(bytes.begin(), bytes.end());
    return flat_root<T>(std::move(storage));
  }

  template <class T>
  std::shared_ptr<flat_view<T>> flat_root_shared(std::shared_ptr<std::vector<std::uint8_t>> storage) {
    auto session = std::make_shared<flat_session>(storage);
    if(storage->size() < 4) {
      throw std::runtime_error("flat_root_shared: buffer too small");
    }
    std::int32_t root_delta = flat_load_le<std::int32_t>(storage->data());
    if(root_delta == 0) {
      return nullptr;
    }
    if(root_delta < 0) {
      throw std::runtime_error("flat_root_shared: unexpected negative root delta");
    }

    std::vector<std::shared_ptr<flat_view<T>>> pool;
    std::size_t cursor = 4;
    while(cursor + 4 <= storage->size()) {
      cursor += 4;
      if(cursor >= storage->size()) break;
      std::size_t frame_start = cursor;
      std::size_t frame_size = flat_detail::measure_frame<T>(
        std::span<const std::uint8_t>(storage->data() + frame_start, storage->size() - frame_start));
      std::size_t frame_end = frame_start + frame_size;
      pool.push_back(std::shared_ptr<flat_view<T>>(new flat_view<T>(session, frame_start, frame_end)));
      cursor = frame_end;
    }
    if(pool.empty()) {
      throw std::runtime_error("flat_root_shared: no frames");
    }

    std::size_t pool_i = 0;
    std::size_t root_i = static_cast<std::size_t>(root_delta - 1);
    while(session->later_slots().size() <= root_i) {
      session->later_slots().push_back(nullptr);
    }
    if(!session->later_slots()[root_i]) {
      session->later_slots()[root_i] = pool[pool_i++];
    }
    auto root = std::static_pointer_cast<flat_view<T>>(session->later_slots()[root_i]);

    while(!session->later_slots().empty()) {
      auto cur = std::static_pointer_cast<flat_view<T>>(session->later_slots().front());
      cur->bind_pointers(pool, pool_i);
      session->later_slots().erase(session->later_slots().begin());
      session->resolved().push_back(cur);
    }

    return root;
  }

} // namespace serialization
