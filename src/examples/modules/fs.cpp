#include <utils/module.hpp>
#include "../structures/fs.hpp"

waos::fs fs_api;

void register_all() {
  register_rpc(fs_api);
  waos::open_flag openFlag = waos::open_flag::NONE;
  register_rpc(openFlag);
  waos::seek_whence seekWhence = waos::seek_whence::SET;
  register_rpc(seekWhence);
  waos::file_info fileInfo;
  register_rpc(fileInfo);
  waos::dir_entry dirEntry;
  register_rpc(dirEntry);
  waos::fs_error fsError;
  register_rpc(fsError);
  waos::fs_not_found fsNotFound;
  register_rpc(fsNotFound);
  waos::fs_already_exists fsAlreadyExists;
  register_rpc(fsAlreadyExists);
  waos::fs_not_a_directory fsNotADirectory;
  register_rpc(fsNotADirectory);
  waos::fs_is_a_directory fsIsADirectory;
  register_rpc(fsIsADirectory);
  waos::fs_invalid_path fsInvalidPath;
  register_rpc(fsInvalidPath);
  waos::fs_invalid_handle fsInvalidHandle;
  register_rpc(fsInvalidHandle);
  waos::fs_invalid_flag fsInvalidFlag;
  register_rpc(fsInvalidFlag);
  waos::fs_io_error fsIoError;
  register_rpc(fsIoError);
}
