# p2996-toolchain.cmake

set(CMAKE_SYSTEM_NAME Linux)

# Kompilator eksperymentalny (P2996 z refleksją)
set(CMAKE_C_COMPILER /home/m8/clang-reflection-p2996/clang-p2996/sysroot-clang/bin/clang)
set(CMAKE_CXX_COMPILER /home/m8/clang-reflection-p2996/clang-p2996/sysroot-clang/bin/clang++)

# Ścieżki do zainstalowanej libc++/libc++abi/libunwind (sysroot)
set(LIBCXX_SYSROOT /home/m8/clang-reflection-p2996/clang-p2996/sysroot)
set(LIBCXX_INCLUDE_DIR ${LIBCXX_SYSROOT}/include/c++/v1)
set(LIBCXX_LIB_DIR ${LIBCXX_SYSROOT}/lib)

set(LIBCXX_INCLUDE_DIR_ARCH ${LIBCXX_SYSROOT}/include/x86_64-unknown-linux-gnu/c++/v1)
set(LIBCXX_INCLUDE_DIR_GEN ${LIBCXX_SYSROOT}/include/c++/v1)

# Flagi kompilatora – refleksja i include-only z sysroota
set(CMAKE_CXX_FLAGS_INIT "-std=c++26 -freflection -fparameter-reflection -fannotation-attributes -fexpansion-statements -nostdinc++ -I${LIBCXX_INCLUDE_DIR_ARCH} -I${LIBCXX_INCLUDE_DIR_GEN}")
#set(CMAKE_CXX_FLAGS_INIT "--sysroot=${LIBCXX_SYSROOT} -std=c++26 -freflection -fparameter-reflection -fannotation-attributes -fexpansion-statements")

# Flagi linkera – libc++, libc++abi, libunwind
set(CMAKE_EXE_LINKER_FLAGS_INIT "-L${LIBCXX_LIB_DIR} -Wl,-rpath,${LIBCXX_LIB_DIR} -lc++ -lc++abi -lunwind -stdlib=libc++")
#set(CMAKE_EXE_LINKER_FLAGS_INIT "--sysroot=${LIBCXX_SYSROOT} -stdlib=libc++")

# Używaj tylko podanych ścieżek include
set(CMAKE_CXX_STANDARD_INCLUDE_DIRECTORIES "")

# Wyszukiwanie tylko w sysroot
set(CMAKE_FIND_ROOT_PATH ${LIBCXX_SYSROOT})
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
