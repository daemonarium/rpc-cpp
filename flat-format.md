# Format flat (indexed / in-place)

Format zoptymalizowany pod **szybki odczyt** (w tym odczyt w miejscu i dostęp swobodny do pól) oraz **wydajny jednoprzebiegowy zapis**. Współdzieli kontrakt `Writer` / `Reader` oraz `serialize` / `deserialize` z formatem skompresowanym; inny jest układ bajtów i znaczenie `begin_frame` / `end_frame` / `save_offset`.

Status: **specyfikacja** (implementacja reader/writer flat — do zrobienia). Haki API są już w kontrakcie i w `serialize.hpp`.

Powiązane: [README.md](README.md), [compressed-format.md](compressed-format.md).

---

## Cele i założenia

| Cel | Decyzja |
|-----|---------|
| Max wydajność odczytu | fixed-width LE, sparse indeks checkpointów, odczyt bez pełnej materializacji (proxy — osobna decyzja UX) |
| Dobra wydajność zapisu | jeden przebieg: najpierw payload, na końcu ramki trailer z offsetami |
| Random access | tak, w ramach ciągłego bufora / wycinka ramki |
| Grafy / cykle `shared_ptr` | **poza scope v1** (użyj formatu skompresowanego) |
| Ewolucja schematu | **poza scope** — writer i reader znają ten sam `T` |
| Walidacja / safety | nie jest celem; format ma być szybki, nie „łatwy do bezpiecznego parse” |
| Alignment | nie wymagane; dopuszczalny unaligned load u16/u32 |

---

## Właściwości

| Cecha | Wartość |
|-------|---------|
| Endianness | **little-endian** dla wszystkich fixed integers / float IEEE |
| Liczby całkowite | **fixed**: 1 / 2 / 4 / 8 bajtów (zgodne z szerokością typu w `type_registry`) |
| Float | surowe bajty IEEE754 LE (bez transformacji sortowalnej) |
| String / binary length, `write_length` | **u32 LE** (fixed), nie varint |
| Indeks | sparse checkpointy po polach **zmiennej długości** (`save_offset`) |
| Trailing `N` (liczba offsetów) | **nie** — `K` ze schematu (UDT) lub z `write_length` + reguł kontenera |
| `is_indexed()` | `true` dla flat readera |
| Medium | ciągły bufor (`buffer_reader`); stream/circular = zapis bloba / odczyt po zebraniu ciągłego wycinka |

Format flat i skompresowany **nie są** wzajemnie kompatybilne bajtowo.

---

## Ramka (frame)

Każdy UDT, kontener i mapa jest otoczony ramką:

```text
begin_frame()
  … payload …
end_frame()     → dopisuje trailer
save_offset()   → (u parenta) rejestruje checkpoint po domknięciu tej ramki / pola variable
```

### Layout ramki

```text
[ start = pozycja begin_frame ]
  payload: pola / elementy sekwencyjnie
[ trailer ]
  [format : u8]           // szerokość offsetów (patrz kompresja); na początku trailera
  [cp₀][cp₁]…[cpₖ₋₁]      // K checkpointów, każdy szerokości W bajtów, LE
[ end ]
```

- Offsety (`cpᵢ`) są **względem `start` ramki**.
- `K` nie jest zapisane w trailerze: reader zna `K` ze schematu lub z semantyki kontenera.
- Pusty trailer przy `K = 0`: wystarczy sam `format` (lub umowa „brak trailera gdy K=0” — implementacja powinna wybrać jedną regułę i trzymać się jej; rekomendacja: przy `K=0` nie zapisywać trailera w ogóle).

### Semantyka `save_offset`

Wołane przez `serialize` **po** zapisaniu wartości o zmiennej długości:

- `string`, `binary`
- kontener / mapa / UDT (po ich `end_frame`)

**Nie** wołane po skalarach (fixed int/float/bool/enum).

Każde `save_offset` w kontekście bieżącej ramki dopisuje do jej listy:

```text
cp = current_write_pos - frame_start
```

czyli pozycję **zaraz za** domkniętym polem variable = start następnych bajtów (kolejne pole albo początek trailera / stały ogon).

### Co daje sparse indeks

Nie indeksujemy każdego pola ani każdego elementu tablicy skalarów.

