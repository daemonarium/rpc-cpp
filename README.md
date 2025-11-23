


# How to build - emscripten
use clang with experimental reflection
copy from it libcxx to emscripten /emsdk/upstream/emscripten/system/lib/libcxx
copy libcxx include to emsdk/upstream/emscripten/cache/sysroot/include/c++/v1
copy it will need __site_config file from emscripten emsdk/upstream/emscripten/cache/sysroot/include/c++


from standard emscripten copy files:
copy __config_site, __assertion_handler to /emsdk/upstream/emscripten/system/lib/libcxx
cp system/lib/libcxx/src/include/overridable_function.h /usr/lib/emsdk/upstream/emscripten/system/lib/libcxx/src/include/
cp system/lib/libcxx/src/new.cpp /usr/lib/emsdk/upstream/emscripten/system/lib/libcxx/src
cp system/lib/libcxxabi/src/stdlib_new_delete.cpp /usr/lib/emsdk/upstream/emscripten/system/lib/libcxxabi/src
