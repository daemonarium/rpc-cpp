# p2996-toolchain.cmake

set(CMAKE_SYSTEM_NAME Linux)


set(COMPILER_SYSROOT "${CMAKE_SOURCE_DIR}/../cpp-compiler/sysroot")

# Kompilator eksperymentalny (P2996 z refleksją)
set(CMAKE_C_COMPILER "${COMPILER_SYSROOT}/usr/local/bin/clang")
set(CMAKE_CXX_COMPILER "${COMPILER_SYSROOT}/usr/local/bin/clang++")

# Ścieżki do zainstalowanej libc++/libc++abi/libunwind (sysroot)
set(LIBCXX_INCLUDE_DIR ${COMPILER_SYSROOT}/usr/include/c++/v1)
set(LIBCXX_LIB_DIR ${COMPILER_SYSROOT}/usr/lib)

set(LIBCXX_INCLUDE_DIR_ARCH ${COMPILER_SYSROOT}/usr/include/x86_64-linux-gnu/c++/v1)
set(LIBCXX_INCLUDE_DIR_GEN ${COMPILER_SYSROOT}/usr/include/c++/v1)

# Flagi kompilatora – refleksja i include-only z sysroota
set(CMAKE_CXX_FLAGS_INIT "-std=c++26 -freflection -fparameter-reflection -fannotation-attributes -fexpansion-statements -nostdinc++ -I${LIBCXX_INCLUDE_DIR_ARCH} -I${LIBCXX_INCLUDE_DIR_GEN}")
#set(CMAKE_CXX_FLAGS_INIT "--sysroot=${LIBCXX_SYSROOT} -std=c++26 -freflection -fparameter-reflection -fannotation-attributes -fexpansion-statements")

# Flagi linkera – libc++, libc++abi, libunwind
set(CMAKE_EXE_LINKER_FLAGS_INIT "-L${LIBCXX_LIB_DIR} -Wl,-rpath,${LIBCXX_LIB_DIR} -lc++ -lc++abi -lunwind -stdlib=libc++")
#set(CMAKE_EXE_LINKER_FLAGS_INIT "--sysroot=${LIBCXX_SYSROOT} -stdlib=libc++")

# Używaj tylko podanych ścieżek include
set(CMAKE_CXX_STANDARD_INCLUDE_DIRECTORIES "")

# Wyszukiwanie tylko w sysroot
set(CMAKE_FIND_ROOT_PATH ${COMPILER_SYSROOT})
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
