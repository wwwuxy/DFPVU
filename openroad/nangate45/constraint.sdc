current_design PvuTop

if {![info exists ::env(PPA_CLOCK_PERIOD_NS)]} {
  set ::env(PPA_CLOCK_PERIOD_NS) 10.0
}
set clk_period $::env(PPA_CLOCK_PERIOD_NS)
set io_delay [expr {$clk_period * 0.20}]

create_clock -name dfpvu_vclk -period $clk_period
set data_inputs [remove_from_collection [all_inputs] [get_ports {clock reset}]]
set_input_delay $io_delay -clock dfpvu_vclk $data_inputs
set_output_delay $io_delay -clock dfpvu_vclk [all_outputs]
set_false_path -from [get_ports {clock reset}]
