# Type registry blob (rpc_description)

Serialized `type_registry` / `TypeRegistry` as returned by WASM `rpc_description` and stored as `registry.<pid>.<sock>.bin`.

## Layout

```text
[envelope u8][compressed type_registry body…]
```

- **Body** is always **compressed** stream encoding (`stream_writer` / TS `Writer`). Parsed once; cheap relative to repeated data decode.
- **Envelope** describes the encoding of **data** that this registry describes (messages, rows, index keys) — **not** the encoding of the registry body itself.

### Envelope `u8` (v1)

| Bits | Meaning |
|------|---------|
| **7** | `1` = envelope present; `0` = **legacy** blob (no prefix; entire buffer is body) |
| **4–6** | reserved (future registry-body version/format); must be `0` today |
| **0–3** | `serialization_format` |

### `serialization_format` (nibble)

| Value | Name | Typical use |
|-------|------|-------------|
| `0` | `compressed` | RPC / stream ([compressed-format.md](compressed-format.md)) |
| `1` | `flat` | hot-path / flat message channel ([flat-format.md](flat-format.md)) |
| `2` | `key` | DB index keys ([key-format.md](key-format.md)) |

Examples: compressed `0x80`, flat `0x81`, key `0x82`.

### Legacy

Blobs whose first byte has bit7 clear are treated as **pre-envelope**: body = whole buffer, data format = **compressed**.

Unknown reserved bits or format nibble ∉ `{0,1,2}` → deserialize error.

## API

| Language | Types / helpers |
|----------|-----------------|
| C++ | `serialization_format`, `serialize_type_registry_blob` / `deserialize_type_registry_blob` ([serialization_format.hpp](src/serialization/serialization_format.hpp), [type_registry_blob.hpp](src/serialization/type_registry_blob.hpp)); `type_registry::data_format()` |
| TS | `SerializationFormat`, `serializeTypeRegistry` / `deserializeTypeRegistry`; `TypeRegistry.dataFormat` |

Default on a new registry: **compressed**. Set `flat` / `key` explicitly when the container uses that data encoding.