| Przypadek | Checkpointy |
|-----------|-------------|
| UDT z polami fixed + variable | jeden `cp` na każde pole variable |
| `vector<uint32_t>` (elementy fixed) | **0** wewnątrz wektora; dostęp: `base + i * 4` po `write_length` |
| `vector<string>` / `vector<Person>` | jeden `cp` po każdym elemencie |
| `binary` / `vector<uint8_t>` jako blob | traktowane jako jedno pole variable (length + bytes), nie per-bajt |

Dzięki temu gęste tablice małych intów nie puchną od indeksu.

---

## Wyliczanie pozycji pola (seek)

Dane wejściowe: schemat typu (kolejność pól, fixed vs variable, `fixed_size`), tablica `cp[0..K)` dla ramki.

Start pola o indeksie `F`:

```text
pos = 0
c = 0
dla j = 0 .. F-1:
  jeśli pole j jest variable:
    pos = cp[c++]
  w przeciwnym razie:
    pos += fixed_size(j)
→ pos = offset startu pola F względem startu ramki
```

- Rozmiar pola variable: `cp_after - start` (checkpoint zarejestrowany po tym polu).
- Nested UDT / kontener: wycinek `[start, cp_after)` to pełna podramka (payload + jej własny trailer).
- Pola fixed po ostatnim variable: ich pozycje = ostatni `cp` + suma rozmiarów fixed aż do danego pola; trailer zaczyna się dopiero po tym stałym ogonie.

`enter_indexed_frame` / seek: ustaw bieżący zakres na wyliczony wycinek childa i (dla flat) odczytaj jego trailer od końca zakresu.

---

## Kompresja szerokości offsetów

Przy `end_frame` znany jest `max(cp)`. Wybór szerokości `W`:

| `max(cp)` | `W` |
|-----------|-----|
| `< 256` | 1 (u8) |
| `< 65536` | 2 (u16 LE) |
| inaczej | 4 (u32 LE) |

### Bajt `format` (na początku trailera)

Propozycja bitów (do zamrożenia przy implementacji):

```text
bits 0–1: szerokość offsetu  (0 → 1 B, 1 → 2 B, 2 → 4 B, 3 → reserved)
bits 2–7: reserved (0)
```

### Lokalizacja trailera

- **Sekwencyjny odczyt:** po przeczytaniu wszystkich pól kursor stoi na początku trailera → czyta `format`, pomija `K * W` bajtów w `exit_frame`.
- **Flat / od końca:** znane `[start, end)` ramki oraz `K` ze schematu. Aby uniknąć cyklu „potrzebuję `W` żeby znaleźć trailer, a `W` jest w trailerze”, jedna z opcji:
  1. **duplikat:** ostatni bajt ramki = kopia `format` (flat czyta `end-1`, liczy `trailer_size = 1 + K*W`, właściwy trailer = `[end - trailer_size, end)` z `format` też na początku), albo
  2. stałe `W=4` w v1 (prostsze; kompresję dodać później), albo
  3. prefiks rozmiaru trailera.

Rekomendacja v1 implementacji: **najpierw zawsze u32** (`W=4`, opcjonalnie stały `format`), potem włączyć kompresję + regułę lokalizacji od końca.

`K` przy UDT = liczba memberów variable (compile-time / reflection).  
`K` przy kontenerze elementów variable = `N` z `write_length`.  
`K` przy kontenerze elementów fixed = `0`.

---

## Kodowanie wartości (payload)

| Typ | Layout |
|-----|--------|
| `u8` / `i8` | 1 B raw (bez XOR sortującego) |
| `u16` / `i16` | 2 B LE |
| `u32` / `i32` | 4 B LE |
| `u64` / `i64` | 8 B LE |
| `bool` | 1 B: `0` / `1` |
| `f32` / `f64` | IEEE754 LE |
| `enum` | jak ustalony integer width (obecnie w `serialize` jako u64 — do ewentualnego dopięcia pod flat) |
| `string` | `u32 LE length` + bytes |
| `binary` | `u32 LE length` + bytes |
| kontener | `u32 LE length N` + elementy (+ trailer ramki) |
| mapa | `u32 LE length N` + pary key/value (+ trailer) |
| UDT | pola w kolejności reflection (+ trailer) |

`write_length` **zostaje** (redundancja względem wiedzy o `N` przy seeku jest zamierzona — upraszcza sekwencyjny odczyt i kontenery).

---

## Przebieg zapisu (writer)

Stos ramek. Każda ramka: `start`, `vector` checkpointów.

