# MATHVM

Tymczasowy szkic `MATHVM` dla RIA.

Aktualny stan:

- `OS code` dla `MATHVM` to tymczasowo `$80`
- stary kod w `mth.c` i `mth.h` zostaje w repo bez zmian
- `main_api()` ma już osobny dispatch do `mathvm_api_op()`
- `MATHVM v1` jest małym VM z kilkoma "grubymi" opcode'ami, głównie `M3V3L`, `A2P2L`, `SPR2L`
- `MATHVM` ma też już opcode'y pokrywające co najmniej obliczenia starego `mth`:
  `mul8/mul16/muls16/div16/sqrt32`, `fatan2/fpow/flog/fexp/ftoi/itof`, `dadd/dmul/ddiv`

Pliki:

- `mathvm.h`: wspólne ABI, nagłówek ramki, opcode'y, statusy i limity
- `mathvm_host.c`: prosty builder ramek po stronie hosta
- `mathvm_client.h` / `mathvm_client.c`: on-target caller po stronie 6502, który buduje ramkę i wykonuje `OS $80`
- `mathvm_ria.c`: szkic interpretera po stronie RIA
- `mathvm_host_test.c`: minimalne testy host-side budujące przykładowe ramki

Minimalne testy host-side:

- `M3V3L 0,9 ; RET 3`
- `SPR2L 0,6,0x02 ; RET 4` dla `bbox`
- `SPR2L 0,6,0x01 ; RET 8` dla 4 rogów
- testy negatywne `v1`: `bad magic`, `bad header`, `unsupported flag`, `stack underflow`, `bad local`

Przykładowy build testu host-side:

```sh
cc -std=c11 -Wall -Wextra -Wsign-compare -Isrc/ria -Isrc \
  src/mathvm/mathvm_host.c src/mathvm/mathvm_host_test.c \
  -o /tmp/mathvm_host_test
```

Przykładowe uruchomienie:

```sh
/tmp/mathvm_host_test
```

Sekwencja wywołania z 6502 dla `OS $80`:

1. Zbuduj liniową ramkę `[header][locals][bytecode]`.
2. Wypchnij ją na `$FFEC` od ostatniego bajtu do pierwszego.
3. Zapisz `$80` do `$FFEF`.
4. Wykonaj `JSR $FFF1` i czekaj aż `BUSY` opadnie.
5. Odczytaj `A = status`, `X = liczba słów wyniku`.
6. Odczytaj `4*X` bajtów z `$FFEC` jako little-endian `wordy`.

To jest nadal szkic `v1`, nie finalne API produkcyjne.
