#pragma once

#include <string>

class DbgFail {
  public:
    static std::string ok() {
      return "ok";
    }

    // Unconditional trap. Replay surfaces this as a runtime error.
    // The kernel host catches it inside handleData and does not emit
    // process_error_event, so a recording of this call has no error frame.
    static void boom() {
      __builtin_trap();
    }
};
