# cpp-rpc

Biblioteka C++ (C++26 reflection / `std::meta`) łącząca:

1. **serializację / deserializację** struktur użytkownika do różnych backendów zapisu i odczytu,
2. **RPC** oparte o tę serializację (request / response / error + kanał wiadomości na sockecie).

Warstwa serializacji jest celowo rozdzielona: **logika typów** (`serialize` / `deserialize`) nie zna formatu ani medium; **writer / reader** implementują konkretny sposób zapisu i odczytu bajtów.

---

## Trzy formaty binarne

Ta sama warstwa `serialize` / `deserialize` i ten sam kontrakt `Writer` / `Reader` obsługują **różne układy bajtów** (niekompatybilne ze sobą). Wybór formatu = wybór konkretnego writer/reader backendu.

| | [Skompresowany](compressed-format.md) | [Flat](flat-format.md) | [Key](key-format.md) |
|--|--------------------------------------|------------------------|----------------------|
| Status | zaimplementowany (`stream_*`, `circular_buffer_*`) | zaimplementowany (`flat_stream_*`, `flat_circular_buffer_writer`, `flat_buffer_reader`, `flat_view` / circular) | zaimplementowany (`key_stream_*`, `key_circular_buffer_*`) |
| Liczby | varint, float sortowalny | fixed LE | varint / float jak compressed (w **data**) |
| Odczyt | sekwencyjny | sekwencyjny + in-place | data do przodu + meta (długości) osobno |
| Długości | przed stringiem / w streamie | u32 LE / trailer | **meta** na końcu bloba (`[data][meta][u8]`) |
| Grafy / mapy | tak | tak | **nie** (throw) |
| Typowe użycie | RPC, transfer | mmap, hot-path read | klucze DB — `memcmp` bez length między polami |

Szczegóły: **[compressed-format.md](compressed-format.md)**, **[flat-format.md](flat-format.md)**, **[key-format.md](key-format.md)**.

---

## Architektura warstw

```
┌─────────────────────────────────────────────────────────────┐
│  RPC (rpc_client / rpc_server, socket_*, message channel)   │
└────────────────────────────┬────────────────────────────────┘
                             │ serialize / deserialize
┌────────────────────────────▼────────────────────────────────┐
│  serialize.hpp / deserialize.hpp                            │
│  (mapowanie typów C++ → wywołania Writer / Reader)          │
└────────────────────────────┬────────────────────────────────┘
                             │ concept Writer / Reader
┌────────────────────────────▼────────────────────────────────┐
│  writer.hpp / reader.hpp  (kontrakt + abstrakcyjne stuby)   │
└──────┬──────────────┬──────────────┬──────────────┬──────────┘
       │              │              │              │
 stream_*      circular_buffer_*   flat_*        key_*         debug_writer
 (compressed)   (compressed)     (flat LE +    (DB keys)      (tekstowy
                                  flat_view)                   dump)
```

### Warstwa codec (format-agnostyczny framing + RPC)

```
┌──────────────────────────────────────────────────────────────┐
│  socket_message_framing<Codec>  — framing: length-prefix,   │
│    flush/drain, dispatch → codec                             │
├──────────────────────────────────────────────────────────────┤
│  Codec (policy): compressed_codec | flat_materialized_codec │
│                  | flat_zero_copy_codec                      │
│    dziedziczą z message_codec_base (CRTP) /                  │
│    flat_message_codec_base (CRTP, dla flat)                 │
├──────────────────────────────────────────────────────────────┤
│  socket_rpc_server<Codec> / socket_rpc_client<Codec>         │
│    dziedziczą socket_rpc_*_base<Codec> (CRTP) +              │
│    rpc_process_call_mixin / rpc_call_mixin (CRTP)            │
└──────────────────────────────────────────────────────────────┘
```

