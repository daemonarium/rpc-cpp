# Refactoring: socket message channel + RPC na format flat

Cel: obsługa wiadomości i RPC w formacie flat (materialized i zero-copy) obok compressed,
przy jak najmniejszym duplikowaniu kodu i wysokiej czytelności.

## Stan obecny (co blokuje)

### Warstwa „generyczna" już jest templowana
- `rpc_server<Rd, Wr>` (`serialization/rpc_server.hpp`) i `rpc_client<Rd, Wr>`
  (`serialization/rpc_client.hpp`) są sparametryzowane konceptami `Reader`/`Writer`.
  Teoretycznie format-agnostyczne.

### Warstwa socketowa jest na sztywno wbity w compressed
- `socket_message_channel` (`serialization/socket_message_channel.hpp`) tworzy
  `circular_buffer_reader`/`circular_buffer_writer` przez `unique_ptr` i wystawia
  je jako konkretne typy (`reader()`/`writer()`).
- `socket_rpc_server` dziedziczy
  `rpc_server_registry<circular_buffer_reader, circular_buffer_writer, ...>`
  i **dubluje** `process_call` z `rpc_server<Rd,Wr>` (copy-paste, bo channel daje
  konkretne typy, nie parametryczne).
- `socket_rpc_client` w ogóle nie jest templatem — hardcode
  `deserialize(channel_.reader(), …)`.

### Ścisłe sprzężenie z implementacją readera/writera
- Channel grzebie bezpośrednio w `reader_->read_offset_` i `writer_->write_offset_`
  (`socket_message_channel.hpp`).
- Dispatch czyta index przez `reader_->read_u64(index)` — to OK, jest w koncencie
  `Reader`.

### Braki w flat readerach/writerach do RPC
- `flat_stream_writer` / `flat_circular_buffer_writer` **nie mają**
  `register_serialize_message_function`, `register_serialize_exception_function`,
  `write_exception`.
- `flat_stream_reader` / `flat_buffer_reader` **nie mają**
  `register_deserialize_exception_function`, `read_exception`.
- Czyli: nawet bez zmiany architektury, trzeba dokończyć API flat do poziomu
  parity z compressed.

### Zero-copy na flat — już prawie działa
- `flat_root_circular<T>(cb, msg_len)` istnieje i działa na ringu bez materializacji
  (test `serialization_flat_circular_ut.cpp`).
- W `handle_data` bytes są żywe aż do `circular_buffer_skip_read(msg_len)` po
  handlerze — więc okno na zero-copy jest zachowane. To jest kluczowe: **zero-copy
  flat na sockecie jest wykonalne bez przepisywania framingu**.

### Wniosek
Problem nie jest „jak zrobić zero-copy", tylko „jak sparametryzować warstwę
socketową po format + tryb odczytu, nie dublując framingu i dispatchu".

## Docelowa architektura (połączenie Propozycji 2 + 3)

W Propozycji 2 codec niesie dispatch (bo sygnatura handlera zależy od formatu),
a w Propozycji 3 framing bazuje na readerze/writerze kanału. Połączenie polega na
tym, że **framing staje się format-agnostyczny i nie potrzebuje readera/writera
do dispatchu — oddaje go codecowi**.

### `socket_message_framing<Codec>` (bazowy, cienki)

- Pętla `handle_data`: length-prefix, `circular_buffer_peek`/`skip_read`,
  `waos_socket_flush`, `waos_socket_input_drain`.
- Po odczytaniu `msg_len` wywołuje `codec_.dispatch(index, cb, msg_len)`.
- `begin_send`/`commit_send`: rezerwuje 4 bajty na długość, oddaje body do
  `codec_.writer()`, na koniec poke długości + commit + flush.
- **Nie wie** nic o reader/writer/typach wiadomości.

### `Codec` (policy, 3 implementacje)

Codec posiada:
- writer (i reader jeśli materializuje) — np. `flat_circular_buffer_writer`,
  `circular_buffer_writer`
- `dispatch_table_`, `message_handlers_`, `message_type_indexes_`
- `register_message_type<T>()`, `register_exception_type<T>()`,
  `write_exception`, `read_exception`
- `write_message_index(writer, i)` / `read_message_index(...)`
- `dispatch<T>(cb, msg_len)`:
  - **compressed / flat-materialized**:
    `deserialize(reader, msg); reader.read_rest(); handler(msg, payload_size)`
  - **flat-zero-copy**:
    `auto v = flat_root_circular<T>(cb, msg_len); handler(v, 0)`

