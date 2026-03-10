# TODO: Refactoring RPC na message-based communication

## Zrobione

- [x] `socket_message_channel.hpp` - bidirectional message layer z payload support
- [x] `rpc.hpp` - usunięte `binary arguments/result`, `function_id` zmieniony na `uint64_t`
- [x] `socket_rpc_client.hpp` - przebudowany na `socket_message_channel`
- [x] `socket_rpc_server.hpp` - przebudowany na `socket_message_channel` (redukcja z ~410 do ~170 linii)
- [x] `rpc_client.hpp` - używa `serialize(rpc_request/rpc_response)` zamiast ręcznych write/read
- [x] `rpc_server.hpp` - używa `serialize(rpc_request/rpc_response/rpc_error)`, helpery `send_response`/`send_error`

## Dalsze kroki

### 1. `on_unknown_message` handler w RPC
- Dodać w `socket_rpc_client` i `socket_rpc_server` obsługę wiadomości spoza protokołu RPC
- To pozwoli na mieszanie RPC z dowolnymi wiadomościami użytkownika na tym samym sockecie
- Handler na poziomie RPC (nie message channel) - channel rzuca błąd dla nieznanych typów
