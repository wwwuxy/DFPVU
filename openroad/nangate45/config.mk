export DESIGN_NAME = PvuTop
export DESIGN_NICKNAME = dfpvu
export PLATFORM = nangate45

DFPVU_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
export VERILOG_FILES = $(DFPVU_ROOT)/vsrc/PvuTop.sv
export SDC_FILE = $(DFPVU_ROOT)/openroad/nangate45/constraint.sdc
export ABC_CLOCK_PERIOD_IN_PS = $(shell awk 'BEGIN { printf "%.12g", $(PPA_CLOCK_PERIOD_NS) * 1000 }')
export SYNTH_HDL_FRONTEND = slang

export CORE_UTILIZATION ?= 55
export PLACE_DENSITY_LB_ADDON = 0.20
export TNS_END_PERCENT = 100
export SYNTH_REPEATABLE_BUILD ?= 1
export ABC_AREA = 1
export ADDER_MAP_FILE :=
