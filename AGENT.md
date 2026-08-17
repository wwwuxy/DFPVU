# DFPVU Coding Principles

DFPVU is a parameterizable vector processing unit written in Chisel. It uses
Posit as its primary internal numeric representation and supports multiple
floating-point input, output, and conversion formats.

## Repository Layout

- `src/main/scala/pvu/`: Chisel top level, format conversion, arithmetic, and
  supporting hardware modules.
- `src/main/resources/`: Verilog resources instantiated by the Chisel design.
- `csrc/`: Verilator C++ drivers selected through `config.h`.
- `test_src/`: binary inputs and expected results for C++ regression tests.
- `vsrc/`: generated SystemVerilog. Treat it as build output unless a task
  explicitly requires checking in generated RTL.
- `Kconfig`, `.config`, and `config.h`: regression-test selection and its
  generated C/C++ configuration.
- `makefile`, `build.sbt`, `build.sc`, and `project/`: build definitions.

## When to Ask

When a change depends on a Posit standard, a floating-point corner case, Chisel or
CIRCT semantics, or undocumented target-hardware behavior:

- Ask for the relevant specification or source document if it is not available
  in the repository.
- Do not invent encodings, rounding behavior, exception behavior, timing, or
  hardware details.
- State any implementation-derived assumption and keep it local until it is
  confirmed by a specification or test evidence.

## Required Skills

- When creating or editing draw.io diagrams, invoke `drawio-skill`.
- When writing or updating the project README, invoke
  `readme-blueprint-generator`.
- When writing code implementation documentation, invoke
  `research-writing-assistant:using-research-writing`.

## Testing Policy

- Use Verilator regression with the C++ drivers in `csrc/` as the project's only
  functional test path.
- Do not add, maintain, or run Scala tests or ChiselTest. sbt and Mill are used
  to compile and elaborate the Chisel design, not to execute tests.
- Select regression cases through `.config` and `config.h`, and keep their exact
  input and expected-output data in `test_src/`.

## Design Preferences

1. **Preserve canonical representations**: Keep raw Posit and floating-point bit
   encodings at module and test boundaries. Clearly distinguish encoded values,
   decoded sign/exponent/fraction fields, intermediate arithmetic forms, and
   packed storage.

2. **Make widths and signedness explicit**: Preserve the intended Chisel
   `UInt`/`SInt` interpretation, literal width, extension rule, truncation point,
   and shift behavior. Do not rely on inferred widths when a result affects a
   numeric or interface boundary.

3. **Avoid false generalization**: Keep logic for one operation, format, width,
   or vector configuration local until at least two real production users need
   the same abstraction.

4. **Minimize public abstractions**: Add a new public module, bundle, parameter,
   or top-level port only when no existing module fits and multiple production
   consumers require it.

5. **Confirm boundary changes**: Before changing top-level I/O, opcode or mode
   meanings, Posit/float encodings, runtime width semantics, vector-lane
   behavior, test-vector formats, or source/generated-file ownership, describe
   the data flow and ownership and wait for user confirmation.

6. **Preserve parameter boundaries**: Distinguish elaboration-time limits such
   as `MAX_POSIT_WIDTH`, `MAX_VECTOR_SIZE`, `MAX_ALIGN_WIDTH`, `ES`, and
   `FLOAT_MODE` from runtime selectors such as `src_posit_width`,
   `dst_posit_width`, `vector_size`, and `float_mode`. Do not silently turn one
   kind into the other.

7. **Keep inactive behavior deterministic**: When working on variable vector
   sizes or formats, preserve the defined behavior of inactive lanes and unused
   output bits. Initialize combinational outputs and wires explicitly and avoid
   accidental latches or partially driven aggregates.

8. **Simplify before completion**: Remove unnecessary modules, wrappers,
   intermediate representations, aliases, parameters, and generated files
   before reporting completion.

9. **Keep code concise and readable**: Do not add dead code, no-op logic,
   redundant assignments, speculative configurability, or comments that merely
   restate the code.

## Project Constraints

1. Construct Posit values, floating-point values, low-precision values, masks, and
   golden results from exact bit patterns. Do not derive them by converting or
   truncating host-language `Float` or `Double` values.

2. Use `BigInt` or explicitly sized Chisel literals for values that may exceed
   signed host-integer ranges. Give hardware constants an explicit width where
   inference could change behavior.

3. Preserve all special-value and boundary behavior, including zero, NaR/NaN,
   infinity where applicable, minimum and maximum encodings, sign transitions,
   rounding ties, overflow, and underflow. Do not infer one format's behavior
   from another format.

4. Treat bit positions and slice endpoints as inclusive Chisel indices. Name
   local constants for non-obvious widths, indices, opcodes, modes, and limits;
   add comments for format encodings, packed layouts, arithmetic alignment, and
   critical normalization or rounding logic.

5. Keep the top-level operation and format controls compatible with
   `PvuTop.scala` and the C++ drivers. Update the top-level comments, drivers,
   tests, and documentation together when an approved interface change is made.

6. Keep `test_src/` files byte-for-byte reproducible. Document byte order,
   element width, vector ordering, and generation method before introducing or
   changing a binary test-vector format.

7. Do not hand-edit generated SystemVerilog in `vsrc/` to implement a source
   change. Modify Chisel or the relevant resource in `src/`, regenerate RTL, and
   review the generated diff.

8. Keep sbt and Mill version differences visible. Do not assume a behavior
   validated with one build definition is automatically validated by the other.

9. Do not add unrelated formatting, refactoring, generated artifacts, or
   dependency updates to a focused change.

## Validation

After code changes, run the smallest relevant checks and then the applicable
end-to-end flow:

1. For Chisel source or elaboration changes, run `make verilog` and confirm that
   elaboration and SystemVerilog emission succeed.
2. For a selected Verilator regression, run `make config.h` after changing
   `.config`, then run `make run`. Use `make menuconfig` only when interactive
   case selection is needed.
3. Run the affected operation, conversion, comparison, or truncation tests with
   exact encoded inputs and expected outputs. Cover changed width and vector-size
   boundaries.
4. Review generated RTL and test artifacts deliberately. Do not commit
   `obj_dir/`, waveforms, `config.h`, or regenerated `vsrc/` content unless the
   task explicitly requires them.
5. Update affected architecture, interface, numeric-format, test-vector, and
   behavior documentation in the same change.
6. Report every validation command run and its relevant output. Clearly identify
   checks that were skipped, unavailable, or failed.
