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

## Kolejność wdrożenia

1. **Parity API flat** (prework, niezależny krok).
   - flat writers: `register_serialize_message_function<T>()`,
     `register_serialize_exception_function<T>()`, `write_exception(e)`
   - flat readers: `register_deserialize_exception_function<T>()`,
     `read_exception()`
2. **Wydzielić framing** z obecnego `socket_message_channel` — nawet bez codec,
   sam framing jako klasa bazowa to czysty zysk.
3. **Wydzielić `compressed_codec`** — opakować obecną logikę, zachować behavior.
4. **Dodać `flat_materialized_codec`** — powinno być prawie kopia compressed z
   innymi reader/writer.
5. **Dodać `flat_zero_copy_codec`** — tylko tu pojawia się
   `flat_root_circular<T>` w dispatchu.
6. **Stemplować `socket_rpc_server`/`socket_rpc_client` na codec**, usunąć
   duplikat `process_call`.

Krok 1 jest bezpieczny i daje samodzielny zysk; krok 2 najlepiej robić razem
z krokiem 3.
