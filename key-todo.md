# Key format — TODO

Szczegóły: [key-format.md](key-format.md).

## Zamrożony layout

- [x] `[data][meta][u8 meta_size]` (max 255 B meta, else throw)
- [x] Skalary w data = encoding compressed
- [x] Lengths / string lens w meta (uvarint32)
- [x] Throw: map, shared_ptr, typed/message/exception
- [x] unique_ptr OK; SimpleException poza kontraktem

## Pliki

- [x] `src/serialization/key_codec.hpp`
- [x] `src/serialization/key_stream_writer.hpp` / `key_stream_reader.hpp`
- [x] `src/serialization/key_circular_buffer_writer.hpp` / `key_circular_buffer_reader.hpp`
- [x] `src/test/serialization_key_ut.cpp`
- [x] `src/test/serialization_key_circular_ut.cpp`
- [x] CMake targets + `add_test`
- [x] README

## Później

- [ ] Lex string / tuple escape (osobny kontrakt)
- [ ] Zero-copy circular decoder bez peek całego message
