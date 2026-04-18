# MATHVM

Current `MATHVM` sketch for RIA.

Current status:

- The final `OS code` for `MATHVM` is `$80`.
- `MATHVM` is the only active math backend in the RIA firmware.
- The legacy code in `src/ria/api/mth.c` and `src/ria/api/mth.h` remains in the repository, but it is no longer linked into the firmware and is no longer dispatched by `main_api()`.
- `main_api()` has a dedicated dispatch path to `mathvm_api_op()`.
- `MATHVM` covers at least the same calculations as the old `mth`:
  `mul8/mul16/muls16/div16/sqrt32`, `fadd/fsub/fmul/fdiv/fsqrt/fsin/fcos/fatan2/fpow/flog/fexp/ftoi/itof`, `dadd/dmul/ddiv`
- `MATHVM` also includes vector and graphics opcodes:
  `M3V3L`, `A2P2L`, `SPR2L`
- Batch/XRAM is available through `MX_M3V3P2X`, which projects many `vec3` records in one call and writes packed `int16 x/y` results to `xram_out`.
- In the current implementation, `XRAM` is actively used only by `MX_M3V3P2X`. All other opcodes work on `locals`, the VM stack, and `XSTACK`.
- For `MX_M3V3P2X`, the XRAM sizes are:
  `xram_in = count * 12` bytes, `xram_out = count * 4` bytes, total `count * 16` bytes
- Example: a dodecahedron with `20` vertices needs `240` bytes of `xram_in` and `80` bytes of `xram_out`, for a total of `320` bytes.

Files:

- `mathvm.h`: shared ABI, frame header, opcodes, status codes, and limits
- `mathvm_host.c`: simple host-side frame builder
- `mathvm_client.h` / `mathvm_client.c`: on-target 6502-side caller that builds a frame and executes `OS $80`
  It now includes a higher-level batch API through `mx_client_call_batch()` and a specialized helper `mx_client_m3v3p2x()`.
- `mathvm_ria.c`: RIA-side interpreter sketch
- `mathvm_host_test.c`: minimal host-side tests that build example frames
- `examples/`: minimal `cc65` / `ca65` examples, benchmarks, and batch/XRAM demos for Picocomputer 6502

Minimal host-side tests:

- `M3V3L 0,9 ; RET 3`
- `SPR2L 0,6,0x02 ; RET 4` for `bbox`
- `SPR2L 0,6,0x01 ; RET 8` for 4 corners
- negative `v1` tests: `bad magic`, `bad header`, `unsupported flag`, `stack underflow`, `bad local`
- execution tests for the opcode set that replaces the old `mth`

On-target examples in `examples/`:

- `mathvm_mathcop_cc65.c`: `MATHVM` compatibility test modeled after the old `mathcop`
- `mathvm_batch_benchmark_cc65.c`: `pure CPU` vs scalar RPC vs single-call `MATHVM` benchmark
- `mathvm_dodecahedron_batch_cc65.c`: dodecahedron rendered with one `MATHVM` call per frame
- `mathvm_dodecahedron_debug_cc65.c`: batch/XRAM debug tool for the dodecahedron path

Example host-side test build:

```sh
cc -std=c11 -Wall -Wextra -Wsign-compare -Isrc/ria -Isrc \
  src/mathvm/mathvm_host.c src/mathvm/mathvm_host_test.c \
  -o /tmp/mathvm_host_test
```

Example run:

```sh
/tmp/mathvm_host_test
```

6502 call sequence for `OS $80`:

1. Build one linear frame: `[header][locals][bytecode]`.
2. Push it to `$FFEC` from the last byte down to the first byte.
3. Write `$80` to `$FFEF`.
4. Execute `JSR $FFF1` and wait for `BUSY` to clear.
5. Read `A = status`, `X = output word count`.
6. Read `4*X` bytes from `$FFEC` as little-endian words.

Important:

- The `XSTACK` result must be copied to RAM immediately after `JSR $FFF1`, before `printf()` or any other OS call.
- For batch/XRAM paths, the caller reads results directly from `xram_out`, not from `XSTACK`.

## Friendlier Batch API

Batch calls no longer need to hand-assemble the full frame header in every caller.

The client layer now exposes:

- `mx_client_call_batch()`: generic helper for one batch frame
- `mx_client_m3v3p2x()`: specialized helper for the common
  `mat3 * vec3 -> projected int16 x/y in XRAM` batch path
