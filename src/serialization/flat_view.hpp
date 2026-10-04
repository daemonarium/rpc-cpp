#pragma once

#include "annotations.hpp"
#include "deserialize.hpp"
#include "flat_encoding.hpp"
#include "flat_bytes.hpp"
#include "flat_bytes_writer.hpp"
#include "flat_source.hpp"
#include "flat_buffer_reader.hpp"
#include "flat_stream_reader.hpp"
#include "meta_expand.hpp"
#include "serialize.hpp"
#include "type_concepts.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <typeindex>
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

  class flat_session;

  struct flat_pool_slot;

  // One frame in an in-place graph. view is a flat_view<Actual>.
  // bind runs that view's pointer walk; it is type-erased because sub-frames
  // are not all flat_view<Root>.
  struct flat_pool_slot {
    std::shared_ptr<void> view;
    std::type_index type = typeid(void);
    std::function<void(const std::vector<flat_pool_slot>&, std::size_t)> bind;
  };

  // Type-erased flat_view reached through a shared_ptr field. get<V>() checks
  // the dynamic type recorded when the pool was built (exact match).
  class flat_polymorphic_view {
    public:
      flat_polymorphic_view() : type_(typeid(void)) {}

      flat_polymorphic_view(std::shared_ptr<void> view, std::type_index type)
        : view_(std::move(view)), type_(type) {}

      explicit operator bool() const { return static_cast<bool>(view_); }
      bool operator==(std::nullptr_t) const { return !view_; }
      bool operator!=(std::nullptr_t) const { return static_cast<bool>(view_); }

      std::type_index get_type() const { return type_; }

      template <class V>
      bool is_type() const {
        return static_cast<bool>(view_) && type_ == std::type_index(typeid(V));
      }

      template <class V>
      std::shared_ptr<flat_view<V>> get() const {
        if(!view_) {
          return nullptr;
        }
        if(type_ != std::type_index(typeid(V))) {
          throw std::runtime_error("flat_polymorphic_view: type mismatch");
        }
        return std::static_pointer_cast<flat_view<V>>(view_);
      }

    private:
      std::shared_ptr<void> view_;
      std::type_index type_;
  };

  // Builds flat_view<Actual> for a wire type index. Register in the same
  // order as register_serialize_typed_function / register_dynamic_type.
  class flat_view_factory_registry {
    public:
      template <class T>
      void register_type() {
        auto type = std::type_index(typeid(T));
        if(indexes_.find(type) != indexes_.end()) {
          throw std::runtime_error("flat_view factory already registered");
        }
        indexes_[type] = factories_.size();
        types_.push_back(type);
        factories_.push_back([](std::shared_ptr<flat_session> session,
            std::size_t start, std::size_t end) {
          auto view = std::shared_ptr<flat_view<T>>(
            new flat_view<T>(std::move(session), start, end));
          flat_pool_slot slot;
          slot.view = view;
          slot.type = std::type_index(typeid(T));
          slot.bind = [view](const std::vector<flat_pool_slot>& pool, std::size_t cur) {
            view->bind_pointers(pool, cur);
          };
          return slot;
        });
      }

      flat_pool_slot make(std::uint32_t index, std::shared_ptr<flat_session> session,
          std::size_t start, std::size_t end) const {
        if(index >= factories_.size()) {
          throw std::runtime_error("flat_view factory index out of range");
        }
        return factories_[index](std::move(session), start, end);
      }

      std::type_index type_of(std::uint32_t index) const {
        if(index >= types_.size()) {
          throw std::runtime_error("flat_view factory index out of range");
        }
        return types_[index];
      }

      std::size_t size() const { return factories_.size(); }

    private:
      using Factory = std::function<flat_pool_slot(
        std::shared_ptr<flat_session>, std::size_t, std::size_t)>;
      std::vector<Factory> factories_;
      std::vector<std::type_index> types_;
      std::unordered_map<std::type_index, std::size_t> indexes_;
  };

  template <class E>
  class flat_vector_view;

  template <class K, class V>
  class flat_map_view;

  class flat_session : public std::enable_shared_from_this<flat_session> {
    public:
      explicit flat_session(std::shared_ptr<std::vector<std::uint8_t>> storage)
        : storage_(std::move(storage)), kind_(Kind::Span),
          span_(*storage_), logical_len_(span_.size()) {}

      // Non-owning view into contiguous external memory (e.g. circular ring region).
      // Caller must keep memory valid for the session lifetime (until skip_read).
      explicit flat_session(std::span<const std::uint8_t> external)
        : storage_(nullptr), kind_(Kind::Span), span_(external),
          logical_len_(external.size()) {}

      // Non-owning window into a circular buffer [0, logical_len) in peek space.
      // Wrap is OK — string/binary materialize per field via flat_string / flat_bytes.
      flat_session(struct circular_buffer* buffer, std::size_t logical_len)
        : storage_(nullptr), kind_(Kind::Circular), circ_(buffer),
          logical_len_(logical_len) {
        if(!buffer) {
          throw std::runtime_error("flat_session: null circular buffer");
        }
        if(logical_len > circular_buffer_readable_bytes(buffer)) {
          throw std::runtime_error("flat_session: logical_len exceeds readable");
        }
      }

      std::size_t size() const { return logical_len_; }

      std::size_t peek(std::size_t offset, void* dest, std::size_t n) const {
        if(offset >= logical_len_) {
          return 0;
        }
        const std::size_t available = logical_len_ - offset;
        const std::size_t to_copy = n < available ? n : available;
        if(kind_ == Kind::Circular) {
          return circ_.peek(offset, dest, to_copy);
        }
        return span_.peek(offset, dest, to_copy);
      }

      bool region_contiguous(std::size_t offset, std::size_t len) const {
        if(offset + len > logical_len_) {
          return false;
        }
        if(kind_ == Kind::Circular) {
          return circ_.region_contiguous(offset, len);
        }
        return span_.region_contiguous(offset, len);
      }

      const std::uint8_t* try_ptr(std::size_t offset, std::size_t len) const {
        if(offset + len > logical_len_) {
          return nullptr;
        }
        if(kind_ == Kind::Circular) {
          return circ_.try_ptr(offset, len);
        }
        return span_.try_ptr(offset, len);
      }

      int get_segments(std::size_t offset, std::size_t len,
          struct circular_buffer_segment out[2]) const {
        if(offset + len > logical_len_ || !out) {
          return 0;
        }
        if(kind_ == Kind::Circular) {
          return circ_.get_segments(offset, len, out);
        }
        const std::uint8_t* p = span_.try_ptr(offset, len);
        if(!p) {
          return 0;
        }
        out[0].ptr = reinterpret_cast<const char*>(p);
        out[0].len = len;
        out[1].ptr = nullptr;
        out[1].len = 0;
        return 1;
      }

      flat_string string_at(std::size_t offset, std::size_t len) const {
        if(const std::uint8_t* p = try_ptr(offset, len)) {
          return flat_string::borrow(reinterpret_cast<const char*>(p), len);
        }
        std::string owned(len, '\0');
        if(len > 0 && peek(offset, owned.data(), len) != len) {
          throw std::runtime_error("flat_session::string_at short read");
        }
        return flat_string::own(std::move(owned));
      }

      flat_bytes bytes_at(std::size_t offset, std::size_t len) const {
        if(const std::uint8_t* p = try_ptr(offset, len)) {
          return flat_bytes::borrow(p, len);
        }
        std::vector<std::uint8_t> owned(len);
        if(len > 0 && peek(offset, owned.data(), len) != len) {
          throw std::runtime_error("flat_session::bytes_at short read");
        }
        return flat_bytes::own(std::move(owned));
      }

      // Owned storage when vector-backed; nullptr for external span / circular.
      const std::shared_ptr<std::vector<std::uint8_t>>& storage() const { return storage_; }

      bool is_circular() const { return kind_ == Kind::Circular; }

      std::vector<flat_pool_slot>& resolved() { return resolved_; }

      void bind_pointer_at(std::size_t abs_offset, std::shared_ptr<void> view, std::type_index type) {
        pointer_at_[abs_offset] = flat_pool_slot{std::move(view), type};
      }

      flat_polymorphic_view pointer_at(std::size_t abs_offset) const {
        auto it = pointer_at_.find(abs_offset);
        if(it == pointer_at_.end() || !it->second.view) {
          return {};
        }
        return flat_polymorphic_view(it->second.view, it->second.type);
      }

      // Resolve a pointer delta to a pool slot. Negative deltas are
      // back-references into resolved_ (frames bound before the current
      // frame). Positive deltas are forward references into the pool, indexed
      // relative to the current frame's position — mirroring the writer's
      // write_later_ queue semantics:
      //   - root (cur==0) is deref-serialized, so it is NOT in write_later_;
      //     delta=+m -> the (m-1)-th child = pool[m].
      //   - sub-frame (cur>=1) is the front of write_later_ during its own
      //     serialization, so delta=+m -> pool[cur + m - 1] (m=1 -> self).
      flat_pool_slot resolve_delta(std::int32_t delta,
          const std::vector<flat_pool_slot>& pool, std::size_t cur) const {
        if(delta == 0) {
          return {};
        }
        if(delta < 0) {
          std::size_t index = static_cast<std::size_t>(
            static_cast<std::ptrdiff_t>(resolved_.size()) + delta);
          return resolved_.at(index);
        }
        std::size_t target = (cur == 0)
          ? static_cast<std::size_t>(delta)
          : cur + static_cast<std::size_t>(delta) - 1;
        if(target >= pool.size()) {
          throw std::runtime_error("flat pointer pool exhausted");
        }
        return pool[target];
      }

    private:
      enum class Kind { Span, Circular };

      std::shared_ptr<std::vector<std::uint8_t>> storage_;
      Kind kind_ = Kind::Span;
      span_flat_source span_;
      circular_flat_source circ_;
      std::size_t logical_len_ = 0;
      std::vector<flat_pool_slot> resolved_;
      std::unordered_map<std::size_t, flat_pool_slot> pointer_at_;
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

    inline std::vector<std::uint32_t> load_checkpoints(const flat_session& session,
        std::size_t start, std::size_t end, std::size_t k) {
      std::vector<std::uint32_t> cps;
      if(k == 0) {
        return cps;
      }
      std::uint8_t format_end = 0;
      session.peek(end - 1, &format_end, 1);
      const std::size_t width = flat_unpack_width(format_end);
      const std::size_t trailer = flat_trailer_size(k, width);
      if(end < start || end - start < trailer) {
        throw std::runtime_error("flat_view frame too small for trailer");
      }
      const std::size_t trailer_start = end - trailer;
      std::uint8_t format = 0;
      session.peek(trailer_start, &format, 1);
      if(format != format_end) {
        throw std::runtime_error("flat_view format mismatch");
      }
      cps.resize(k);
      for(std::size_t i = 0; i < k; ++i) {
        std::uint8_t buf[4]{};
        session.peek(trailer_start + 1 + i * width, buf, width);
        cps[i] = flat_load_cp(buf, width);
      }
      return cps;
    }

  } // namespace flat_detail

  template <class E>
  class flat_vector_view {
    public:
      flat_vector_view(std::shared_ptr<flat_session> session, std::size_t start, std::size_t end)
        : session_(std::move(session)), start_(start), end_(end) {
        std::uint8_t buf[4];
        session_->peek(start_, buf, 4);
        size_ = flat_load_le<std::uint32_t>(buf);
        elem_start_ = start_ + 4;
        if constexpr (is_flat_variable_v<E>) {
          cps_ = flat_detail::load_checkpoints(*session_, start_, end_, size_);
        }
      }

      std::size_t size() const { return size_; }

      auto operator[](std::size_t i) const {
        if(i >= size_) {
          throw std::out_of_range("flat_vector_view index");
        }
        if constexpr (SharedPointer<E>) {
          return session_->pointer_at(elem_start_ + i * 4);
        } else if constexpr (is_flat_variable_v<E>) {
          std::size_t abs_begin = (i == 0) ? elem_start_ : (start_ + cps_[i - 1]);
          std::size_t abs_end = start_ + cps_[i];
          if constexpr (String<E>) {
            std::uint8_t buf[4];
            session_->peek(abs_begin, buf, 4);
            std::uint32_t len = flat_load_le<std::uint32_t>(buf);
            return session_->string_at(abs_begin + 4, len);
          } else {
            return flat_view<E>(session_, abs_begin, abs_end);
          }
        } else {
          constexpr std::size_t stride = flat_fixed_size<E>();
          std::uint8_t buf[8]{};
          session_->peek(elem_start_ + i * stride, buf, stride);
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
        return session->pointer_at(abs);
      } else if constexpr (String<MT>) {
        std::uint8_t buf[4];
        session->peek(abs, buf, 4);
        std::uint32_t len = flat_load_le<std::uint32_t>(buf);
        return session->string_at(abs + 4, len);
      } else if constexpr (Binary<MT>) {
        std::uint8_t buf[4];
        session->peek(abs, buf, 4);
        std::uint32_t len = flat_load_le<std::uint32_t>(buf);
        return session->bytes_at(abs + 4, len);
      } else if constexpr (ContainerButNotString<MT> && !Binary<MT>) {
        using Elem = typename MT::value_type;
        return flat_vector_view<Elem>(session, abs, abs_end);
      } else if constexpr (Map<MT>) {
        return flat_map_view<typename MT::key_type, typename MT::mapped_type>(session, abs, abs_end);
      } else if constexpr (UserDefinedType<MT> && !String<MT> && !Pointer<MT> && !SimpleException<MT>) {
        return flat_view<MT>(session, abs, abs_end);
      } else if constexpr (Boolean<MT>) {
        std::uint8_t b = 0;
        session->peek(abs, &b, 1);
        return static_cast<MT>(b != 0);
      } else if constexpr (Enum<MT>) {
        std::uint8_t buf[8];
        session->peek(abs, buf, 8);
        return static_cast<MT>(flat_load_le<std::uint64_t>(buf));
      } else {
        constexpr std::size_t n = flat_fixed_size<MT>();
        std::uint8_t buf[8]{};
        session->peek(abs, buf, n);
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
        session_->peek(start_, nbuf, 4);
        size_ = flat_load_le<std::uint32_t>(nbuf);
        if(size_ == 0) {
          keys_base_ = start_ + 4;
          values_base_ = keys_base_;
          return;
        }
        std::uint8_t format_k = 0;
        session_->peek(start_ + 4, &format_k, 1);
        const std::size_t wk = flat_unpack_width(format_k);
        key_offs_.resize(size_ + 1);
        for(std::size_t i = 0; i < size_ + 1; ++i) {
          std::uint8_t buf[4]{};
          session_->peek(start_ + 5 + i * wk, buf, wk);
          key_offs_[i] = flat_load_cp(buf, wk);
        }
        keys_base_ = start_ + 5 + (size_ + 1) * wk;
        values_base_ = keys_base_ + key_offs_[size_];

        std::uint8_t format_v = 0;
        session_->peek(end_ - 1, &format_v, 1);
        val_width_ = flat_unpack_width(format_v);
        const std::size_t trailer = flat_trailer_size(size_, val_width_);
        const std::size_t trailer_start = end_ - trailer;
        std::uint8_t format_v0 = 0;
        session_->peek(trailer_start, &format_v0, 1);
        if(format_v0 != format_v) {
          throw std::runtime_error("flat_map_view value trailer format mismatch");
        }
        val_offs_.resize(size_);
        for(std::size_t i = 0; i < size_; ++i) {
          std::uint8_t buf[4]{};
          session_->peek(trailer_start + 1 + i * val_width_, buf, val_width_);
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
        std::size_t lo = 0;
        std::size_t hi = size_;
        while(lo < hi) {
          const std::size_t mid = lo + (hi - lo) / 2;
          const std::uint32_t off = key_offs_[mid];
          const std::uint32_t len = key_offs_[mid + 1] - off;
          const std::size_t cmp_len = std::min<std::size_t>(len, needle.size());
          const int cmp = flat_memcmp_prefix(*session_, keys_base_ + off, len,
            needle.data(), cmp_len);
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

      // Used by flat_root_shared to bind pointer slots. cur is this frame's
      // index in the pool (0 for root), used to resolve forward-reference
      // deltas relative to the current frame.
      void bind_pointers(const std::vector<flat_pool_slot>& pool, std::size_t cur) {
        walk_base<T>([&]<auto dm> {
          if constexpr(!is_member_ignored(dm)) {
            using MT = typename [:std::meta::type_of(dm):];
            if constexpr (SharedPointer<MT>) {
              std::size_t abs = field_abs_offset<dm>();
              std::uint8_t buf[4];
              session_->peek(abs, buf, 4);
              std::int32_t delta = flat_load_le<std::int32_t>(buf);
              auto target = session_->resolve_delta(delta, pool, cur);
              if(target.view) {
                session_->bind_pointer_at(abs, target.view, target.type);
              }
            } else if constexpr (ContainerButNotString<MT> && !Binary<MT>) {
              using E = typename MT::value_type;
              if constexpr (SharedPointer<E>) {
                auto vec_info = field_abs_and_end<dm>();
                std::uint8_t lbuf[4];
                session_->peek(vec_info.first, lbuf, 4);
                std::uint32_t n = flat_load_le<std::uint32_t>(lbuf);
                std::size_t elem = vec_info.first + 4;
                for(std::uint32_t i = 0; i < n; ++i) {
                  std::uint8_t buf[4];
                  session_->peek(elem + i * 4, buf, 4);
                  std::int32_t delta = flat_load_le<std::int32_t>(buf);
                  auto target = session_->resolve_delta(delta, pool, cur);
                  if(target.view) {
                    session_->bind_pointer_at(elem + i * 4, target.view, target.type);
                  }
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
          *session_, start_, end_, flat_detail::variable_count<T>());
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
    return flat_view<T>(session, 0, session->size());
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
  flat_view<T> flat_root(std::span<const std::uint8_t> external) {
    auto session = std::make_shared<flat_session>(external);
    return flat_view<T>(session, 0, session->size());
  }

  namespace flat_detail {

    // Build a flat_view graph over a session by walking per-frame size
    // prefixes. Wire layout starting at body_offset:
    //   [encoded_size_0 = N0+1 4B u32 LE][frame 0 = root T]
    //   [encoded_size_i = Ni+1][type index u32][frame i = sub] ...
    //   [0 sentinel 4B][payload]
    // encoded_size_i = real_size_i + 1 (0 = sentinel). Root has no per-frame
    // type index. Sub-frames do: real_size includes the 4-byte index.
    // factories == nullptr treats every sub-frame as T (homogeneous).
    template <class T>
    std::shared_ptr<flat_view<T>> flat_root_shared_on_session(
        std::shared_ptr<flat_session> session, std::size_t body_offset, std::size_t message_len,
        const flat_view_factory_registry* factories) {
      std::vector<flat_pool_slot> pool;
      std::size_t cursor = body_offset;
      bool root_frame = true;
      while(cursor + 4 <= message_len) {
        std::uint8_t sbuf[4];
        session->peek(cursor, sbuf, 4);
        std::uint32_t encoded_size = flat_load_le<std::uint32_t>(sbuf);
        if(encoded_size == 0) {
          break; // sentinel — frame list ends
        }
        std::uint32_t real_size = encoded_size - 1;
        std::size_t frame_end = cursor + 4 + real_size;
        if(frame_end > message_len) {
          throw std::runtime_error("flat_root_shared: frame exceeds message");
        }
        if(root_frame) {
          auto view = std::shared_ptr<flat_view<T>>(
            new flat_view<T>(session, cursor + 4, frame_end));
          flat_pool_slot slot;
          slot.view = view;
          slot.type = std::type_index(typeid(T));
          slot.bind = [view](const std::vector<flat_pool_slot>& frames, std::size_t cur) {
            view->bind_pointers(frames, cur);
          };
          pool.push_back(std::move(slot));
          root_frame = false;
        } else {
          if(real_size < 4) {
            throw std::runtime_error("flat_root_shared: sub-frame missing type index");
          }
          std::uint8_t ibuf[4];
          session->peek(cursor + 4, ibuf, 4);
          std::uint32_t type_index = flat_load_le<std::uint32_t>(ibuf);
          std::size_t body_start = cursor + 8;
          std::size_t body_end = frame_end;
          if(factories) {
            pool.push_back(factories->make(type_index, session, body_start, body_end));
          } else {
            auto view = std::shared_ptr<flat_view<T>>(
              new flat_view<T>(session, body_start, body_end));
            flat_pool_slot slot;
            slot.view = view;
            slot.type = std::type_index(typeid(T));
            slot.bind = [view](const std::vector<flat_pool_slot>& frames, std::size_t cur) {
              view->bind_pointers(frames, cur);
            };
            pool.push_back(std::move(slot));
            (void)type_index;
          }
        }
        cursor = frame_end;
      }
      if(pool.empty()) {
        return nullptr;
      }

      // Root is frame 0. Sub-frames (pool[1..]) back the deferred pointer
      // deltas. resolved_ must mirror written_pointers_ at each frame's
      // serialization time: the root is pre-registered (mark_written_pointer)
      // BEFORE its body, so push root to resolved_ BEFORE binding it (enables
      // root self-refs, delta=-1). Sub-frames are added to written_pointers_
      // AFTER their body, so push them to resolved_ AFTER binding (so their
      // own back-refs see only prior frames). Forward-reference deltas are
      // resolved relative to the current frame's pool index (cur).
      auto root = std::static_pointer_cast<flat_view<T>>(pool[0].view);
      session->resolved().push_back(pool[0]);
      pool[0].bind(pool, 0);
      for(std::size_t i = 1; i < pool.size(); ++i) {
        if(!pool[i].bind) {
          throw std::runtime_error("flat_root_shared: missing frame binder");
        }
        pool[i].bind(pool, i);
        session->resolved().push_back(pool[i]);
      }

      return root;
    }

    template <class T>
    std::shared_ptr<flat_view<T>> flat_root_shared_on_session(
        std::shared_ptr<flat_session> session, std::size_t body_offset, std::size_t message_len) {
      return flat_root_shared_on_session<T>(session, body_offset, message_len, nullptr);
    }

    // Back-compat overload: body_offset defaults to 0 (no leading type-index prefix).
    template <class T>
    std::shared_ptr<flat_view<T>> flat_root_shared_on_session(
        std::shared_ptr<flat_session> session, std::size_t message_len) {
      return flat_root_shared_on_session<T>(session, 0, message_len, nullptr);
    }

    template <class T>
    std::shared_ptr<flat_view<T>> flat_root_shared_on_session(
        std::shared_ptr<flat_session> session, std::size_t message_len,
        const flat_view_factory_registry& factories) {
      return flat_root_shared_on_session<T>(session, 0, message_len, &factories);
    }

  } // namespace flat_detail

  template <class T>
  std::shared_ptr<flat_view<T>> flat_root_shared(std::shared_ptr<std::vector<std::uint8_t>> storage) {
    auto session = std::make_shared<flat_session>(storage);
    return flat_detail::flat_root_shared_on_session<T>(session, session->size());
  }

  template <class T>
  flat_view<T> flat_root_circular(struct circular_buffer* buffer, std::size_t frame_len) {
    auto session = std::make_shared<flat_session>(buffer, frame_len);
    return flat_view<T>(session, 0, frame_len);
  }

  template <class T>
  std::shared_ptr<flat_view<T>> flat_root_shared_circular(
      struct circular_buffer* buffer, std::size_t message_len) {
    auto session = std::make_shared<flat_session>(buffer, message_len);
    return flat_detail::flat_root_shared_on_session<T>(session, message_len);
  }

} // namespace serialization