1. `begin_frame` — push; zapamiętaj `start = pos`.
2. Zapis pól / elementów jak w `serialize`.
3. Przy nested `begin_frame` — nowa ramka na stosie (parent nie rejestruje offsetu tutaj).
4. `save_offset` — `current_frame.cps.push(pos - start)`.
5. `end_frame` — wybierz `W` / `format`; zapisz trailer; pop. Jeśli parent oczekuje `save_offset` po tym UDT/kontenerze, wywołuje je `serialize` zaraz potem.

Brak patchowania wstecz w payloadzie — tylko append trailera. Odpowiednie dla budowania w `vector<uint8_t>` i późniejszego poke do circular / write do stream.

---

## Przebieg odczytu

### Sekwencyjny (`deserialize` + flat-aware reader)

1. `enter_frame` — push zakresu / licznika.
2. Czytaj pola jak dziś (`read_*`, nested frames).
3. Po variable polach reader powinien zliczać checkpointy (mirror `save_offset` **lub** `K` wyłącznie ze schematu w `exit_frame`).
4. `exit_frame` — pomiń trailer (`format` + `K*W`).

Wymaga poprawienia `deserialize`, żeby konsekwentnie wołać `enter_frame` / `exit_frame` (i ewentualnie notować variable fields). Obecne no-op w stream readerze nie wystarczą dla flat.

### Random access / indexed

1. Znasz `[start, end)` (root = cały komunikat; child = z `cp` parenta).
2. Odczytaj trailer (reguła z sekcji kompresji) → span `cp[]` **w miejscu** (bez kopii).
3. Wylicz offset pola ze schematu + `cp`.
4. Czytaj fixed w miejscu; dla variable ustaw podzakres; dla nested powtórz.

`is_indexed() == true`; `enter_indexed_frame(id)` — id = indeks logiczny childa / slotu zgodny z umową implementacji (np. indeks pola variable lub indeks elementu kontenera).

---

## Przykład logiczny

Typ:

```text
struct Skill {
  string name;   // variable → save_offset
  int32  level;  // fixed
};

struct Person {
  string name;           // variable
  int32  age;            // fixed
  Occupation occupation; // UDT variable (własna ramka)
  vector<Skill> skills;  // kontener variable
};
```

Ramka `Person` (schematycznie):

```text
[ name: u32 len + bytes ]           → cp0  (za name)
[ age: i32 LE ]
[ Occupation frame … ]              → cp1  (za occupation)
[ skills: u32 N + Skill frames… ]   → cp2  (za skills)
[ format ][ cp0 ][ cp1 ][ cp2 ]     // K=3
```

Seek `age`: `pos = cp0` (pierwsze pole to variable name; age zaczyna się na `cp0`).  
Seek `skills`: start = `cp1 + sizeof(Occupation nie — Occupation jest variable, więc start skills = cp1`.  
Element `skills[i]` (variable): po `write_length`, checkpointy **wewnątrz** ramki `skills`.

Ramka `vector<int32_t>`: payload `u32 N` + `N * 4` bajtów; `K=0`; brak trailera (lub pusty); `skills[i]` analogicznie przez stride.

---

## Backendy (plan)

| Komponent | Rola |
|-----------|------|
| `flat_buffer_writer` | budowa w ciągłym buforze (źródło prawdy layoutu) |
| `flat_buffer_reader` | indexed + sekwencyjny na `span` |
| stream / circular adaptery | zapis gotowego bloba; odczyt po uzyskaniu ciągłego regionu wiadomości |
| `debug_writer` | bez zmian (dump wywołań); ewentualnie osobny dump offsetów później |

Circular buffer z wrap-around: zero-copy flat tylko gdy wiadomość leży w **jednym ciągłym** segmencie (albo kopia do scratch).

---

## Świadomie poza v1

- Ewolucja schematu / opcjonalne pola / field id
- Współdzielone vtable jak FlatBuffers
- Grafy `shared_ptr` / `write_rest` w flat
- Rygorystyczna walidacja OOB
- Wymuszony alignment

---

## Checklist implementacji (orientacyjny)

1. Writer: stos ramek + `save_offset` + trailer u32.
2. Reader: skip trailera w `exit_frame`; round-trip przez `deserialize`.
3. Seek: `field_offset(schema, cps, i)` + testy.
4. Kompresja `W` + reguła `format` od końca.
5. Adaptery stream/circular.
6. UX proxy / `flat_view<T>` — osobna decyzja projektowa.