- `mx_client_xram_write_vec3i_array()`: writes normal `int16 x/y/z` input records to `xram_in`
- `mx_client_xram_read_point2i_array()`: reads packed `int16 x/y` output records from `xram_out`
- `mx_client_m3v3p2x_q8_8()`: safe helper that takes a `Q8.8` matrix and integer camera values
- `mx_client_m3v3p2x_yrot30()`: angle-based helper for the common `y-rotation + 30-degree tilt` projection used by the demos
- `mx_client_project_vec3i_batch_yrot30()`: one-call helper that uploads `vec3[]`, runs the batch projection, and reads back `point2i[]`

The safe client API exposes integer batch data types:

- `mx_vec3i_t`: `int16_t x, y, z`
- `mx_point2i_t`: `int16_t x, y`

`mx_client_call_batch()` takes an `mx_client_batch_desc_t` with:

- `flags`
- `xram_in`, `xram_out`
- `count`
- `locals`, `local_words`
- `program`, `prog_len`
- `out_words`
- `stack_words`

This keeps batch callers focused on:

- filling locals
- writing normal integer input records to `xram_in`
- building only the actual bytecode program
- reading normal `int16 x/y` output records from `xram_out`

instead of rebuilding the full `MATHVM` frame header every time.

For the most common demo projection path, callers can go one step further and use:

- `mx_client_project_vec3i_batch_yrot30(angle_deg, persp_d, screen_cx, screen_cy, xram_base, vecs, points, count)`

This helper uses one contiguous XRAM scratch region:

- input records start at `xram_base`
- output records start at `xram_base + count * 12`

So the caller only supplies:

- an integer angle in degrees
- integer camera values
- a base XRAM address
- `mx_vec3i_t[]` input
- `mx_point2i_t[]` output
- `count`

This keeps the public `cc65` caller side free of `float`. Conversion to `float32`
for the VM happens inside `mathvm_client`.

## Memory Cost

Current `MATHVM` memory cost in RIA:

- Additional global `.bss/.data` cost: `0 B`
- Per-call ARM stack cost inside `mathvm_api_op()`: about `874 B` minimum
- Existing shared `XSTACK` usage:
  up to `368 B` for the input frame and up to `32 B` for returned output words
- Optional `XRAM` usage for `MX_M3V3P2X`:
  `count * 16 B`

Breakdown:

- `mx_vm_t`: `504 B`
- local `frame[MX_MAX_FRAME]`: `368 B`
- `frame_len`: `2 B`

So the practical working-set cost of one `mathvm_api_op()` call is roughly `0.9 KB` of ARM stack, plus any shared `XSTACK` and optional `XRAM` usage.

On the code-size side, the linked `mathvm_api_op` symbol in the current firmware build is about `0x100c` bytes (`4108 B`) before accounting for additional math library code pulled in by used float operations.

This is still a `v1` sketch, not a final production API.

## Production Readiness Checklist

- [x] Finalize the `OS code`: `$80`
- [ ] Freeze the frame ABI: `flags`, `count`, `xram_in`, `xram_out`, limits, and output format
  Partial: single-shot `v1` and batch `MX_M3V3P2X` already work, but the ABI is not yet formally frozen.
- [ ] Sync `mathvm_notes.md` with the actually implemented `MATHVM` scope
  Partial: the notes describe the `v1` core, but they no longer fully match batch/XRAM support.
- [ ] Add full regression tests for batch/XRAM, including `MX_M3V3P2X`
  Partial: working examples and on-target debug tools exist, but there is no full regression suite yet.
- [ ] Add validation tests for `count=0`, bad `XRAM` flags, and XRAM range overflow
  Partial: negative tests exist for core `v1`, but not yet for the full batch/XRAM path.
- [ ] Add regression tests for the `int16 x/y` output format across firmware and `cc65` callers
  Partial: a real bug was found and fixed, but there is no permanent regression test yet.
- [ ] Finalize and document the numeric error policy
  Partial: basic status codes and `int16` packing exist, but the policy is not final yet.
- [ ] Have at least one real application caller outside of examples
- [ ] Unify the source of truth for `mathvm.h` and `mathvm_client.*` so examples cannot drift from the main code
  Partial: the copies are manually kept in sync, but they are still duplicates.
- [ ] Run a long on-target soak test without artifacts or result regressions
  Partial: positive tests and several demos work on hardware, but there has been no long soak test yet.
- [ ] Update the README from a `v1` sketch description to a stable supported API description
