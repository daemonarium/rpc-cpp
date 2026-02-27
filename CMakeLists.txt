cmake_minimum_required(VERSION 3.17)
project(cppSerialization)

set(CMAKE_CXX_STANDARD 26)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

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