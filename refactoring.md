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
| 6. Stemplować RPC na codec | Pending | — |

Po kroku 5b `flat_zero_copy_codec` obsługuje payload: handler dostaje
`flat_view<T>&` + `payload_size`, czyta surowe bajty payloadu przez `channel.reader()`
(reader pozycjonowany na payload przez `skip`). Wire format flat (materialized
i zero-copy) jest wspólny i interoperacyjny: 12-bajtowy nagłówek
`[type index][root_frame_size][payload_size]` przed ramką. Compressed bez zmian.

### Wire format flat

```
[length prefix 4B][type index 4B u32 LE][root_frame_size 4B u32 LE][payload_size 4B u32 LE]
                 [root T frame N_root B][shared sub-frames N_shared B][payload P B]
```

- `root_frame_size = N_root` — rozmiar samej ramki root T (bez shared sub-frames).
  Pozwala `flat_view<T>(session, body_offset, body_offset + N_root)` — `flat_view`
  czyta trailer CPS z `end`, więc `end` musi być dokładnym końcem ramki root.
- `payload_size = P` — payload leży na końcu: `payload_offset = msg_len - P`,
  niezależne od `N_shared` (shared sub-frames między ramką root a payloadem).
- `payload_size` wpisywany przez `codec.finalize_send()` (wołane z
  `framing.commit_send` po zapisaniu payloadu) — w `write_message_header`
  payload jeszcze nie jest znany (caller pisze go po `begin_send`).
- Compressed BEZ ZMIAN: `[index uvarint64][serialize][write_rest][payload]`,
  `payload_size = msg_len - cursor` na odczycie.

### Ograniczenia zero-copy v1

- **shared_ptr grafy = nullptr.** `flat_view<T>` dla pól `shared_ptr<U>` zwraca
  `session->pointer_at<U>()` = nullptr (bez `bind_pointers`). Full graph zero-copy
  wymaga `flat_root_shared_on_session` z root delta offset 4 (nie 0) — to future
  extension API `flat_view` (krok 5c). v1: handler widzi nullptr dla `shared_ptr`
  pól, skalary/stringi zero-copy działają.
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
6. **Stemplować `socket_rpc_server`/`socket_rpc_client` na codec**, usunąć
   duplikat `process_call`.

Krok 1 jest bezpieczny i daje samodzielny zysk; krok 2 najlepiej robić razem
z krokiem 3.

