# rpc-cpp

A C++26 serialization and RPC library. `[[=...]]` annotations drive a serializer, and an RPC layer runs over a socket framing layer. Three binary formats (flat, compressed, key) share the same `serialize` / `deserialize` core.

The CMake target is `cpp_rpc`. This repository is `daemonarium/rpc-cpp`.

## What you need

Stock Clang cannot compile this library. P2996 reflection lives in the [reflection-toolchain](https://github.com/daemonarium/reflection-toolchain) sysroot.

```
git clone https://github.com/daemonarium/reflection-toolchain.git
cd reflection-toolchain
./build-sysroot.sh
export P2996_COMPILER_SYSROOT=$PWD/sysroot
```

## Add it to a project

```
git submodule add https://github.com/daemonarium/rpc-cpp.git extern/rpc-cpp
```

```cmake
add_subdirectory(extern/rpc-cpp)
target_link_libraries(your_app PRIVATE cpp_rpc)
```

That build does not fetch Boost.UT and does not build the tests or examples.

## Build it standalone

```
cmake -B build -S . \
  -DCMAKE_TOOLCHAIN_FILE=$P2996_COMPILER_SYSROOT/toolchain/p2996-toolchain.cmake
cmake --build build
ctest --test-dir build --output-on-failure
```

WebAssembly example modules:

```
cmake -B build-web -S . \
  -DCMAKE_TOOLCHAIN_FILE=$P2996_COMPILER_SYSROOT/toolchain/emscripten-p2996-toolchain.cmake \
  -DWASM=ON
cmake --build build-web
```

## One include

```cpp
#include <serialization/serialize.hpp>

struct [[=serialization::message()]] Hello {
  std::string name;
};

serialize(writer, hello);
deserialize(reader, hello);
```

The long map of writers, readers, and RPC types is in [docs/architecture.md](docs/architecture.md).

## Formats

- [Flat](docs/flat-format.md) is the default for processes and RPC.
- [Compressed](docs/compressed-format.md) is the legacy opt-in.
- [Key](docs/key-format.md) is the database-key layout.
- [Type registry blob](docs/type-registry-blob.md) is the schema envelope.

## Wasm

The same sources compile to standalone wasm modules. `src/examples/modules/` is the export shape: `_rpc_description`, `_waos_socket_data`, `_waos_socket_output_drain`.

## License

MIT for this library. Upstream notices are in `NOTICE`.
