# VCD Output on `/dev/sda1` Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make Verilator write both supported VCD files under `/home/DFPVU-waveforms` by default and make `make wave` open the active waveform from that directory.

**Architecture:** Keep the C++ drivers' relative VCD filenames unchanged. The root Makefile prepares the storage directory, migrates conflict-free existing files, and exposes storage files through repository-local symbolic links before running Verilator; the selected `.config` value determines which storage file GTKWave opens.

**Tech Stack:** GNU Make 4.2.1, POSIX shell with GNU Coreutils, Verilator, GTKWave.

## Global Constraints

- `/dev/sda1` is mounted at `/home`; the default output directory is exactly `/home/DFPVU-waveforms`.
- `VCD_DIR` remains overrideable from the Make command line.
- C++ drivers retain `waveform.vcd` and `pvu_top_wave.vcd` as their canonical relative output names.
- Existing source and destination files must never be silently overwritten.
- Temporary validation files must stay under the project-local `tmp/` directory.
- Existing unrelated working-tree changes must not be modified or committed.

---

### Task 1: Makefile-managed VCD storage

**Files:**
- Modify: `makefile:1-76`
- Reference: `docs/superpowers/specs/2026-08-13-vcd-output-on-sda1-design.md`

**Interfaces:**
- Consumes: `.config` variable `CONFIG_SAMPLE_TEST`, C++ outputs `waveform.vcd` and `pvu_top_wave.vcd`.
- Produces: overrideable Make variable `VCD_DIR`, target `prepare-vcd`, repository-local VCD symlinks, and active waveform path used by `wave`.

- [ ] **Step 1: Establish an isolated failing preparation check**

Create a unique directory with `mktemp -d /root/DFPVU/tmp/vcd-output-test.XXXXXX`, then create a `work` directory, a `store` directory, a `work/.config` link to the repository `.config`, and an empty `work/waveform.vcd`. Run:

```bash
make -C "$TEST_ROOT/work" -f /root/DFPVU/makefile \
  prepare-vcd VCD_DIR="$TEST_ROOT/store"
```

Expected before implementation: FAIL with `No rule to make target 'prepare-vcd'`.

- [ ] **Step 2: Add the VCD variables and active-file selection**

Add the following near the existing source/configuration variables, with active-file selection after `include .config`:

```make
VCD_DIR ?= /home/DFPVU-waveforms
VCD_FILES := waveform.vcd pvu_top_wave.vcd

include .config

ifeq ($(CONFIG_SAMPLE_TEST),y)
ACTIVE_VCD := pvu_top_wave.vcd
else
ACTIVE_VCD := waveform.vcd
endif
```

Declare the changed command targets explicitly:

```make
.PHONY: prepare-vcd run wave
```

- [ ] **Step 3: Implement conflict-safe migration and links**

Add this target before `run`:

```make
prepare-vcd:
	@mkdir -p "$(VCD_DIR)"
	@set -eu; \
	for vcd_file in $(VCD_FILES); do \
		destination="$(VCD_DIR)/$$vcd_file"; \
		if [ -e "$$vcd_file" ] && [ ! -L "$$vcd_file" ]; then \
			if [ -e "$$destination" ]; then \
				echo "error: both $$vcd_file and $$destination exist; refusing to overwrite" >&2; \
				exit 1; \
			fi; \
			mv -- "$$vcd_file" "$$destination"; \
		fi; \
		ln -sfn -- "$$destination" "$$vcd_file"; \
	done
```

Make waveform preparation a prerequisite of the regression:

```make
run: prepare-vcd ${CSRCS} ${VSRCS}
```

- [ ] **Step 4: Make `wave` select and validate the active storage file**

Replace the existing `wave` recipe with:

```make
wave:
	@if [ ! -f "$(VCD_DIR)/$(ACTIVE_VCD)" ]; then \
		echo "error: waveform not found: $(VCD_DIR)/$(ACTIVE_VCD)" >&2; \
		exit 1; \
	fi
	gtkwave "$(VCD_DIR)/$(ACTIVE_VCD)"
```

- [ ] **Step 5: Verify preparation and selection in the isolated directory**

Repeat the Step 1 command. Expected: PASS, the original regular file is at `$TEST_ROOT/store/waveform.vcd`, and both repository-local names in `$TEST_ROOT/work` are symlinks into `$TEST_ROOT/store`.

Check target selection without launching GTKWave:

```bash
make -n wave VCD_DIR="$TEST_ROOT/store"
make -n wave VCD_DIR="$TEST_ROOT/store" CONFIG_SAMPLE_TEST=y
```

Expected: the first command contains `waveform.vcd`; the second contains `pvu_top_wave.vcd`.

- [ ] **Step 6: Verify the conflict path does not overwrite data**

In a second isolated `work`/`store` pair, create ordinary files named `work/waveform.vcd` and `store/waveform.vcd`, then run `prepare-vcd` as in Step 1.

Expected: FAIL containing `refusing to overwrite`; both files remain ordinary files with their original sizes and checksums.

- [ ] **Step 7: Run static checks and inspect the focused diff**

Run:

```bash
make -n run
make -n wave
git diff --check -- makefile
git diff -- makefile
```

Expected: `run` lists `prepare-vcd` commands before Verilator, `wave` references the active storage file, `git diff --check` emits no errors, and only the approved Makefile behavior changes are present.

- [ ] **Step 8: Prepare the real `/dev/sda1`-backed output directory**

Before mutation, run:

```bash
findmnt -T /home/DFPVU-waveforms
ls -l /home/DFPVU-waveforms /home/DFPVU-waveforms/*.vcd 2>/dev/null
```

If no same-name destination conflicts exist, run:

```bash
make prepare-vcd
```

Expected: `/root/DFPVU/waveform.vcd` and `/root/DFPVU/pvu_top_wave.vcd` are symbolic links, both targets reside under `/home/DFPVU-waveforms`, and `findmnt -T` reports `/dev/sda1`. If a conflict exists, stop and report both paths without overwriting either.

- [ ] **Step 9: Remove only isolated validation artifacts and commit**

Remove the exact unique directories created by `mktemp`; do not remove the project `tmp/` directory or unrelated contents. Then run:

```bash
git status --short
git add -- makefile
git commit -m "build: store VCD output on sda1"
```

Expected: the commit contains only `makefile`; existing unrelated changes remain in the working tree.

