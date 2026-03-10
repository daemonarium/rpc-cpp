# TODO: Refactoring RPC na message-based communication

Kolejne kroki po dodaniu `socket_message_channel` (z obsługą payload):

## 1. Aktualizacja `rpc.hpp`
- Usunąć pole `binary arguments` z `rpc_request` (argumenty to payload po nagłówku)
- Usunąć pole `binary result` z `rpc_response` (wynik to payload po nagłówku)
- `rpc_request` staje się nagłówkiem: `{ request_id, function_id }`
- `rpc_response` staje się nagłówkiem: `{ request_id, success }`
- `rpc_error` bez zmian: `{ request_id, success=false, error }`
- Zmienić `rpc_request.function_id` z `uint16_t` na `uint64_t` (spójność z resztą kodu)

## 2. Refactoring `socket_rpc_client`
- Zamienić wewnętrzny reader/writer/socket na `socket_message_channel`
- Zarejestrować `rpc_request`, `rpc_response`, `rpc_error` jako typy wiadomości
- `call()` i `call_method()`:
  - `begin_send(rpc_request{request_id, function_id})`
  - Serializują argumenty bezpośrednio do writera jako payload
  - `commit_send()`
- Handler dla `rpc_response` z payloadem:
  - Deserializuje wynik z payloadu readera (zero kopii)
  - Resolve'uje promise
- Handler dla `rpc_error`:
  - Reject'uje promise z wiadomością błędu
- `handle_data()` deleguje do `channel_.handle_data()`
- Udostępnić `register_message_type<T>()`, `on_message<T>()`, `send_message<T>()` dla typów użytkownika

## 3. Refactoring `socket_rpc_server`
- Analogicznie - `socket_message_channel` zamiast surowego protokołu
- Handler dla `rpc_request` z payloadem:
  - Czyta argumenty bezpośrednio z readera (zero kopii)
  - Wywołuje funkcję z `functions_[function_id]`
- `process_call` warianty:
  - `begin_send(rpc_response{request_id, true})`
  - Serializują wynik jako payload
  - `commit_send()`
  - Lub wysyłają `rpc_error` w przypadku błędu
- Drastyczna redukcja kodu (obecne ~400 linii powtórzonego framingu -> ~100 linii)

## 4. `on_unknown_message` handler w RPC
- Dodać w `socket_rpc_client` i `socket_rpc_server` obsługę wiadomości spoza protokołu RPC
- To pozwoli na mieszanie RPC z dowolnymi wiadomościami użytkownika na tym samym sockecie
- Handler na poziomie RPC (nie message channel) - channel rzuca błąd dla nieznanych typów

## 5. Refactoring generycznego `rpc_client`/`rpc_server`
- Dostosować do serializacji wiadomości (write_message/read_message)
- Wpływa na testy i infrastrukturę testową
