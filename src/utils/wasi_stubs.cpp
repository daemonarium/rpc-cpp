#include <stdint.h>

extern "C" {

using wasi_fd_t = uint32_t;
using wasi_size_t = uint32_t;
using wasi_errno_t = uint32_t;
using wasi_filesize_t = uint64_t;
using wasi_filedelta_t = int64_t;
using wasi_whence_t = uint16_t;

// structy są „spłaszczone” do i32, więc sygnatury w Wasm i tak będą i32
struct wasi_iovec {
  uint32_t buf;
  uint32_t buf_len;
};

// fd_write(fd, iovs, iovs_len, nwritten)
wasi_errno_t __wasi_fd_write(
  wasi_fd_t fd,
  const wasi_iovec* iovs,
  wasi_size_t iovs_len,
  wasi_size_t* nwritten
) {
  if (nwritten) *nwritten = 0;
  return 0;  // 0 = __WASI_ESUCCESS
}

// fd_read(fd, iovs, iovs_len, nread)
wasi_errno_t __wasi_fd_read(
  wasi_fd_t fd,
  const wasi_iovec* iovs,
  wasi_size_t iovs_len,
  wasi_size_t* nread
) {
  if (nread) *nread = 0;
  return 0;
}

wasi_errno_t __wasi_fd_seek(
  wasi_fd_t fd,
  wasi_filedelta_t offset,
  wasi_whence_t whence,
  wasi_filesize_t* newoffset
) {
  if (newoffset) *newoffset = 0;
  return 0;
}


wasi_errno_t __wasi_fd_close(wasi_fd_t) {
  return 0;
}

// environ_sizes_get(environ_count, environ_buf_size)
wasi_errno_t __wasi_environ_sizes_get(
  wasi_size_t* count,
  wasi_size_t* buf_size
) {
  if (count) *count = 0;
  if (buf_size) *buf_size = 0;
  return 0;
}

// environ_get(environ, environ_buf)
wasi_errno_t __wasi_environ_get(
  uint32_t* /*environ*/,
  char* /*environ_buf*/
) {
  return 0;
}

void emscripten_notify_memory_growth(int) {    
}


}
