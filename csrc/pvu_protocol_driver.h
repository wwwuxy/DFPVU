#pragma once

#include <cstdlib>
#include <iostream>

#include "VPvuTop.h"

static void pvu_wait_until_request_ready(VPvuTop* dut) {
    dut->eval();
    for (unsigned cycle = 0; !dut->io_in_ready; ++cycle) {
        if (cycle == 10000) {
            std::cerr << "timed out waiting for in_ready\n";
            std::exit(1);
        }
        dut->clock = 1;
        dut->eval();
        dut->clock = 0;
        dut->eval();
    }
}

static void pvu_wait_until_response_valid(VPvuTop* dut) {
    dut->eval();
    for (unsigned cycle = 0; !dut->io_out_valid; ++cycle) {
        if (cycle == 10000) {
            std::cerr << "timed out waiting for out_valid\n";
            std::exit(1);
        }
        dut->clock = 1;
        dut->eval();
        dut->clock = 0;
        dut->eval();
    }
}
