# Format skompresowany (stream / circular)

> **Legacy opt-in** — to **nie** jest domyślny wire procesów / RPC.
> Domyślny RPC: [flat-format.md](flat-format.md) (`flat_socket_rpc_*`). Opt-in: C++ `WAOS_MODULE_RPC_COMPRESSED` / `compressed_socket_rpc_*`, TS `{ format: 'compressed' }`.

Sekwencyjny format binarny używany przez `stream_writer` / `stream_reader` oraz `circular_buffer_writer` / `circular_buffer_reader`. Oba backendy dzielą **ten sam układ bajtów**; różni je tylko medium.

Cel: mały rozmiar na przewodzie / w buforze, sortowalne klucze (DB), pełne grafy obiektów (`shared_ptr`), legacy transfer / RPC.

Status: **zaimplementowany** (opt-in).

Powiązane: [README.md](README.md), [flat-format.md](flat-format.md).

---

## Właściwości

| Cecha | Wartość |
|-------|---------|
| Kierunek odczytu | tylko sekwencyjny (przód) |
| Endianness liczb zmiennych | big-endian w obrębie uvarint/svarint (prefiks długości w MSB) |
| Liczby całkowite ≥ 16 bit | **varint** (uvarint / svarint), osobne warianty 16 / 32 / 64 |
| Float | stały rozmiar 4 / 8 B, kodowanie **sortowalne** + bajty w porządku big-endian na LE host |
| Ramki (`begin_frame` / `end_frame`) | wywoływane z `serialize`, backend **no-op** |
| `save_offset` | **no-op** (brak trailera indeksów) |
| `is_indexed()` | `false` |
| Grafy / cykle | tak (`write_pointer` / `write_rest`) |
| Ewolucja schematu | nie jest celem formatu; obie strony muszą znać ten sam układ typów |

---

## Mapowanie wywołań Writer → bajty

Logika typów jest w `serialize.hpp`. Poniżej semantyka backendu.

### Skalary

