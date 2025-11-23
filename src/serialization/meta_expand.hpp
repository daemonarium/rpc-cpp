#pragma once

#include <vector>
#include <meta>

namespace serialization {
// workaround from
  // https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2024/p2996r3.html#back-and-forth
  // for missing expansion statements
  namespace __impl {
    template<auto... vals>
    struct replicator_type {
      template<typename F>
        constexpr void operator>>(F body) const {
          (body.template operator()<vals>(), ...);
        }
    };

    template<auto... vals>
    replicator_type<vals...> replicator = {};
  } // namespace __impl

  template <typename R> consteval auto expand(R range) {
    std::vector<std::meta::info> args;
    for (auto r : range) {
      args.push_back(std::meta::reflect_constant(r));
    }
    return substitute(^^__impl::replicator, args);
  }
  // end of workaround
}