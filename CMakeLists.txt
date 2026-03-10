cmake_minimum_required(VERSION 3.17)
project(cppSerialization)

set(CMAKE_CXX_STANDARD 26)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)
FetchContent_Declare(ut
  GIT_REPOSITORY https://github.com/boost-ext/ut.git
  GIT_TAG v2.0.0
)
FetchContent_MakeAvailable(ut)

include_directories(src)
aux_source_directory(src/serialization SERIALIZATION_FILES)
aux_source_directory(src/io IO_FILES)
aux_source_directory(src/socket SOCKET_FILES)

set(WASI_STUBS_FILES ${CMAKE_SOURCE_DIR}/cpp-rpc/src/utils/wasi_stubs.cpp)
set(MODULE_FILES ${CMAKE_SOURCE_DIR}/cpp-rpc/src/utils/module.cpp)

file(GLOB EXAMPLE_FILES "src/examples/*.cpp")

foreach(EXAMPLE_FILE ${EXAMPLE_FILES})
  get_filename_component(EXAMPLE_NAME ${EXAMPLE_FILE} NAME_WE)
  add_executable(${EXAMPLE_NAME} ${EXAMPLE_FILE} ${SERIALIZATION_FILES} ${IO_FILES})
  if(WASM)
    target_compile_options(${EXAMPLE_NAME} PRIVATE
      -fwasm-exceptions
    )
    target_link_options(${EXAMPLE_NAME} PRIVATE
      -fwasm-exceptions
    )
  endif()
  target_include_directories(${EXAMPLE_NAME} PRIVATE include src)
  set_target_properties(${EXAMPLE_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/examples
  )
endforeach()


file(GLOB EXAMPLE_MODULES_FILES "src/examples/modules/*.cpp")

if(WASM)
  foreach(EXAMPLE_MODULE_FILE ${EXAMPLE_MODULES_FILES})
    get_filename_component(EXAMPLE_MODULE_NAME ${EXAMPLE_MODULE_FILE} NAME_WE)
    
    add_executable(${EXAMPLE_MODULE_NAME} ${EXAMPLE_MODULE_FILE} ${SERIALIZATION_FILES} ${IO_FILES} ${SOCKET_FILES}
                   ${WASI_STUBS_FILES} ${MODULE_FILES})
    target_compile_options(${EXAMPLE_MODULE_NAME} PRIVATE
      -fwasm-exceptions
    )
    target_link_options(${EXAMPLE_MODULE_NAME} PRIVATE
      -s STANDALONE_WASM=1
      -fwasm-exceptions
      -mbulk-memory
      -Wl,--import-memory      
      --no-entry
      -sALLOW_MEMORY_GROWTH=1
      -sINITIAL_MEMORY=640kB
      -sTOTAL_STACK=20000
      -sEXPORTED_FUNCTIONS=_rpc_description,_start,_malloc,_waos_socket_data,_waos_socket_output_drain
      -sERROR_ON_UNDEFINED_SYMBOLS=0
      -Wl,--allow-undefined-file=${CMAKE_SOURCE_DIR}/cpp-rpc/src/examples/modules/allowed-undefined-symbols.syms
    )

    target_include_directories(${EXAMPLE_MODULE_NAME} PRIVATE include src)
    target_compile_definitions(${EXAMPLE_MODULE_NAME}
      PRIVATE
        RPC_SERVER
        RPC_TYPE_DESCRIPTION
    )
    set_target_properties(${EXAMPLE_MODULE_NAME} PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/examples/modules
      SUFFIX ".wasm"
    )
    
  endforeach()
endif()

# Native unit tests (Boost.UT)
if(NOT WASM)
  add_library(socket_test_stub STATIC src/test/socket_flush_stub.c)
  target_include_directories(socket_test_stub PRIVATE src)

  # --- Existing tests ---

  add_executable(io_circular_buffer_ut src/test/io_circular_buffer_ut.cpp ${IO_FILES})
  target_include_directories(io_circular_buffer_ut PRIVATE src)
  target_link_libraries(io_circular_buffer_ut PRIVATE Boost::ut)
  set_target_properties(io_circular_buffer_ut PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
  )

  add_executable(socket_loopback_ut src/test/socket_loopback_ut.cpp ${IO_FILES} ${SOCKET_FILES})
  target_include_directories(socket_loopback_ut PRIVATE src)
  target_link_libraries(socket_loopback_ut PRIVATE Boost::ut socket_test_stub)
  set_target_properties(socket_loopback_ut PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
  )

  # --- Serialization tests (stream) ---

  add_executable(serialization_stream_ut src/test/serialization_stream_ut.cpp ${IO_FILES})
  target_include_directories(serialization_stream_ut PRIVATE src)
  target_link_libraries(serialization_stream_ut PRIVATE Boost::ut)
  set_target_properties(serialization_stream_ut PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
  )

  # --- Serialization tests (circular_buffer) ---

  add_executable(serialization_cb_ut src/test/serialization_cb_ut.cpp ${IO_FILES})
  target_include_directories(serialization_cb_ut PRIVATE src)
  target_link_libraries(serialization_cb_ut PRIVATE Boost::ut)
  set_target_properties(serialization_cb_ut PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
  )

  # --- Serialization types tests (custom structs, rpc messages) ---

  add_executable(serialization_types_ut src/test/serialization_types_ut.cpp ${IO_FILES})
  target_include_directories(serialization_types_ut PRIVATE src)
  target_link_libraries(serialization_types_ut PRIVATE Boost::ut)
  set_target_properties(serialization_types_ut PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
  )

  # --- Message channel tests ---

  add_executable(message_channel_ut src/test/message_channel_ut.cpp ${IO_FILES} ${SOCKET_FILES})
  target_include_directories(message_channel_ut PRIVATE src)
  target_link_libraries(message_channel_ut PRIVATE Boost::ut socket_test_stub)
  set_target_properties(message_channel_ut PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
  )

  # --- RPC loopback tests (stream-based) ---

  add_executable(rpc_loopback_ut src/test/rpc_loopback_ut.cpp ${IO_FILES})
  target_include_directories(rpc_loopback_ut PRIVATE src)
  target_link_libraries(rpc_loopback_ut PRIVATE Boost::ut)
  set_target_properties(rpc_loopback_ut PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
  )

  # --- Socket RPC tests ---

  add_executable(socket_rpc_ut src/test/socket_rpc_ut.cpp ${IO_FILES} ${SOCKET_FILES})
  target_include_directories(socket_rpc_ut PRIVATE src)
  target_link_libraries(socket_rpc_ut PRIVATE Boost::ut socket_test_stub)
  set_target_properties(socket_rpc_ut PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
  )

  # --- CTest registration ---

  enable_testing()
  add_test(NAME io_circular_buffer_ut COMMAND io_circular_buffer_ut WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/tests)
  add_test(NAME socket_loopback_ut COMMAND socket_loopback_ut WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/tests)
  add_test(NAME serialization_stream_ut COMMAND serialization_stream_ut WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/tests)
  add_test(NAME serialization_cb_ut COMMAND serialization_cb_ut WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/tests)
  add_test(NAME serialization_types_ut COMMAND serialization_types_ut WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/tests)
  add_test(NAME message_channel_ut COMMAND message_channel_ut WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/tests)
  add_test(NAME rpc_loopback_ut COMMAND rpc_loopback_ut WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/tests)
  add_test(NAME socket_rpc_ut COMMAND socket_rpc_ut WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/tests)
endif()