| Operacja | Encoding |
|----------|----------|
| `write_u8` | 1 bajt raw |
| `write_i8` | 1 bajt: `value ^ 0x80` (sortowalność względem unsigned byte order) |
| `write_u16` / `u32` / `u64` | uvarint16 / uvarint32 / uvarint64 |
| `write_i16` / `i32` / `i64` | svarint16 / svarint32 / svarint64 |
| `write_bool` | 1 bajt: `0` lub `1` |
| `write_f32` / `write_f64` | patrz [Float sortowalny](#float-sortowalny) |
| `write_length` | uvarint32 |
| `write_string` | uvarint32 `len` + `len` bajtów UTF-8 / raw |
| `write_binary` | uvarint32 `len` + `len` bajtów |

### Ramki i offsety

`begin_frame`, `end_frame`, `save_offset` — **nic nie zapisują**. Struktura wywołań istnieje pod wspólne `serialize` z formatem flat.

### Wskaźniki i typy

| Operacja | Encoding |
|----------|----------|
| `write_pointer` | svarint32 **delta** (patrz niżej); ciało odroczone |
| `write_rest` | dopisuje odroczone obiekty w kolejności kolejki |
| `write_typed` / message / exception | uvarint32 indeks rejestru + payload przez `serialize` |

---

## Uvarint

Bezznakowa liczba o zmiennej długości. Pierwszy bajt koduje długość w leading ones (bitach `1` od MSB), potem wartość w remaining bitach + kolejnych bajtach (big-endian).

### uvarint64 (i analogicznie mniejsze zakresy)

| Zakres wartości | Bajty | Prefiks (MSB) |
|-----------------|-------|----------------|
| `< 0x80` | 1 | `0………` |
| `< 0x4000` | 2 | `10……` |
| `< 0x200000` | 3 | `110…` |
| `< 0x10000000` | 4 | `1110` |
| `< 0x800000000` | 5 | `11110…` |
| … | … | … |
| pełne 64 bit | 9 | `11111111` + 8 B |

Dokładne progi i maski: implementacja w `stream_writer.hpp` (`write_uvar_int*`) oraz `circular_buffer` (`*_uvarint*`).

**uvarint32** — max 5 bajtów; **uvarint16** — max 3 bajty. Te same reguły prefiksu, przy overflow → błąd po stronie readera.

---

## Svarint

Znakowa liczba o zmiennej długości.

- Bit znaku: MSB pierwszego bajtu — `1` = nieujemna, `0` = ujemna (w implementacji stream).
- Długość: leading ones (dodatnie) lub leading zeros potem one (ujemne).
- Wartości ujemne kodowane przez bitową negację magnitude w polu danych (szczegóły w `write_svar_int*` / `read_svar_int*`).

Używane m.in. do `i16`/`i32`/`i64` oraz **pointer delta**.

---

## Float sortowalny

Cel: porządek leksykograficzny zakodowanych bajtów = porządek wartości float (z uwzględnieniem znaku), przydatne jako klucz sortujący.

Dla `f64` (analogicznie `f32` na 32 bitach):

1. Interpretuj bity IEEE jako `uint64_t` / `uint32_t`.
2. Jeśli znak ujemny (MSB = 1): `v = ~v`, w przeciwnym razie `v ^= sign_bit`.
3. Specjalny przypadek `+0` / reprezentacja zera → ustalona wartość (`0x8000…`).
4. Na hoście little-endian: `bswap` przed zapisem (w strumieniu bajty big-endian).

Odczyt: odwrotna transformacja.

---

## Układ złożonych typów (logiczny)

Mimo no-op ramek, kolejność bajtów wynika z `serialize`:

### String / binary

```text
[uvarint32 length][bytes…]
```

### Kontener (nie-binary)

```text
[uvarint32 length N]
[element₀]…[elementₙ₋₁]
```

(`begin_frame` / `end_frame` / `save_offset` bez efektu na bajty.)

### Mapa

```text
[uvarint32 length N]
[key₀][value₀] … [keyₙ₋₁][valueₙ₋₁]
```

### UDT (obiekt)

```text
[pole₀][pole₁]…   // kolejność: bazy, potem non-static data members (bez policy::ignore)
```

### `shared_ptr` / graf

W miejscu wystąpienia wskaźnika:

```text
[svarint32 delta]
```

| Delta | Znaczenie |
|-------|-----------|
| `0` | `nullptr` |
| `< 0` | już zapisany obiekt: indeks = `written_count + delta` |
| `> 0` | obiekt w kolejce later: indeks 1-based w `write_later_` |

Po głównej serializacji `write_rest()` dopisuje dla każdego odroczonego obiektu m.in. typed payload (`uvarint32 type_index` + ciało). Reader: `read_rest()` w lustrze.

Kolejność `register_serialize_typed_function<T>()` / deserialize **musi być identyczna** po obu stronach.

---

## Backendy

| Backend | Medium | Uwagi |
|---------|--------|--------|
| `stream_*` | `std::ostream` / `istream` | uvarint w C++ |
| `circular_buffer_*` | peek/poke + native uvarint w C API | ten sam layout; offset read/write po stronie callera |
| `debug_writer` | tekst | nie jest tym formatem — tylko dump wywołań |

---

## Kiedy używać

- Legacy RPC / transfer sekwencyjny (opt-in; domyślny wire → [flat-format.md](flat-format.md))
- Logi, mały rozmiar na przewodzie
- Klucze / rekordy sortowane bajtowo (float, i8) — albo rozważ [key-format.md](key-format.md)
- Grafy z cyklami (`shared_ptr`), także polimorficzne
- Gdy nie potrzeba odczytu w miejscu / random access do pól

Flat też wspiera polimorficzne grafy (`shared_ptr` + type index). Różnica to układ: flat ma per-frame size prefix, trailer checkpointów i type index **u32 LE** w sub-ramce; compressed ma **uvarint** type index i nie ma trailera ani prefiksu rozmiaru ramki. Patrz [flat-format.md](flat-format.md).

Domyślny RPC procesów i hot-path → [flat-format.md](flat-format.md).
