# emscripten-p2996-toolchain.cmake

set(CMAKE_SYSTEM_NAME Emscripten)

set(COMPILER_SYSROOT "${CMAKE_SOURCE_DIR}/../cpp-compiler/sysroot")

# Ścieżka do Emscripten z zmodyfikowanym clangiem (P2996 z refleksją)
set(EMSCRIPTEN_ROOT_PATH ${COMPILER_SYSROOT}/emscripten)

# Kompilatory Emscripten używające clanga z refleksją
set(CMAKE_C_COMPILER ${EMSCRIPTEN_ROOT_PATH}/emcc-wrapper.sh)
set(CMAKE_CXX_COMPILER ${EMSCRIPTEN_ROOT_PATH}/em++-wrapper.sh)

# Flagi kompilatora – refleksja i C++26 z właściwymi include
set(CMAKE_CXX_FLAGS_INIT "-std=c++26 -freflection -fparameter-reflection -fannotation-attributes -fexpansion-statements")

# Flagi linkera dla Emscripten z libc++
set(CMAKE_EXE_LINKER_FLAGS_INIT "-s WASM=1 -s ALLOW_MEMORY_GROWTH=1 -lc++ -lc++abi -stdlib=libc++")

# Ustawienia specyficzne dla Emscripten
set(CMAKE_EXECUTABLE_SUFFIX ".html")

# Wyszukiwanie w Emscripten
set(CMAKE_FIND_ROOT_PATH ${EMSCRIPTEN_ROOT_PATH})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY) 