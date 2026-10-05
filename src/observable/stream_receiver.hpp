#pragma once

#include "examples/structures/stream_protocol.hpp"

namespace waos::observable {

  // Wire-facing sink for one stream_id. StreamMultiplexer demuxes incoming
  // stream_* messages onto a StreamReceiver. RemoteObservable implements this.
  //
  // onStreamOpened is optional: the muxer owns cancel. RemoteObservable
  // leaves it as a no-op.
  class StreamReceiver {
  public:
    virtual ~StreamReceiver() = default;
    virtual void onStreamOpened(waos::stream_opened&) {}
    virtual void onStreamItem(waos::stream_item&) = 0;
    virtual void onStreamKey(waos::stream_key& m) {
      waos::stream_item item;
      item.stream_id = m.stream_id;
      item.key = std::move(m.key);
      item.value = {0};
      item.timestamp = m.timestamp;
      onStreamItem(item);
    }
    virtual void onStreamDone(waos::stream_done&) = 0;
    virtual void onStreamReset(waos::stream_reset&) = 0;
    virtual void onStreamError(waos::stream_error&) = 0;
  };

}
