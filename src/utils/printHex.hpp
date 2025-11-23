#pragma once

#include <iostream>
#include <iomanip>


void printHex(const void* data, size_t size) {
  // pretty print hex in lines of 16 bytes, with indexes spaces and header
  const unsigned char* bytes = static_cast<const unsigned char*>(data);
  for (size_t i = 0; i < size; i += 16) {
    std::cout << std::hex << std::setw(8) << std::setfill('0') << i << " | ";
    for (size_t j = 0; j < 16; j++) {
      if (i + j < size) {
        std::cout << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(bytes[i + j]) << " ";
      } else {
        std::cout << "   ";
      }
    }
    std::cout << std::endl;
  }
  std::cout << std::endl;
  std::cout << std::dec;
}