Trzy implementacje:
- `compressed_codec` — obecne zachowanie, materializacja
- `flat_materialized_codec` — format flat, materializacja do `T`
- `flat_zero_copy_codec` — format flat, handler dostaje `flat_view<T>`

### `socket_rpc_server<Codec>` / `socket_rpc_client<Codec>`

Templatowane na codec, dziedziczą
`rpc_server_registry<typename Codec::reader_type, typename Codec::writer_type, ...>`.
**Duplikat `process_call` znika** (ten z `socket_rpc_server.hpp`).

## Szwy, na które trzeba uważać

1. **`reader_type` dla zero-copy codec.** Args RPC trzeba materializować
   (funkcja woła się z `int x, std::string y`, nie z `flat_view`). Więc nawet
   `flat_zero_copy_codec` ma `reader_type` do deserializacji argumentów — zero-copy
   dotyczy **odpowiedzi / one-way wiadomości**, nie request args. To rozwiązuje
   pozorny paradoks: codec ma reader dla args, ale handler wiadomości dostaje
   `flat_view<T>`.

2. **Sygnatura handlera zależy od codec.** `on_message<T>(void(T&))` dla
   materialized, `on_message<T>(void(flat_view<T>&))` dla zero-copy. Codec
   definiuje typ `handler_signature<T>`, a `on_message` w framingu jest
   templatowany po codec — `static_assert` wyłącza zły wariant.

3. **Offset API.** Framing grzebie dziś w `read_offset_`/`write_offset_`. W
   połączonym wariancie framing nie dotyka offsetów — to codec resetuje swój
   reader per wiadomość. Czyściej.

4. **Parity API flat.** Niezależnie od kombinowania architekturą, flat
   writers/readery muszą dostać `register_serialize_message_function`,
   `register_serialize_exception_function`, `write_exception`,
   `read_exception`.

## Ocena

| | |
|---|---|
| **Za** | framing ~30 linii socket I/O, współdzielone przez 3 codec; codec = format + dispatch + reader/writer w jednym miejscu; zero-copy to po prostu inny codec, a nie specjalna ścieżka; `process_call` deduplikowany; testowalne bez socketa |
| **Przeciw** | codec niesie sporo (dispatch + handlers + reader/writer + exception); dwie sygnatury handlera; trzeba dokończyć API flat |
| **Czytelność** | wysoka — każdy z 3 codec to mały, samodzielny byt; framing to trywialna pętla |

## Stan realizacji

