/* Stubs for socket/WASM functions when building native tests (no WASM host). */

void socket_flush(int socket) {
  (void)socket;
}

void waos_socket_flush(unsigned int socket) {
  (void)socket;
}

void waos_socket_input_drain(unsigned int socket) {
  (void)socket;
}
