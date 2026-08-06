#include <utils/module.hpp>
#include "../structures/debug_store.hpp"

waos::debug_store debug_store_api;
waos::debug_session debug_session_api;

namespace waos {
  std::shared_ptr<serialization::promise<begin_session_result>> debug_store::begin(begin_session_request) {
    return serialization::promise<begin_session_result>::resolved(begin_session_result{});
  }
  std::shared_ptr<serialization::promise<void>> debug_session::set_labels(labels) {
    return serialization::promise<void>::resolved();
  }
  std::shared_ptr<serialization::promise<void>> debug_session::sync() {
    return serialization::promise<void>::resolved();
  }
  std::shared_ptr<serialization::promise<void>> debug_session::seal() {
    return serialization::promise<void>::resolved();
  }
  std::shared_ptr<serialization::promise<void>> debug_session::fail(std::string) {
    return serialization::promise<void>::resolved();
  }
}

void register_all() {
  register_rpc(debug_store_api);
  register_rpc(debug_session_api);

  waos::begin_session_request beginReq;
  register_rpc(beginReq);
  waos::begin_session_result beginRes;
  register_rpc(beginRes);

  waos::debug_snapshot_event snapEv;
  register_rpc(snapEv);
  waos::debug_io_event ioEv;
  register_rpc(ioEv);
  waos::debug_logs_event logsEv;
  register_rpc(logsEv);
  waos::debug_spans_event spansEv;
  register_rpc(spansEv);
  waos::debug_metrics_event metricsEv;
  register_rpc(metricsEv);
  waos::debug_bindings_event bindingsEv;
  register_rpc(bindingsEv);
  waos::debug_registry_event registryEv;
  register_rpc(registryEv);

  waos::debug_error dbgErr;
  register_rpc(dbgErr);
  waos::debug_invalid_argument dbgInv;
  register_rpc(dbgInv);
  waos::debug_conflict dbgConf;
  register_rpc(dbgConf);
  waos::debug_closed dbgClosed;
  register_rpc(dbgClosed);
  waos::debug_io_error dbgIo;
  register_rpc(dbgIo);
}