| Krok | Status | Pliki |
|------|--------|-------|
| 1. Parity API flat | **Done** | `flat_stream_writer.hpp`, `flat_circular_buffer_writer.hpp`, `flat_stream_reader.hpp`, `flat_buffer_reader.hpp` (dodano `register_serialize_message_function`, `write_message`, `register_serialize_exception_function`, `write_exception`, `register_deserialize_exception_function`, `read_exception`); test: `serialization_flat_exception_ut.cpp` |
| 2. Wydzielenie framing | **Done** | `socket_message_framing.hpp` (nowy, `socket_message_framing<Codec>`) |
| 3. Wydzielenie `compressed_codec` | **Done** | `compressed_codec.hpp` (nowy), `socket_message_channel.hpp` → alias `socket_message_framing<compressed_codec>`; test: `compressed_codec_ut.cpp` |
| 4. `flat_materialized_codec` | **Done** | `flat_materialized_codec.hpp` (nowy), `flat_buffer_reader.hpp` (+`reset()`), `socket_message_channel.hpp` (+alias `flat_socket_message_channel`); test: `flat_materialized_codec_ut.cpp` |
| 5. `flat_zero_copy_codec` | **Done** | `flat_zero_copy_codec.hpp` (nowy), `socket_message_framing.hpp` (generyczny `on_message<T,Handler>`), `socket_message_channel.hpp` (+alias `flat_zero_copy_socket_message_channel`); testy: `flat_zero_copy_codec_ut.cpp`, `flat_zero_copy_message_channel_ut.cpp` |
| 5b. Payload w flat zero-copy | **Done** | `flat_buffer_reader.hpp` (+`skip(n)`), `compressed_codec.hpp` (+`finalize_send()` no-op), `socket_message_framing.hpp` (`commit_send` woła `finalize_send`), `flat_materialized_codec.hpp` + `flat_zero_copy_codec.hpp` (12-bajtowy nagłówek: index + root_frame_size + payload_size + `finalize_send` pokuje payload_size); testy: payload w `flat_zero_copy_codec_ut.cpp`, `flat_zero_copy_message_channel_ut.cpp` |
| 5c. Full zero-copy grafów `shared_ptr` | **Done** | `flat_stream_writer.hpp` + `flat_circular_buffer_writer.hpp` (+`mark_written_pointer`, `poke_u32`, per-frame size prefix w `write_rest`), `flat_stream_reader.hpp` + `flat_buffer_reader.hpp` (+`mark_read_pointer`), `flat_view.hpp` (`flat_root_shared_on_session` z `body_offset` — walk prefiksów, `resolve_delta` relatywne do bieżącej ramki, usunięto `measure_frame`/`later_slots_`), `serialize.hpp`/`deserialize.hpp` (`make_shared_write_later`/`make_shared_read_later`), `flat_materialized_codec.hpp` + `flat_zero_copy_codec.hpp` (nowy wire format: per-frame size prefix + sentinel, `finalize_send` no-op); testy: `serialization_flat_generated_ut.cpp` (32 grafy z cyklami + `flat_view`), `serialization_flat_ut.cpp`, `serialization_flat_circular_ut.cpp`, `serialization_flat_circular_writer_ut.cpp`, `flat_zero_copy_codec_ut.cpp`, `flat_zero_copy_message_channel_ut.cpp` |
| 5d. Deduplikacja codec (CRTP baza) | **Done** | `message_codec_base.hpp` (nowy, CRTP baza: `dispatch_table_`/`message_type_indexes_`/`message_handlers_`/`current_msg_len_`, `register_exception_type`/`register_dynamic_type`/`register_message_type` szkielet, `dispatch` szkielet, `finalize_send` no-op, `invoke_handler<T>`), `compressed_codec.hpp` + `flat_materialized_codec.hpp` + `flat_zero_copy_codec.hpp` (dziedziczą z bazy, usunięto duplikat map i szkieletów, format-specyficzne hooki w pochodnych: `reader`/`writer`/`read_type_index`/`write_message_header`/`make_dispatch_handler`/`reset_for_dispatch`/`on_message`); testy: bez zmian, wszystkie codec/channel/RPC przechodzą |
| 5e. Deduplikacja flat codec (CRTP baza pośrednia) | **Done** | `flat_message_codec_base.hpp` (nowy, CRTP baza pośrednia dziedzicząca `message_codec_base`: `read_type_index` (u32+prefix), `write_message_header<T>`, `poke_u32`, `payload_start_`), `flat_materialized_codec.hpp` + `flat_zero_copy_codec.hpp` (dziedziczą z `flat_message_codec_base`, usunięto byte-identical `read_type_index`/`write_message_header`/`poke_u32`/`payload_start_`; `on_message` zostaje w pochodnych — flat_zero_copy nie dziedziczy `void(T&)`); testy: bez zmian |
| 5f. Deduplikacja flat writer/reader (CRTP baza) | **Done** | `flat_writer_base.hpp` + `flat_reader_base.hpp` (nowe, CRTP bazy dziedziczące `writer`/`reader`: `mark_*_pointer`, `make_shared_*_later<T>`, `write_pointer`/`read_pointer` (+weak), `write_rest`/`read_rest` per-frame prefix loop, tabele `written_pointers_`/`write_later_`/`read_pointers_`/`read_later_`/`skip_pointers_`, hooki `current_pos()`/`poke_u32()`), `flat_stream_writer.hpp` + `flat_circular_buffer_writer.hpp` (dziedziczą `flat_writer_base`, dodano `current_pos()`/`poke_u32()`, usunięto duplikat), `flat_stream_reader.hpp` + `flat_buffer_reader.hpp` (dziedziczą `flat_reader_base`, usunięto duplikat); testy: bez zmian, wszystkie 22 executów przechodzą |
| 6. Stemplować RPC na codec | **Done** | `rpc_process_call_mixin.hpp` (nowy, CRTP mixin: 4 warianty `process_call` z `if constexpr`/constraints, woła `derived().send_response`/`send_error` bez writera), `rpc_call_mixin.hpp` (nowy, CRTP mixin: 4 overloads `call`/`call_method`, lambdy wołają `derived().reader()`, wysyłka `derived().send_request_impl`/`send_method_request_impl`, tabele `response_handlers_`/`last_request_id_`), `rpc_server.hpp` (dziedziczy mixin, `current_writer_`, `send_response`/`send_error` bez parametru writera), `rpc_client.hpp` (dziedziczy mixin, `current_reader_`, `reader()`, `send_request_impl`/`send_method_request_impl`), `socket_rpc_server.hpp` → templatka `socket_rpc_server<Codec>` + mixin, alias `compressed_socket_rpc_server`, `socket_rpc_client.hpp` → templatka `socket_rpc_client<Codec>` + mixin, alias `compressed_socket_rpc_client`, `message_codec_base.hpp` (+`reader_type`/`writer_type`); testy: `socket_rpc_ut.cpp` (alias), `flat_rpc_loopback_ut.cpp` (nowy, `rpc_server`/`rpc_client` na flat stream), `flat_socket_rpc_ut.cpp` (nowy, `socket_rpc_server`/`socket_rpc_client<flat_materialized_codec>`); wszystkie 24 executów przechodzą |

