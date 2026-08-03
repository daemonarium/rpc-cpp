# Format key (sortable binary keys)

Format zoptymalizowany pod **porządek binarny** (`memcmp`) kluczy DB: długości i county **nie** siedzą między polami danych. Lexykografia stringów poza scope (osobne kodowanie jako binary, jeśli potrzeba).

Status: **zaimplementowany** (`key_encoder` / `key_decoder`, `key_stream_*`, `key_circular_buffer_*`). Checklist: [key-todo.md](key-todo.md).

Powiązane: [README.md](README.md), [compressed-format.md](compressed-format.md).

---

## Cele

| Cel | Decyzja |
|-----|---------|
| Sort `memcmp` | najpierw bajty **data**; meta na końcu |
| Długości nie zaburzają sortu treści | `write_length` / len string\|binary → **meta** |
| Kompaktowość | varint (jak compressed) w data i meta |
| Grafy / mapy | **nie** — throw |

---

## Layout bloba

```text
[ data … ][ meta … ][ u8 meta_size ]
```

- **data** — skalary (encoding jak compressed: uvarint/svarint, float sortowalny, i8^0x80) + surowe bajty string/binary.
- **meta** — uvarint32 w kolejności wywołań (`write_length`, długości string/binary).
- **meta_size** — 1 bajt, rozmiar regionu meta (0–255). Przy `finish()`, jeśli meta &gt; 255 → throw. Pozwala znaleźć granicę data/meta; nie psuje sortu po data.

---

## Mapowanie Writer → bufory

| Operacja | Bufor |
|----------|--------|
| `write_u*` / `write_i*` / float / bool | data |
| `write_length` | meta (uvarint32) |
| `write_string` / `write_binary` | len → meta; bajty → data |
| `begin_frame` / `end_frame` / `save_offset` / `write_rest` | no-op |
| `write_map` / `write_pointer` / `write_typed` / `write_message` / `write_exception` | throw |

---

## Zakres typów

**OK:** skalary, enum, string / `string_view` / `const char*`, `vector<uint8_t>`, `vector` / `array`, UDT, `unique_ptr` (unwrap).

**Throw:** map, `shared_ptr` / `weak_ptr`, typed/message/exception registry.

`SimpleException` poza kontraktem (nie używać jako klucz).

---

## Backendy

| Komponent | Rola |
|-----------|------|
| `key_encoder` / `key_decoder` | core; `finish()` → blob |
| `key_stream_writer` / `key_stream_reader` | ostream: `serialize` + `finish()`; istream: wczytaj cały blob |
| `key_circular_buffer_writer` / `key_circular_buffer_reader` | `finish()` → write; reader `(cb, message_len)` przez peek |

---

## Odczyt

1. Odczytaj `meta_size` z ostatniego bajtu.
2. `data = blob[0 .. n)`, `meta = blob[n .. end-1)` gdzie `n = size - 1 - meta_size`.
3. Kursor data i meta do przodu (jak `deserialize`).

Circular: `message_len` = rozmiar bloba w readable; peek (także przez szew).