Trzy implementacje codec:
- `compressed_codec` — format compressed, materializacja (obecne zachowanie).
- `flat_materialized_codec` — format flat, materializacja do `T`.
- `flat_zero_copy_codec` — format flat, handler dostaje `flat_view<T>&` (zero-copy).

Aliasy: `compressed_socket_rpc_server`/`client` = `socket_rpc_*<compressed_codec>`.

Dodatkowe elementy:

| Plik | Rola |
|------|------|
| `type_concepts.hpp` | Concepty typów: liczby, string, kontenery, mapy, pointery, wyjątki, UDT |
| `annotations.hpp` | Adnotacje: `[[=policy::ignore]]`, `[[=message()]]`, `[[=exception()]]`, `[[=dynamic()]]`, `[[=read()]]` |
| `type_registry.hpp` / `type_description.hpp` | Rejestr i opis typów (RPC, typed pointers) |
| `meta_expand.hpp` | Helpery do ekspansji reflection |

---

## Kontrakt Writer / Reader

### `Writer` (`writer.hpp`)

Backend zapisu musi udostępniać m.in.:

- **skalary**: `write_u64/i64/u32/…`, `write_f32/f64`, `write_bool`, `write_string`, `write_binary`, `write_length`
- **ramki (frames)**: `begin_frame()` / `end_frame()` — oznaczają początek/koniec obiektu lub kontenera (długość, ewentualne indeksy)
- **`save_offset()`** — punkt zaczepienia pod zapis offsetów / indeksów (obecnie no-op w stream i circular)
- **wskaźniki**: `write_pointer(shared_ptr|weak_ptr, WriteLater)` — deduplikacja grafów obiektów
- **polimorfizm**: `write_typed`, rejestracja funkcji serializacji typów / wiadomości

### `Reader` (`reader.hpp`)

Backend odczytu:

- odpowiadające `read_*` dla skalarów, stringów, binary, length
- **`enter_frame()` / `exit_frame()`**
- **`is_indexed()`** + **`enter_indexed_frame(id)`** — przygotowane pod format z dostępem swobodnym (obecnie `is_indexed() == false`, indexed frame rzuca)
- **`read_pointer` / `read_typed` / `read_rest()`** — odpowiednik `write_later` po stronie odczytu

Obecne backendy stream i circular są **sekwencyjne** (`is_indexed() == false`). Haki frame/offset/indexed są świadomie zostawione pod przyszły format flat / indexed.

---

## Mapowanie typów: `serialize` / `deserialize`

Pliki `serialize.hpp` i `deserialize.hpp` są **wspólne dla wszystkich writerów/readerów**. Nie wiedzą, czy bajty idą do `ostream`, circular buffer czy logu debug.

### Obsługiwane typy

| Kategoria | Zachowanie przy zapisie |
|-----------|-------------------------|
| Bezznakowe / znakowe int (1/2/4/8 B) | `write_u*` / `write_i*` |
| `float` / `double` | `write_f32` / `write_f64` |
| `bool` | `write_bool` |
| `enum` | jako `uint64` |
| `string` / string-like | `write_string` + `save_offset` |
| `vector<uint8_t>` (Binary) | `write_binary` |
| Kontenery (nie-string) | `begin_frame` → `write_length` → elementy → `end_frame` → `save_offset` |
| Mapy | jak kontener, para klucz+wartość |
| `unique_ptr` | dereferencja i serializacja wskazywanego |
| `shared_ptr` | `write_pointer` + opcjonalnie `write_typed` (polimorfizm) |
| UDT (struktury/klasy) | reflection: bazy + pola (z pominięciem `[[=policy::ignore]]`) w `begin_frame`…`end_frame` |
| wyjątki „proste” | serializacja `what()` jako string |

Deserializacja jest lustrzanym odbiciem tych ścieżek (`enter_frame` / `read_length` / `read_*` / `exit_frame`).

### Reflection (C++26)

Dla typów użytkownika:

