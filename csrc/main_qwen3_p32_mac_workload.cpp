#include "../config.h"

#ifdef CONFIG_QWEN3_P32_MAC_WORKLOAD

#include <cstdlib>
#include <iostream>
#include <stdexcept>

#include "pvu_qwen3_metrics.h"
#include "pvu_qwen3_trace.h"

int main(int argc, char** argv) {
  try {
    const pvu::Qwen3Selection selection =
        pvu::parse_qwen3_selection(argc, argv);
    const pvu::Qwen3Trace trace = pvu::load_qwen3_trace(selection.trace_root);
    if (trace.modules.size() != 6 ||
        trace.modules.at("q_proj").input.shape.size() != 2) {
      throw std::runtime_error("trace contract mismatch");
    }
    std::cout << "Loaded Qwen3 trace: model=" << trace.model
              << " root=" << selection.trace_root
              << " modules=" << trace.modules.size()
              << " tokens=" << selection.token_count
              << " rows=" << selection.row_count << '\n';
    return EXIT_SUCCESS;
  } catch (const std::exception& error) {
    std::cerr << "Qwen3 trace error: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
}

#endif  // CONFIG_QWEN3_P32_MAC_WORKLOAD
