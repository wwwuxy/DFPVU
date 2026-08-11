// Copyright 2016 ETH Zurich and University of Bologna.
// Copyright and related rights are licensed under the Solderpad Hardware
// License, Version 0.51 (the "License"); you may not use this file except in
// compliance with the License.  You may obtain a copy of the License at
// http://solderpad.org/licenses/SHL-0.51. Unless required by applicable law
// or agreed to in writing, software, hardware and materials distributed under
// this License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
// CONDITIONS OF ANY KIND, either express or implied. See the License for the
// specific language governing permissions and limitations under the License.

/// cf_math_pkg: Constant Function Implementations of Mathematical Functions for HDL Elaboration
///
/// This package contains a collection of mathematical functions that are commonly used when defining
/// the value of constants in HDL code.  These functions are implemented as Verilog constants
/// functions.  Introduced in Verilog 2001 (IEEE Std 1364-2001), a constant function (§ 10.3.5) is a
/// function whose value can be evaluated at compile time or during elaboration.  A constant function
/// must be called with arguments that are constants.
package cf_math_pkg;

    /// Ceiled Division of Two Natural Numbers
    ///
    /// Returns the quotient of two natural numbers, rounded towards plus infinity.
    function automatic integer ceil_div (input longint dividend, input longint divisor);
        automatic longint remainder;

        // pragma translate_off
        `ifndef VERILATOR
        if (dividend < 0) begin
            $fatal(1, "Dividend %0d is not a natural number!", dividend);
        end

        if (divisor < 0) begin
            $fatal(1, "Divisor %0d is not a natural number!", divisor);
        end

        if (divisor == 0) begin
            $fatal(1, "Division by zero!");
        end
        `endif
        // pragma translate_on

        remainder = dividend;
        for (ceil_div = 0; remainder > 0; ceil_div++) begin
            remainder = remainder - divisor;
        end
    endfunction

    function automatic integer clog(input integer n);
        begin
            n = n-1;
            for(clog=0; n>0; clog=clog+1)
                n = n>>1;
        end
    endfunction
    /// Index width required to be able to represent up to `num_idx` indices as a binary
    /// encoded signal.
    /// Ensures that the minimum width if an index signal is `1`, regardless of parametrization.
    ///
    /// Sample usage in type definition:
    /// As parameter:
    ///   `parameter type idx_t = logic[cf_math_pkg::idx_width(NumIdx)-1:0]`
    /// As typedef:
    ///   `typedef logic [cf_math_pkg::idx_width(NumIdx)-1:0] idx_t`
    function automatic integer unsigned idx_width (input integer unsigned num_idx);
        return (num_idx > 32'd1) ? unsigned'(clog(num_idx)) : 32'd1;
    endfunction

endpackage

// Copyright (c) 2018 - 2019 ETH Zurich, University of Bologna
// All rights reserved.
//
// This code is under development and not yet released to the public.
// Until it is released, the code is under the copyright of ETH Zurich and
// the University of Bologna, and may contain confidential and/or unpublished
// work. Any reuse/redistribution is strictly forbidden without written
// permission from ETH Zurich.
//
// Bug fixes and contributions will eventually be released under the
// SolderPad open hardware license in the context of the PULP platform
// (http://www.pulp-platform.org), under the copyright of ETH Zurich and the
// University of Bologna.

/// A trailing zero counter / leading zero counter.
/// Set MODE to 0 for trailing zero counter => cnt_o is the number of trailing zeros (from the LSB)
/// Set MODE to 1 for leading zero counter  => cnt_o is the number of leading zeros  (from the MSB)
/// If the input does not contain a zero, `empty_o` is asserted. Additionally `cnt_o` contains
/// the maximum number of zeros - 1. For example:
///   in_i = 000_0000, empty_o = 1, cnt_o = 6 (mode = 0)
///   in_i = 000_0001, empty_o = 0, cnt_o = 0 (mode = 0)
///   in_i = 000_1000, empty_o = 0, cnt_o = 3 (mode = 0)
/// Furthermore, this unit contains a more efficient implementation for Verilator (simulation only).
/// This speeds up simulation significantly.
module LZC #(
  /// The width of the input vector.
  parameter int unsigned WIDTH = 2,
  /// Mode selection: 0 -> trailing zero, 1 -> leading zero
  parameter bit          MODE  = 1'b0,
  /// Dependent parameter. Do **not** change!
  ///
  /// Width of the output signal with the zero count.
  parameter int unsigned CNT_WIDTH = cf_math_pkg::idx_width(WIDTH)
) (
  /// Input vector to be counted.
  input  logic [WIDTH-1:0]     in_i,
  /// Count of the leading / trailing zeros.
  output logic [CNT_WIDTH-1:0] cnt_o,
  /// Counter is empty: Asserted if all bits in in_i are zero.
  output logic                 empty_o
);

  if (WIDTH == 1) begin : gen_degenerate_lzc

    assign cnt_o[0] = !in_i[0];
    assign empty_o = !in_i[0];

  end else begin : gen_lzc

    localparam int unsigned NumLevels = cf_math_pkg::clog(WIDTH);

    // pragma translate_off
    initial begin
      assert(WIDTH > 0) else $fatal(1, "input must be at least one bit wide");
    end
    // pragma translate_on

    logic [WIDTH-1:0][NumLevels-1:0] index_lut;
    logic [2**NumLevels-1:0] sel_nodes;
    logic [2**NumLevels-1:0][NumLevels-1:0] index_nodes;

    logic [WIDTH-1:0] in_tmp;

    // reverse vector if required
    always_comb begin : flip_vector
      for (int unsigned i = 0; i < WIDTH; i++) begin
        in_tmp[i] = (MODE) ? in_i[WIDTH-1-i] : in_i[i];
      end
    end

    for (genvar j = 0; unsigned'(j) < WIDTH; j++) begin : g_index_lut
      assign index_lut[j] = (NumLevels)'(unsigned'(j));
    end

    for (genvar level = 0; unsigned'(level) < NumLevels; level++) begin : g_levels
      if (unsigned'(level) == NumLevels - 1) begin : g_last_level
        for (genvar k = 0; k < 2 ** level; k++) begin : g_level
          // if two successive indices are still in the vector...
          if (unsigned'(k) * 2 < WIDTH - 1) begin : g_reduce
            assign sel_nodes[2 ** level - 1 + k] = in_tmp[k * 2] | in_tmp[k * 2 + 1];
            assign index_nodes[2 ** level - 1 + k] = (in_tmp[k * 2] == 1'b1)
              ? index_lut[k * 2] :
                index_lut[k * 2 + 1];
          end
          // if only the first index is still in the vector...
          if (unsigned'(k) * 2 == WIDTH - 1) begin : g_base
            assign sel_nodes[2 ** level - 1 + k] = in_tmp[k * 2];
            assign index_nodes[2 ** level - 1 + k] = index_lut[k * 2];
          end
          // if index is out of range
          if (unsigned'(k) * 2 > WIDTH - 1) begin : g_out_of_range
            assign sel_nodes[2 ** level - 1 + k] = 1'b0;
            assign index_nodes[2 ** level - 1 + k] = '0;
          end
        end
      end else begin : g_not_last_level
        for (genvar l = 0; l < 2 ** level; l++) begin : g_level
          assign sel_nodes[2 ** level - 1 + l] =
              sel_nodes[2 ** (level + 1) - 1 + l * 2] | sel_nodes[2 ** (level + 1) - 1 + l * 2 + 1];
          assign index_nodes[2 ** level - 1 + l] = (sel_nodes[2 ** (level + 1) - 1 + l * 2] == 1'b1)
            ? index_nodes[2 ** (level + 1) - 1 + l * 2] :
              index_nodes[2 ** (level + 1) - 1 + l * 2 + 1];
        end
      end
    end

    assign cnt_o = NumLevels > unsigned'(0) ? index_nodes[0] : {(cf_math_pkg::clog(WIDTH)) {1'b0}};
    assign empty_o = NumLevels > unsigned'(0) ? ~sel_nodes[0] : ~(|in_i);

  end : gen_lzc

endmodule : LZC
