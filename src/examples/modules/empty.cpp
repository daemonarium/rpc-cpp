#include <cstdint>
#include <cstddef>

extern "C" size_t rpc_description(char** buffer) {
  return 0;
}

extern "C" __attribute__((import_module("env"), import_name("send_message")))
void send_message(std::uint32_t channel, char* buffer, size_t size);

extern "C" void handle_message(std::uint32_t channel, char* buffer, size_t size) {
}

extern "C" void start() {
}
