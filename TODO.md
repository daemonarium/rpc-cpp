# TODO: Refactoring RPC na message-based communication

Kolejne kroki po dodaniu `socket_message_channel`:

## 1. `binary_serialization.hpp`
Utility do serializacji/deserializacji do `std::vector<uint8_t>` (binary blob).
Potrzebne do pakowania argumentów RPC do pola `rpc_request.arguments`
i wyników do `rpc_response.result`.
Implementacja: `stream_writer` + `ostringstream` / `stream_reader` + `istringstream`.

## 2. Refactoring `socket_rpc_client`
- Zamienić wewnętrzny reader/writer/socket na `socket_message_channel`
- Zarejestrować `rpc_request`, `rpc_response`, `rpc_error` jako typy wiadomości
- Handler dla `rpc_response` - deserializuje wynik z `binary result` i resolve'uje promise
- Handler dla `rpc_error` - reject'uje promise z wiadomością błędu
- `call()` i `call_method()` tworzą `rpc_request` z argumentami serializowanymi do `binary arguments`
- `handle_data()` deleguje do `channel_.handle_data()`
- Udostępnić `register_message_type<T>()`, `on_message<T>()`, `send_message<T>()` dla typów użytkownika

## 3. Refactoring `socket_rpc_server`
- Analogicznie - `socket_message_channel` zamiast surowego protokołu
- Handler dla `rpc_request` - tworzy tymczasowy `circular_buffer` z `req->arguments`, deserializuje argumenty, wywołuje funkcję
- `process_call` warianty wysyłają `rpc_response`/`rpc_error` przez kanał zamiast ręcznego pisania do bufora
- Drastyczna redukcja kodu (obecne ~400 linii powtórzonego framingu -> ~100 linii)

## 4. `on_unknown_message` handler w RPC
- Dodać w `socket_rpc_client` i `socket_rpc_server` obsługę wiadomości spoza protokołu RPC
- To pozwoli na mieszanie RPC z dowolnymi wiadomościami użytkownika na tym samym sockecie
- Handler na poziomie RPC (nie message channel) - channel rzuca błąd dla nieznanych typów

## 5. Refactoring generycznego `rpc_client`/`rpc_server`
- Dostosować do serializacji wiadomości (write_message/read_message)
- Wpływa na testy i infrastrukturę testową

## 6. Drobne poprawki
- Zmienić `rpc_request.function_id` z `uint16_t` na `uint64_t` (spójność z resztą kodu, obecny surowy protokół używa uint64_t)
