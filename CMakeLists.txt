cmake_minimum_required(VERSION 3.17)
project(cppSerialization)

set(CMAKE_CXX_STANDARD 26)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include_directories(src)
aux_source_directory(src SRC_FILES)
aux_source_directory(src/serialization SERIALIZATION_FILES)

add_executable(cppSerialization ${SRC_FILES} ${SERIALIZATION_FILES})

target_include_directories(cppSerialization PRIVATE include)

file(GLOB EXAMPLE_FILES "src/examples/*.cpp")

foreach(EXAMPLE_FILE ${EXAMPLE_FILES})
  get_filename_component(EXAMPLE_NAME ${EXAMPLE_FILE} NAME_WE)
  add_executable(${EXAMPLE_NAME} ${EXAMPLE_FILE} ${SERIALIZATION_FILES})
  target_include_directories(${EXAMPLE_NAME} PRIVATE include src)
  set_target_properties(${EXAMPLE_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/examples
  )
endforeach()


file(GLOB EXAMPLE_MODULES_FILES "src/examples/modules/*.cpp")

if(WASM)
  foreach(EXAMPLE_MODULE_FILE ${EXAMPLE_MODULES_FILES})
    get_filename_component(EXAMPLE_MODULE_NAME ${EXAMPLE_MODULE_FILE} NAME_WE)
    
    add_executable(${EXAMPLE_MODULE_NAME}.wasm ${EXAMPLE_MODULE_FILE} ${SERIALIZATION_FILES})
    target_link_options(${EXAMPLE_MODULE_NAME}.wasm PRIVATE
      -s STANDALONE_WASM=1
      -Wl,--import-memory
      --no-entry
      -sALLOW_MEMORY_GROWTH=1
      -sINITIAL_MEMORY=640kB
      -sTOTAL_STACK=20000
      -sEXPORTED_FUNCTIONS=_rpc_description,_start,_handle_message
      -sERROR_ON_UNDEFINED_SYMBOLS=0
      -Wl,--allow-undefined-file=${CMAKE_SOURCE_DIR}/src/examples/modules/allowed-undefined-symbols.syms
    )

    target_include_directories(${EXAMPLE_MODULE_NAME}.wasm PRIVATE include src)
    target_compile_definitions(${EXAMPLE_MODULE_NAME}.wasm
      PRIVATE
        RPC_SERVER
        RPC_TYPE_DESCRIPTION
    )
    set_target_properties(${EXAMPLE_MODULE_NAME}.wasm PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/examples/modules
    )
    
  endforeach()
endif()