```cpp
serialize_members → bases_of + nonstatic_data_members_of
deserialize_members → to samo po stronie odczytu
```

Nie trzeba ręcznie pisać `serialize`/`deserialize` dla typowych struktur — wystarczy, że pola są serializowalne.

### Grafy i `write_rest` / `read_rest`

`shared_ptr` nie jest zapisywany „w miejscu” całego obiektu przy pierwszym wystąpieniu:

1. Writer zapisuje **deltę indeksu** (null / już zapisany / będzie zapisany później).
2. Ciało obiektu (przez `write_typed` lub callback) trafia do kolejki `write_later_`.
3. Po głównej serializacji wywołuje się **`write_rest()`** — dopisuje odroczone obiekty.

Analogicznie reader: **`read_rest()`** dokańcza odroczone `read_pointer` / `read_typed`.

Przykład: `examples/serialize_people.cpp` (cykliczne `friends` między osobami).

---

## Format skompresowany (stream + circular_buffer) — skrót

Pełna specyfikacja: **[compressed-format.md](compressed-format.md)**.

`stream_writer` / `stream_reader` oraz `circular_buffer_writer` / `circular_buffer_reader` dzielą ten sam układ bajtów (varint, float sortowalny, grafy `shared_ptr`). Ramki i `save_offset` są u nich **no-op** — wspólne wywołania z `serialize` zyskują znaczenie dopiero w formacie flat.

### Rejestracja typów

Przed serializacją polimorficzną / wiadomościami:

```cpp
writer.register_serialize_typed_function<Person>();
reader.register_deserialize_typed_function<Person>();
// analogicznie: register_*_message_function, register_*_exception_function
```

Kolejność rejestracji musi być **identyczna** po obu stronach — indeksy trafiają do strumienia.

---

## Backendy zapisu / odczytu

### 1. Stream — `stream_writer.hpp` / `stream_reader.hpp`

- Medium: `std::ostream` / `std::istream`.
- Pełna implementacja uvarint/svarint w C++.
- Opcjonalny debug: `#define SERIALIZATION_STREAM_WRITER_DEBUG` / `…_READER_DEBUG`.
- Typowe użycie: pliki, stringstream, testy round-trip, payload RPC.

```cpp
serialization::stream_writer sw(oss);
serialization::serialize(sw, value);
sw.write_rest();

serialization::stream_reader sr(iss);
serialization::deserialize(sr, out);
sr.read_rest();
```

### 2. Circular buffer — `circular_buffer_writer.hpp` / `circular_buffer_reader.hpp`

- Medium: C API `io/circular_buffer.h` (`circular_buffer_peek` / `poke`, w tym `*_uvarint*` / `*_svarint*`).
- Zapis z `write_offset_`, odczyt z `read_offset_` — **nie konsumuje** bufora automatycznie (peek/poke względem offsetu); caller zarządza commit/consume.
- Writer: `ensure_writable` → `circular_buffer_reserve_for_writing` + poke.
- Reader: `ensure_readable`; przy braku danych rzuca `circular_buffer_reader::underflow_error`.
- Ten sam format binarny co stream — round-trip między stream a circular jest możliwy.

Użycie: IPC / ring buffer między procesami lub wątkami, protokoły bez kopiowania przez `stringstream`.

### 3. Debug — `debug_writer.hpp`

- Nie produkuje bajtów protokołu; wypisuje **drzewo wywołań** (`beginFrame`, `writeU64 …`, `writePointer …`, `writeRest`, …) z wcięciami.
- Implementuje ten sam kontrakt `Writer`, więc działa z istniejącym `serialize`.
- Do diagnostyki layoutu obiektów i kolejności pól / pointerów bez hexdumpu.

```cpp
serialization::debug_writer dw(std::cout);
dw.register_serialize_typed_function<Person>();
serialization::serialize(dw, john);
dw.write_rest();
```

---

## RPC (skrót)

Na serializacji oparte są:

