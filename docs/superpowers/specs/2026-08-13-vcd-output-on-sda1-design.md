# VCD Output on `/dev/sda1` Design

## Goal

Store both Verilator waveform files on the filesystem mounted from `/dev/sda1`
instead of the project filesystem backed by NVMe. In this environment,
`/dev/sda1` is exposed at `/home`, so the default waveform directory is
`/home/DFPVU-waveforms`.

## Scope

The change is limited to the root `makefile`. Existing C++ drivers keep their
canonical relative filenames:

- ResNet regressions write `waveform.vcd`.
- The sample test writes `pvu_top_wave.vcd`.

No Chisel, generated RTL, C++ test behavior, or test-selection schema changes.

## Makefile Design

Define `VCD_DIR ?= /home/DFPVU-waveforms` so callers can override the location.
A phony preparation target runs before `run` and performs the following steps:

1. Create `$(VCD_DIR)` if it does not exist.
2. For each supported VCD filename, migrate an existing regular file from the
   repository root when the destination does not already exist.
3. Stop with an explicit error when both the repository and destination contain
   regular files with the same name; do not overwrite either file.
4. Create or refresh repository-local symbolic links that point to the files in
   `$(VCD_DIR)`.

Because the Verilator drivers open the existing relative filenames, they follow
the links and write waveform data directly to `/dev/sda1` from the start. The
repository-local links and VCD files remain covered by the existing `*.vcd`
Git-ignore rule.

The `wave` target selects the file from `.config`:

- `CONFIG_SAMPLE_TEST=y` opens `$(VCD_DIR)/pvu_top_wave.vcd`.
- Other configured regressions open `$(VCD_DIR)/waveform.vcd`.

`wave` reports a clear error if the selected file does not exist. It does not
generate a waveform; generation remains the responsibility of `run`.

## Data Flow and Ownership

`make run` owns preparation of the output path. The active C++ driver owns VCD
contents, while the Makefile owns only the output directory and links. GTKWave
reads the selected file from `$(VCD_DIR)`. Users may redirect all of these steps
without editing sources by passing, for example,
`VCD_DIR=/home/other-waveforms` to Make.

## Validation

- Dry-run `run` and `wave` to inspect command expansion without starting the
  regression or GTKWave.
- Exercise preparation with `VCD_DIR` set to a project-local directory under
  `tmp/`, confirming both links resolve into that directory.
- Confirm the conflict path fails without overwriting either file.
- Confirm `.config` selects the expected filename for `wave`.
- Run `make run` only if the full Verilator regression is practical in the
  available environment; otherwise report it as skipped.

