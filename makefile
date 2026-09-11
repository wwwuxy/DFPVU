# project source
TOPNAME = PvuTop
VSRCS += $(shell find $(abspath ./vsrc) -name "*.sv")
CSRCS += $(shell find $(abspath ./csrc) -name "*.cpp")
CONFIG_H = config.h

# The exact-reference regression uses the checked-out SoftPosit source. Its
# headers use C++ aggregate initializers, so compile the small p32-only subset
# as C++ rather than relying on the upstream all-target (which also builds
# unrelated formats). No SoftPosit source is copied into this repository.
SOFTPOSIT_ROOT ?= /root/SoftPosit
SOFTPOSIT_REF_DIR ?= build/softposit-ref
SOFTPOSIT_REF_LIB := $(SOFTPOSIT_REF_DIR)/libsoftposit-ref.a
SOFTPOSIT_REF_SRCS := \
	$(SOFTPOSIT_ROOT)/source/s_addMagsP32.c \
	$(SOFTPOSIT_ROOT)/source/s_subMagsP32.c \
	$(SOFTPOSIT_ROOT)/source/s_mulAddP32.c \
	$(SOFTPOSIT_ROOT)/source/p32_add.c \
	$(SOFTPOSIT_ROOT)/source/p32_sub.c \
	$(SOFTPOSIT_ROOT)/source/p32_mul.c \
	$(SOFTPOSIT_ROOT)/source/p32_mulAdd.c \
	$(SOFTPOSIT_ROOT)/source/p32_div.c \
	$(SOFTPOSIT_ROOT)/source/p32_to_pX2.c \
	$(SOFTPOSIT_ROOT)/source/p32_to_i32.c \
	$(SOFTPOSIT_ROOT)/source/i32_to_p32.c \
	$(SOFTPOSIT_ROOT)/source/c_convertDecToPosit32.c \
	$(SOFTPOSIT_ROOT)/source/c_convertPosit32ToDec.c
SOFTPOSIT_REF_CXXFLAGS := -std=c++17 -O2 -DSOFTPOSIT_FAST_INT64 -DINLINE_LEVEL=5 \
	-I$(SOFTPOSIT_ROOT)/build/Linux-x86_64-GCC \
	-I$(SOFTPOSIT_ROOT)/source/8086-SSE \
	-I$(SOFTPOSIT_ROOT)/source/include

# verilator flags
VERILATOR_FLAGS += -Wall --cc --trace --exe --build --top-module $(TOPNAME)
VERILATOR_FLAGS += -Wno-DECLFILENAME -Wno-PINCONNECTEMPTY -Wno-UNUSEDSIGNAL -Wno-UNOPTFLAT
VERILATOR_FLAGS += --threads-dpi all
VERILATOR_FLAGS += -j 16

include .config

PVU_RUN_ARGS :=