Po kroku 5c `flat_zero_copy_codec` obsługuje **full zero-copy grafów `shared_ptr`**:
handler dostaje `flat_view<T>&` z powiązanymi polami `shared_ptr<U>` (graf
homogeniczny T), bez materializacji. Wire format flat (materialized i
zero-copy) jest wspólny i interoperacyjny: per-frame size prefix + 0 sentinel
(zamiast 12-bajtowego nagłówka z 5b). Compressed bez zmian.

### Wire format flat (po 5c)

```
[length prefix 4B][type index 4B u32 LE]
                 [encoded_size_0 = N0+1 4B u32 LE][frame 0 = root T N0 B]
                 [encoded_size_1 = N1+1][frame 1 sub N1 B] ...
                 [0 sentinel 4B]
                 [payload P B]
```

- `encoded_size_i = real_size_i + 1` (zero = sentinel, pusta ramka -> encoded 1).
  Brak `root_delta` (root = frame 0), `root_frame_size`, `payload_size` w nagłówku.
- Root jest deref-serializowany (`serialize(w, *value)`, nie `write_pointer`) i
  pre-rejestrowany w `written_pointers_` przez `mark_written_pointer` — self-ref
  roota (delta=-1) używa back-reference, nie duplikuje ramki.
- Sub-frame w grafie: `[encoded_size_i][frame i]` (brak type index, graf
  homogeniczny T). `write_rest` opakowuje każdy sub-frame per-frame prefixem.
- `payload_size = msg_len - cursor` po sentinel — dispatch walkuje prefiksy
  do sentinel, `reader_->skip` do payload.
- Wyjątki/typed top-level: `[type index][encoded_size][frame][0 sentinel]`
  (normalnie, jak wiadomości).
- Compressed BEZ ZMIAN: `[index uvarint64][serialize][write_rest][payload]`,
  `payload_size = msg_len - cursor` na odczyt.

### Zachowanie zero-copy (po 5c)

- **shared_ptr grafy = full zero-copy.** `flat_view<T>` dla pól
  `shared_ptr<U>` zwraca powiązane `flat_view<U>` (graf homogeniczny T).
  `flat_root_shared_on_session<T>(session, body_offset, msg_len)` walkuje
  per-frame size prefiksy, buduje pool `flat_view<T>` i binduje wskaźniki
  przez `resolve_delta` (relatywne do bieżącej ramki: root delta=+m -> pool[m],
  sub-frame i delta=+m -> pool[i+m-1]; back-ref delta<0 -> resolved_).
  Self-ref roota i cykle obsługiwane (root pre-rejestrowany w `resolved_`).
- **Payload obsługiwany.** Handler `void(flat_view<T>&, size_t payload_size)` czyta
  surowe bajty payloadu przez `channel.reader()` (pozycjonowany na payload przez
  `skip`). Handler `void(flat_view<T>&)` ignoruje payload (framing skipuje).
- **Lifetime view = okno dispatch.** `flat_view` jest ważny tylko w handlerze,
  przed `framing.skip_read(msg_len)` po dispatch. Po powrocie z handlera sesja
  jest zwalniana. Handler musi skopiować dane, których potrzebuje poza dispatch.

## Kolejność wdrożenia

