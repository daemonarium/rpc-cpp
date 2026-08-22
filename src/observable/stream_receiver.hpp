#pragma once

#include "examples/structures/stream_protocol.hpp"

namespace waos::observable {

  // Wire-facing sink for one stream_id. StreamMultiplexer demuxes incoming
  // stream_* messages onto a StreamReceiver. RemoteObservable implements this.
  //
  // onStreamOpened is a no-op by default: the current wire stream_opened
  // message has no stream_id (it is used as a socket-open ack) so the mux
  // broadcasts it to every registered receiver.
  class StreamReceiver {
  public:
    virtual ~StreamReceiver() = default;
    virtual void onStreamOpened(waos::stream_opened&) {}
    virtual void onStreamItem(waos::stream_item&) = 0;
    virtual void onStreamDone(waos::stream_done&) = 0;
    virtual void onStreamReset(waos::stream_reset&) = 0;
    virtual void onStreamError(waos::stream_error&) = 0;
  };

}