- typy wiadomości: `rpc_request`, `rpc_response`, `rpc_error` (`rpc.hpp`, adnotacja `[[=message()]]`),
- `rpc_client` / `rpc_server` — serialize request/response (templatki na `Reader`/`Writer`),
- `socket_message_framing<Codec>` — format-agnostyczny framing (length-prefix, flush/drain, dispatch),
- `socket_rpc_server<Codec>` / `socket_rpc_client<Codec>` — RPC nad framingiem, templatki na codec.

### Codec

Codec to policy łącząca format (reader/writer) z dispatchem (tabele typów, handlerów). Trzy implementacje:

| Codec | Format | Handler `on_message<T>` | Reader args RPC |
|-------|--------|----------------------|-----------------|
| `compressed_codec` | compressed | `void(T&, size_t)` | materializacja |
| `flat_materialized_codec` | flat | `void(T&, size_t)` | materializacja |
| `flat_zero_copy_codec` | flat | `void(flat_view<T>&, size_t)` | materializacja (args), zero-copy (odpowiedzi/one-way) |

`flat_zero_copy_codec` używa `handler_signature<T> = flat_view<T>&` dla wiadomości użytkownika, ale control messages RPC (`rpc_request`/`rpc_response`/`rpc_error`) są zawsze materializowane przez dedykowaną ścieżkę `on_control_message`.

### CRTP deduplikacja

- `message_codec_base<Derived, Reader, Writer>` — wspólne tabele, `dispatch`, `register_*`, `invoke_handler`, `handler_signature`, control message path.
- `flat_message_codec_base<Derived>` — wspólne `read_type_index` (u32+prefix), `write_message_header<T>`, `poke_u32`, `make_materialized_dispatch_handler`.
- `flat_writer_base<Derived>` / `flat_reader_base<Derived>` — wspólne `mark_*_pointer`, `write_pointer`/`read_pointer`, `write_rest`/`read_rest` (per-frame prefix loop), tabele.
- `rpc_process_call_mixin<Derived>` / `rpc_call_mixin<Derived>` — wspólne `process_call` / `call` / `call_method`.
- `socket_rpc_server_base<Codec>` / `socket_rpc_client_base<Codec>` — wspólne control/registry/send.

---

## Adnotacje

| Adnotacja | Efekt |
|-----------|--------|
| `[[=serialization::policy::ignore]]` | pole pomijane w serialize/deserialize |
| `[[=serialization::message()]]` | typ wiadomości (RPC / message channel) |
| `[[=serialization::exception()]]` | typ wyjątku serializowalnego przez rejestr |
| `[[=serialization::dynamic()]]` | typ dynamiczny (hierarchia annotation) |
| `[[=serialization::read()]]` | adnotacja członu (kierunek odczytu; rozszerzalne) |

---

## Format flat — skrót

Pełna specyfikacja: **[flat-format.md](flat-format.md)**. Checklist: [flat-todo.md](flat-todo.md).

Fixed LE, sparse checkpointy (`save_offset` po polach variable), trailer `[format][cps…][format]`, grafy przez `write_rest` (delta = i32 LE). Mapy: SoA (`key_off[N+1]` + values + `val_off` trailer), porządek `memcmp`, `write_map` / `flat_map_view::find`.

| Komponent | Rola |
|-----------|------|
| `flat_stream_writer` / `flat_stream_reader` | zapis/odczyt sekwencyjny do `ostream`/`istream` |
| `flat_circular_buffer_writer` | flat LE do pierścienia (poke + `write_offset_`); caller `commit_write` |
| `flat_buffer_reader` + `FlatSource` | `span_flat_source` / `circular_flat_source` (peek, `get_segments`, `try_ptr`; szew OK) |
| `flat_view<T>` / `flat_map_view` / `flat_root` / `flat_root_shared` | meta `get<^^Field>()` / `get<"name">()` → `flat_string` / `flat_bytes` / nested views; mapy SoA + BS `find`; `shared_ptr` → `shared_ptr<flat_view<U>>` |
| `flat_root_circular` / `flat_root_shared_circular` | session na pierścieniu (wrap OK, bez full materialize); string/binary borrow lub per-field own; views do `skip_read` |

