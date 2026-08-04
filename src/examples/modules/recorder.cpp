#include <utils/module.hpp>
#include "../structures/recorder.hpp"

waos::recorder recorder_api;

namespace waos {
  std::shared_ptr<serialization::promise<start_recording_result>> recorder::start_recording(start_recording_request) {
    return serialization::promise<start_recording_result>::resolved(start_recording_result{});
  }
  std::shared_ptr<serialization::promise<void>> recorder::stop_recording(std::uint64_t) {
    return serialization::promise<void>::resolved();
  }
  std::shared_ptr<serialization::promise<void>> recorder::dump(std::uint64_t) {
    return serialization::promise<void>::resolved();
  }
  std::shared_ptr<serialization::promise<void>> recorder::rotate(std::uint64_t) {
    return serialization::promise<void>::resolved();
  }
}

void register_all() {
  register_rpc(recorder_api);

  waos::record_config cfg;
  register_rpc(cfg);
  waos::start_recording_request startReq;
  register_rpc(startReq);
  waos::start_recording_result startRes;
  register_rpc(startRes);

  waos::recorder_error e1;
  register_rpc(e1);
  waos::recorder_invalid_argument e2;
  register_rpc(e2);
  waos::recorder_not_found e3;
  register_rpc(e3);
  waos::recorder_conflict e4;
  register_rpc(e4);
  waos::recorder_closed e5;
  register_rpc(e5);
}
