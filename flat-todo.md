# Flat format — TODO implementacji

Szczegóły layoutu: [flat-format.md](flat-format.md).

## Zamrożony layout

- [x] Trailer: `K==0` → brak; `K>0` → `[format][cps…][format]` (duplikat na końcu)
- [x] format bits 0–1: szerokość offsetu (0→u8, 1→u16, 2→u32)
- [x] Pointer: fixed i32 LE delta; bez `save_offset`; ciała w `write_rest`
- [x] Out of scope v1: `view.name()`, RPC na flat, ewolucja schematu

## Pliki

- [x] `src/serialization/flat_encoding.hpp`
- [x] `src/serialization/flat_stream_writer.hpp`
- [x] `src/serialization/flat_circular_buffer_writer.hpp`
- [x] `src/serialization/flat_stream_reader.hpp`
- [x] `src/serialization/flat_source.hpp`
- [x] `src/serialization/flat_buffer_reader.hpp`
- [x] `src/serialization/flat_view.hpp`
- [x] `src/serialization/flat_bytes.hpp` (`flat_string` / `flat_bytes`)
- [x] `src/serialization/flat_bytes_writer.hpp`
- [x] `src/test/serialization_flat_ut.cpp`
- [x] `src/test/serialization_flat_circular_ut.cpp`
- [x] `src/test/serialization_flat_circular_writer_ut.cpp`
- [x] CMake: target + `add_test`
- [x] `reader.hpp` / `deserialize.hpp` / no-op `save_offset` w stream/cb readers
- [x] README + status w flat-format.md
- [x] `write_map` / `read_map` (compressed interleaved + flat SoA)
- [x] `flat_map_view` + map get w `flat_view`

## Fazy

### 1. Encoding helpers
- [x] LE read/write
- [x] format / W / trailer_size
- [x] `is_flat_variable` / `flat_fixed_size` (pointer = 4)

### 2. Deserialize mirror
- [x] `Reader::save_offset`
- [x] wywołania po variable w `deserialize.hpp`
- [x] no-op w compressed readers

### 3. flat_stream_writer
- [x] ramki + trailer
- [x] fixed LE scalars/string/binary
- [x] write_pointer / write_rest / write_typed (i32 delta)

### 4. flat_stream_reader
- [x] skip trailera
- [x] read_pointer / read_rest / read_typed
- [x] round-trip UT (+ graf)

### 5. FlatSource + flat_buffer_reader
- [x] concept + span adapter
- [x] `circular_flat_source` + C API segments / contig / try_ptr
- [x] sequential + pointer/rest (peek przez szew)
- [x] UT

### 6. flat_view
- [x] get<^^> / get<"name">
- [x] nested / vector
- [x] shared_ptr → shared_ptr\<flat_view\<T\>\>
- [x] `flat_root_circular` / `flat_root_shared_circular` (circular session; string/binary borrow lub per-field own)
- [x] UT (+ circular)

### 8. Mapa SoA
- [x] layout: key_off[N+1] sentinel, values, val_off trailer
- [x] `write_map` / `read_map` (flat + compressed)
- [x] `flat_map_view` BS `find` (memcmp order)
- [x] UT (empty, typy kluczy, UDT, nested, gaps)

### 7. Docs
- [x] odhaczyć ten plik
- [x] README

### 9. flat_circular_buffer_writer
- [x] poke + `write_offset_` (bez auto-commit)
- [x] ramki / mapy / pointer / typed jak stream
- [x] UT (roundtrip, wrap-on-write, graph, short space)

## Później

- [ ] `view.name()` codegen / injection
