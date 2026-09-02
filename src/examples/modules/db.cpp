#include <utils/module.hpp>
#include "../structures/db.hpp"

waos::db db_api;

void register_all() {
  register_rpc(db_api);

  waos::db_write_mode writeMode = waos::db_write_mode::durable;
  register_rpc(writeMode);
  waos::db_bound_mode boundMode = waos::db_bound_mode::none;
  register_rpc(boundMode);

  waos::db_get_result getResult;
  register_rpc(getResult);
  waos::db_write_options writeOpts;
  register_rpc(writeOpts);
  waos::db_write_entry writeEntry;
  register_rpc(writeEntry);
  waos::db_scan_query scanQuery;
  register_rpc(scanQuery);

  waos::channel_handle channelHandle;
  register_rpc(channelHandle);
  waos::stream_handle streamHandle;
  register_rpc(streamHandle);

  waos::socket_opened socketOpened;
  register_rpc(socketOpened);
  waos::db_visible_event visibleEv;
  register_rpc(visibleEv);
  waos::db_durable_event durableEv;
  register_rpc(durableEv);
  waos::stream_item scanItem;
  register_rpc(scanItem);
  waos::stream_key scanKey;
  register_rpc(scanKey);
  waos::stream_done scanDone;
  register_rpc(scanDone);
  waos::stream_error scanError;
  register_rpc(scanError);
  waos::stream_reset scanReset;
  register_rpc(scanReset);
  waos::stream_opened streamOpened;
  register_rpc(streamOpened);

  waos::db_error dbError;
  register_rpc(dbError);
  waos::db_invalid_argument dbInvalidArgument;
  register_rpc(dbInvalidArgument);
  waos::db_closed dbClosed;
  register_rpc(dbClosed);
  waos::db_io_error dbIoError;
  register_rpc(dbIoError);
}