LLM_P32_TRACE_DIR := $(subst ",,$(CONFIG_LLM_P32_MAC_TRACE_DIR))
LLM_P32_MAC_M := $(CONFIG_MATRIX_GEMM_P32_M)
LLM_P32_MAC_N := $(CONFIG_MATRIX_GEMM_P32_N)
LLM_P32_MAC_K := $(CONFIG_MATRIX_GEMM_P32_K)
LLM_P32_CONVERSION_TENSOR := $(subst ",,$(or $(CONFIG_LLM_P32_CONVERSION_TENSOR),input))
LLM_P32_CONVERSION_SAMPLE_COUNT := $(or $(CONFIG_LLM_P32_CONVERSION_SAMPLE_COUNT),256)
LLM_P32_DOT_SAMPLE_COUNT := $(or $(CONFIG_LLM_P32_DOT_SAMPLE_COUNT),256)
LLM_P32_MUL_SAMPLE_COUNT := $(or $(CONFIG_LLM_P32_MUL_SAMPLE_COUNT),256)
LLM_P32_TO_INT_TENSOR := $(subst ",,$(or $(CONFIG_LLM_P32_TO_INT_TENSOR),input))
LLM_P32_TO_INT_SAMPLE_COUNT := $(or $(CONFIG_LLM_P32_TO_INT_SAMPLE_COUNT),256)

LLM_P32_CONFIG_MODEL_DIR := $(subst ",,$(CONFIG_LLM_P32_MODEL_DIR))
LLM_P32_AUTO_EXPORT_TRACE := $(if $(CONFIG_LLM_P32_AUTO_EXPORT_TRACE),$(CONFIG_LLM_P32_AUTO_EXPORT_TRACE),y)
LLM_P32_PYTHON ?= /root/qwen312-venv/bin/python
LLM_P32_TRACE_EXPORTER ?= tools/export_llm_p32_trace.py
LLM_P32_EXPORT_TOKEN_COUNT ?= 128

ifeq ($(CONFIG_LLM_P32_PROFILE_QWEN3_0_6B),y)
LLM_P32_TRACE_PROFILE := qwen3-0.6b
LLM_P32_DEFAULT_MODEL_DIR := /root/models/Qwen3-0.6B
LLM_P32_EXPORT_MODULE := self_attn.q_proj
endif
ifeq ($(CONFIG_LLM_P32_PROFILE_GEMMA3_1B),y)
LLM_P32_TRACE_PROFILE := gemma3-1b
LLM_P32_DEFAULT_MODEL_DIR := /root/models/gemma-3-1b-it
LLM_P32_EXPORT_MODULE := self_attn.q_proj
endif
ifeq ($(CONFIG_LLM_P32_PROFILE_PHI4_MINI),y)
LLM_P32_TRACE_PROFILE := phi4-mini
LLM_P32_DEFAULT_MODEL_DIR := /root/models/phi-4-mini-instruct
LLM_P32_EXPORT_MODULE := self_attn.qkv_proj
endif
LLM_P32_MODEL_DIR := $(or $(LLM_P32_CONFIG_MODEL_DIR),$(LLM_P32_DEFAULT_MODEL_DIR))
LLM_P32_WORKLOADS := $(filter y,$(CONFIG_LLM_P32_MAC_WORKLOAD) $(CONFIG_LLM_P32_CONVERSION_WORKLOAD) $(CONFIG_LLM_P32_DOT_WORKLOAD) $(CONFIG_LLM_P32_MUL_WORKLOAD) $(CONFIG_LLM_P32_TO_INT_WORKLOAD))
ifneq ($(LLM_P32_WORKLOADS),)
ifneq ($(words $(LLM_P32_WORKLOADS)),1)
$(error Select exactly one LLM P32 workload in Kconfig)
endif
endif

ifeq ($(CONFIG_LLM_P32_MAC_WORKLOAD),y)
PVU_RUN_ARGS += "$(LLM_P32_TRACE_DIR)" "$(LLM_P32_MAC_M)" "$(LLM_P32_MAC_N)" "$(LLM_P32_MAC_K)"
endif
ifeq ($(CONFIG_LLM_P32_CONVERSION_WORKLOAD),y)
PVU_RUN_ARGS += "$(LLM_P32_TRACE_DIR)" "$(LLM_P32_CONVERSION_TENSOR)" "$(LLM_P32_CONVERSION_SAMPLE_COUNT)"
endif
ifeq ($(CONFIG_LLM_P32_DOT_WORKLOAD),y)
PVU_RUN_ARGS += "$(LLM_P32_TRACE_DIR)" "$(LLM_P32_DOT_SAMPLE_COUNT)"
endif
ifeq ($(CONFIG_LLM_P32_MUL_WORKLOAD),y)
PVU_RUN_ARGS += "$(LLM_P32_TRACE_DIR)" "$(LLM_P32_MUL_SAMPLE_COUNT)"
endif
ifeq ($(CONFIG_LLM_P32_TO_INT_WORKLOAD),y)
PVU_RUN_ARGS += "$(LLM_P32_TRACE_DIR)" "$(LLM_P32_TO_INT_TENSOR)" "$(LLM_P32_TO_INT_SAMPLE_COUNT)"
endif

ifneq ($(LLM_P32_WORKLOADS),)
run: llm_p32_trace_config
.PHONY: llm_p32_trace_config llm_p32_mac_trace_config
llm_p32_trace_config:
	@test -n "$(LLM_P32_TRACE_DIR)" || { echo "CONFIG_LLM_P32_MAC_TRACE_DIR must name an LLM trace directory" >&2; exit 2; }
	@if test -f "$(LLM_P32_TRACE_DIR)/metadata.json"; then if ! grep -q '"profile"' "$(LLM_P32_TRACE_DIR)/metadata.json"; then echo "LLM trace: reusing legacy trace without profile $(LLM_P32_TRACE_DIR)"; elif grep -Eq '"profile"[[:space:]]*:[[:space:]]*"$(LLM_P32_TRACE_PROFILE)"' "$(LLM_P32_TRACE_DIR)/metadata.json"; then echo "LLM trace: reusing $(LLM_P32_TRACE_DIR)"; else echo "LLM trace profile does not match selected Kconfig profile: expected $(LLM_P32_TRACE_PROFILE)" >&2; exit 2; fi; elif test "$(LLM_P32_AUTO_EXPORT_TRACE)" != "y"; then echo "LLM trace metadata is missing: $(LLM_P32_TRACE_DIR)/metadata.json" >&2; echo "Enable CONFIG_LLM_P32_AUTO_EXPORT_TRACE or export the trace manually." >&2; exit 2; elif test -z "$(LLM_P32_TRACE_PROFILE)" || test -z "$(LLM_P32_EXPORT_MODULE)"; then echo "Select one LLM P32 trace profile in Kconfig." >&2; exit 2; elif test ! -d "$(LLM_P32_MODEL_DIR)"; then echo "Local LLM model directory is missing: $(LLM_P32_MODEL_DIR)" >&2; echo "Set CONFIG_LLM_P32_MODEL_DIR in Kconfig to the downloaded model." >&2; exit 2; elif test ! -x "$(LLM_P32_PYTHON)"; then echo "LLM Python interpreter is missing or not executable: $(LLM_P32_PYTHON)" >&2; echo "Override LLM_P32_PYTHON with an environment containing torch and transformers." >&2; exit 2; elif test -d "$(LLM_P32_TRACE_DIR)" && test -n "$$(find "$(LLM_P32_TRACE_DIR)" -mindepth 1 -maxdepth 1 -print -quit)"; then echo "Refusing to overwrite non-empty trace directory without metadata.json: $(LLM_P32_TRACE_DIR)" >&2; exit 2; else echo "LLM trace: exporting $(LLM_P32_TRACE_PROFILE) from local model (offline, $(LLM_P32_EXPORT_TOKEN_COUNT) tokens)"; HF_HUB_OFFLINE=1 TRANSFORMERS_OFFLINE=1 "$(LLM_P32_PYTHON)" "$(LLM_P32_TRACE_EXPORTER)" --profile "$(LLM_P32_TRACE_PROFILE)" --model "$(LLM_P32_MODEL_DIR)" --output "$(LLM_P32_TRACE_DIR)" --tokens "$(LLM_P32_EXPORT_TOKEN_COUNT)" --layer 0 --module "$(LLM_P32_EXPORT_MODULE)"; fi
	@test -f "$(LLM_P32_TRACE_DIR)/metadata.json" || { echo "LLM trace exporter did not produce metadata.json: $(LLM_P32_TRACE_DIR)" >&2; exit 2; }

llm_p32_mac_trace_config: llm_p32_trace_config
endif
# The LLM workload is intentionally invoked with its Kconfig trace path. It
# cannot silently use a synthetic fixture.

PVU_SOFTPOSIT_REFERENCE_TESTS := $(CONFIG_PVU_PROTOCOL_REGRESSION) $(CONFIG_PVU_MAC_REGRESSION) $(CONFIG_LLM_P32_MAC_WORKLOAD) $(CONFIG_LLM_P32_CONVERSION_WORKLOAD) $(CONFIG_LLM_P32_DOT_WORKLOAD) $(CONFIG_LLM_P32_MUL_WORKLOAD) $(CONFIG_LLM_P32_TO_INT_WORKLOAD) $(CONFIG_MATRIX_GEMM_P32_BENCHMARK) $(CONFIG_RESNET_POSIT32_TO_FP4) $(CONFIG_RESNET_POSIT32_TO_FP8) $(CONFIG_RESNET_POSIT32_TO_FP16) $(CONFIG_RESNET_POSIT32_TO_FP32)
ifneq ($(filter y,$(PVU_SOFTPOSIT_REFERENCE_TESTS)),)
VERILATOR_FLAGS += -CFLAGS "$(SOFTPOSIT_REF_CXXFLAGS)"
VERILATOR_FLAGS += -LDFLAGS "$(abspath $(SOFTPOSIT_REF_LIB))"
PVU_RUN_PREREQS += $(SOFTPOSIT_REF_LIB)
endif

$(CONFIG_H): .config
	@echo "/* Auto-generated by Makefile from .config */" > $(CONFIG_H)
	@echo "#ifndef CONFIG_H" >> $(CONFIG_H)
	@echo "#define CONFIG_H" >> $(CONFIG_H)
	@echo "" >> $(CONFIG_H)
	@awk 'BEGIN { FS="="; OFS=" "; } \
		/^[^#]/ { \
			if ($$2 == "y") { \
				print "#define", $$1, "1"; \
			} else if ($$2 == "n" || $$2 == "") { \
				print "#define", $$1, "0"; \
			} else if ($$2 ~ /^[0-9]+$$/) { \
				print "#define", $$1, $$2; \
			} \
		}' .config >> $(CONFIG_H)

	@echo "#endif // CONFIG_H" >> $(CONFIG_H)


verilog:
	export JAVA_OPTS="-Xmx16G -Xms8G -XX:+UseG1GC -XX:G1HeapRegionSize=32M -XX:MaxGCPauseMillis=200 -XX:ParallelGCThreads=8" && \
	sbt -v -no-colors \
		-J-Xmx16G -J-Xms8G \
		-J-XX:+UseG1GC \
		-J-XX:G1HeapRegionSize=32M \
		-J-XX:MaxGCPauseMillis=200 \
		-J-XX:ParallelGCThreads=8 \
		-Dsbt.task.timings=true \
		-Dsbt.ci=true \
		-DmaxThreads=8 \
		"runMain pvu.Elaborate"
	python3 clean_line.py

$(SOFTPOSIT_REF_LIB): $(SOFTPOSIT_REF_SRCS)
	@mkdir -p $(SOFTPOSIT_REF_DIR)
	cd $(SOFTPOSIT_REF_DIR) && g++ $(SOFTPOSIT_REF_CXXFLAGS) -x c++ -c $(SOFTPOSIT_REF_SRCS)
	ar crs $@ $(SOFTPOSIT_REF_DIR)/*.o

run:${CSRCS} ${VSRCS} $(CONFIG_H) $(PVU_RUN_PREREQS)
	verilator ${VERILATOR_FLAGS} ${CSRCS} ${VSRCS}
	./obj_dir/VPvuTop $(PVU_RUN_ARGS)

wave:
	gtkwave pvu_top_wave.vcd
	
menuconfig:
	menuconfig
	make config.h

count:
	@echo "Number of Scala files:"
	@echo $(shell find . -type f -name "*.scala" | wc -l)
	@echo "Total lines of Scala code:"
	@echo $(shell find src -name "*.scala" -type f | xargs cat | wc -l)
	@echo "Total lines of Verilog code:"
	@echo $(shell find vsrc -name "*.sv" -type f | xargs cat | wc -l)


debug:
	make verilog
	make run