1. **Parity API flat** (prework, niezależny krok). **[DONE]**
   - flat writers: `register_serialize_message_function<T>()`,
     `register_serialize_exception_function<T>()`, `write_exception(e)`
   - flat readers: `register_deserialize_exception_function<T>()`,
     `read_exception()`
2. **Wydzielić framing** z obecnego `socket_message_channel` — nawet bez codec,
   sam framing jako klasa bazowa to czysty zysk. **[DONE]**
3. **Wydzielić `compressed_codec`** — opakować obecną logikę, zachować behavior. **[DONE]**
4. **Dodać `flat_materialized_codec`** — powinno być prawie kopia compressed z
   innymi reader/writer. **[DONE]**
5. **Dodać `flat_zero_copy_codec`** — tylko tu pojawia się
   `flat_root_circular<T>` w dispatchu. **[DONE]**
5b. **Payload w flat zero-copy** — zmiana wire formatu flat (12-bajtowy nagłówek
   z `root_frame_size` + `payload_size`), `finalize_send` w framing, `skip` w
   readerze. **[DONE]**
5c. **Full zero-copy grafów `shared_ptr`** — zmiana wire formatu flat na
   per-frame size prefix + 0 sentinel (usunięto 12-bajtowy nagłówek z 5b),
   `flat_root_shared_on_session` z `body_offset` (walk prefiksów, bez
   `measure_frame`), `mark_written_pointer`/`mark_read_pointer` (root
   deref-serializowany), `resolve_delta` relatywne do bieżącej ramki. **[DONE]**
5d. **Deduplikacja codec (CRTP baza `message_codec_base`)** — wyciągnięcie
   współdzielonych map (`dispatch_table_`/`message_type_indexes_`/`message_handlers_`/`current_msg_len_`)
   i szkieletów (`register_exception_type`/`register_dynamic_type`/`register_message_type`/`dispatch`/`finalize_send`/`invoke_handler`)
   do CRTP bazy `message_codec_base<Derived, Reader, Writer>`; `compressed_codec`,
   `flat_materialized_codec`, `flat_zero_copy_codec` dziedziczą z bazy i
   dostarczają tylko format-specyficzne hooki (`reader`/`writer`/`read_type_index`/
   `write_message_header`/`make_dispatch_handler`/`reset_for_dispatch`/`on_message`).
   API publiczne bez zmian, testy bez modyfikacji. **[DONE]**
5e. **Deduplikacja flat codec (CRTP baza pośrednia `flat_message_codec_base`)** —
   wyciągnięcie byte-identical `read_type_index` (u32+prefix), `write_message_header<T>`,
   `poke_u32`, `payload_start_` do CRTP bazy pośredniej `flat_message_codec_base<Derived>`
   (dziedziczącej `message_codec_base`); `flat_materialized_codec` i
   `flat_zero_copy_codec` dziedziczą z niej. `on_message` zostaje w pochodnych —
   flat_zero_copy nie dziedziczy `void(T&)` (jej dispatch przekazuje `flat_view<T>*`,
   reinterpret_cast na `T*` byłby UB). API bez zmian, testy bez modyfikacji. **[DONE]**
5f. **Deduplikacja flat writer/reader (CRTP baza `flat_writer_base`/`flat_reader_base`)** —
   wyciągnięcie `mark_*_pointer`, `make_shared_*_later<T>`, `write_pointer`/`read_pointer`
   (+weak), `write_rest`/`read_rest` (per-frame prefix loop) oraz tabelek
   `written_pointers_`/`write_later_`/`read_pointers_`/`read_later_`/`skip_pointers_`
   do CRTP baz `flat_writer_base<Derived>`/`flat_reader_base<Derived>` (dziedziczących
   `writer`/`reader`); pary flat writerów/readerów dziedziczą z baz, dostarczają
   hooków `current_pos()`/`poke_u32()`. `begin_frame`/`end_frame`/`write_typed`/
   `register_*` zostają w concrete (większy refactor, osobny follow-up). API bez
   zmian, testy bez modyfikacji, wszystkie 22 executów przechodzą. **[DONE]**
6. **Stemplować `socket_rpc_server`/`socket_rpc_client` na codec**, usunąć
   duplikat `process_call`. **[DONE]**

Krok 1 jest bezpieczny i daje samodzielny zysk; krok 2 najlepiej robić razem
z krokiem 3.