---

## Format key — skrót

Pełna specyfikacja: **[key-format.md](key-format.md)**. Checklist: [key-todo.md](key-todo.md).

Blob `[data][meta][u8 meta_size]`: skalary i bajty stringów w **data** (encoding jak compressed); długości w **meta** (max 255 B meta). Pod `memcmp` długości nie wbijają się między pola. Bez map / `shared_ptr`.

| Komponent | Rola |
|-----------|------|
| `key_encoder` / `key_decoder` | core + `finish()` |
| `key_stream_writer` / `key_stream_reader` | ostream (`serialize` + `finish`) / istream (cały blob) |
| `key_circular_buffer_writer` / `key_circular_buffer_reader` | write blob do ring; read `(cb, message_len)` przez peek |

---

## Pliki kluczowe (serializacja)

```
README.md, compressed-format.md, flat-format.md, flat-todo.md, key-format.md, key-todo.md
src/serialization/
  reader.hpp / writer.hpp
  serialize.hpp / deserialize.hpp
  stream_reader.hpp / stream_writer.hpp
  circular_buffer_reader.hpp / circular_buffer_writer.hpp
  flat_encoding.hpp, flat_source.hpp, flat_bytes.hpp, flat_bytes_writer.hpp
  flat_stream_writer.hpp / flat_stream_reader.hpp
  flat_circular_buffer_writer.hpp
  flat_buffer_reader.hpp / flat_view.hpp
  flat_writer_base.hpp / flat_reader_base.hpp          (CRTP bazy flat)
  key_codec.hpp, key_stream_*.hpp, key_circular_buffer_*.hpp
  debug_writer.hpp
  annotations.hpp, type_concepts.hpp, type_registry.hpp
  rpc.hpp, rpc_server.hpp, rpc_client.hpp
  rpc_process_call_mixin.hpp / rpc_call_mixin.hpp      (CRTP mixiny RPC)
  message_codec_base.hpp                                (CRTP baza codec)
  flat_message_codec_base.hpp                           (CRTP baza flat codec)
  compressed_codec.hpp / flat_materialized_codec.hpp / flat_zero_copy_codec.hpp
  socket_message_framing.hpp                            (format-agnostyczny framing)
  socket_message_channel.hpp                            (aliasy compressed/flat)
  socket_rpc_server_base.hpp / socket_rpc_client_base.hpp  (CRTP bazy RPC)
  socket_rpc_server.hpp / socket_rpc_client.hpp        (cienkie pochodne + aliasy)
src/io/circular_buffer.h
src/test/serialization_flat_ut.cpp
src/test/serialization_flat_circular_ut.cpp
src/test/serialization_flat_circular_writer_ut.cpp
src/test/serialization_flat_generated_ut.cpp
src/test/serialization_flat_exception_ut.cpp
src/test/serialization_compressed_generated_ut.cpp
src/test/compressed_codec_ut.cpp
src/test/flat_materialized_codec_ut.cpp
src/test/flat_zero_copy_codec_ut.cpp
src/test/flat_zero_copy_message_channel_ut.cpp
src/test/flat_zero_copy_rpc_ut.cpp
src/test/flat_rpc_loopback_ut.cpp
src/test/flat_socket_rpc_ut.cpp
src/test/socket_rpc_ut.cpp
src/test/serialization_key_ut.cpp
src/test/serialization_key_circular_ut.cpp
src/test/io_circular_buffer_ut.cpp
```

Przykłady: `src/examples/serialize_people.cpp`, `serialize_game.cpp`, `circular_buffer_serialization.test.cpp`.
