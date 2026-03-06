#include <iostream>
#include <serialization/promise.hpp>

std::shared_ptr<serialization::promise<void>> test_promise_exception() {
  auto p = std::make_shared<serialization::promise<void>>();
  p->reject(std::make_exception_ptr(std::runtime_error("test exception")));

  try {
    co_await p;
    std::cout << "no exception" << std::endl;
  } catch (const std::exception& e) {
    std::cout << "exception: " << e.what() << std::endl;
  }
}

int main(int argc, char** argv) {
  test_promise_exception();
  std::cout << "test done" << std::endl;

  return 0;
}