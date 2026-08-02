# Format flat (indexed / in-place)

Format zoptymalizowany pod **szybki odczyt** (w tym odczyt w miejscu i dostęp swobodny do pól) oraz **wydajny jednoprzebiegowy zapis**. Współdzieli kontrakt `Writer` / `Reader` oraz `serialize` / `deserialize` z formatem skompresowanym; inny jest układ bajtów i znaczenie `begin_frame` / `end_frame` / `save_offset`.

Status: **zaimplementowany** (`flat_stream_*`, `flat_buffer_reader`, `flat_view`). Checklist: [flat-todo.md](flat-todo.md).

Powiązane: [README.md](README.md), [compressed-format.md](compressed-format.md).

---

## Cele i założenia

| Cel | Decyzja |
|-----|---------|
| Max wydajność odczytu | fixed-width LE, sparse indeks checkpointów, `flat_view` / in-place |
| Dobra wydajność zapisu | jeden przebieg: payload, na końcu ramki trailer |
| Random access | tak, w ciągłym buforze / wycinku ramki |
| Grafy / cykle `shared_ptr` | tak — ten sam protokół `write_pointer` / `write_rest` co compressed; delta = **i32 LE** |
| Ewolucja schematu | **poza scope** |
| Walidacja / safety | nie jest celem |
| Alignment | nie wymagane |

---

## Właściwości

| Cecha | Wartość |
|-------|---------|
| Endianness | **little-endian** |
| Liczby całkowite | **fixed** 1 / 2 / 4 / 8 B |
| Float | IEEE754 LE |
| String / binary length, `write_length` | **u32 LE** |
| Indeks | sparse checkpointy po polach **variable** (`save_offset`) |
| Trailing `N` | **nie** — `K` ze schematu / `write_length` + reguł kontenera |
| Pointer delta | **i32 LE** (`0` null, `<0` already, `>0` later 1-based) |
| `is_indexed()` | `true` dla buffer readera |
| Medium | stream (blob), `FlatSource` / span; circular później |

Format flat i skompresowany **nie są** wzajemnie kompatybilne bajtowo.

---

## Ramka (frame)

```text
begin_frame()
  … payload …
end_frame()     → dopisuje trailer (gdy K>0)
save_offset()   → checkpoint po polu variable
```

### Layout ramki

```text
[ start ]
  payload
[ trailer — tylko gdy K > 0 ]
  [format : u8]
  [cp₀]…[cpₖ₋₁]     // każdy W bajtów LE
  [format : u8]     // duplikat — lokalizacja od końca
[ end ]
```

Przy **`K == 0` nie ma trailera**.

- `cpᵢ` względem `start` ramki.
- `format` bits 0–1: `0`→W=1, `1`→W=2, `2`→W=4.
- Od końca: `format = byte[end-1]`, `trailer_size = 2 + K*W`, początek trailera = `end - trailer_size`.
- Sekwencyjnie: kursor na początku trailera → czytaj `format`, skip `K*W`, czytaj drugi `format`.

### Semantyka `save_offset`

Po: `string`, `binary`, kontener, mapa, UDT.  
**Nie** po skalarach / enum / bool / **`shared_ptr`** (uchwyt fixed 4 B).

```text
cp = current_write_pos - frame_start
```

### Sparse indeks

| Przypadek | Checkpointy |
|-----------|-------------|
| UDT mixed | jeden `cp` na pole variable |
| `vector<uint32_t>` | 0 wewnątrz; stride |
| `vector<string>` / UDT | `cp` po każdym elemencie |
| `shared_ptr` | brak — fixed delta w miejscu; ciała w `write_rest` |

---

## Pointer / write_rest

Jak compressed:

1. W miejscu: `i32` delta.
2. Ciała odroczone → `write_rest()` (kolejne flat-ramki + typed index u32 LE).
3. Odczyt: `read_pointer` / `read_rest`.
4. `flat_view`: `get` na `shared_ptr<U>` → `shared_ptr<flat_view<U>>` (wspólna tabela identity).

---

## Wyliczanie pozycji pola (seek)

```text
pos = 0; c = 0
dla j przed F:
  jeśli variable(j): pos = cp[c++]
  else: pos += fixed_size(j)
→ start pola F
```

---

## Kodowanie wartości

| Typ | Layout |
|-----|--------|
| u8/i8 | 1 B raw |
| u16/i16 | 2 B LE |
| u32/i32 | 4 B LE |
| u64/i64 / enum | 8 B LE |
| bool | 1 B |
| f32/f64 | IEEE LE |
| string/binary | u32 len + bytes |
| kontener/mapa | u32 N + elementy (+ trailer) |
| UDT | pola reflection (+ trailer) |
| shared_ptr | i32 delta; ciało w rest |

---

## Backendy

| Komponent | Rola |
|-----------|------|
| `flat_stream_writer` / `flat_stream_reader` | ostream / istream |
| `flat_buffer_reader` + `FlatSource` | span / przyszły circular |
| `flat_view` | meta `get<>` |

---

## Poza scope

- Ewolucja schematu / field id
- Współdzielone vtable FB
- Circular `FlatSource` (na później)
- `view.name()` codegen
- RPC wyłącznie na flat
- Rygorystyczna walidacja OOB
