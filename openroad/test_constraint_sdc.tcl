set repo_root [file normalize [file join [file dirname [info script]] ..]]
set ::env(PPA_CLOCK_PERIOD_NS) 7.5

proc current_design {name} {}
proc create_clock {args} {}
proc all_inputs {args} { return {clock reset data_a data_b} }
proc all_outputs {} { return {result} }
proc get_ports {ports} { return $ports }
proc set_input_delay {delay args} {
  set ::input_delay $delay
  set ::input_delay_args $args
}
proc set_output_delay {delay args} {}
proc set_false_path {args} { set ::false_path_args $args }

source [file join $repo_root openroad nangate45 constraint.sdc]

if {$data_inputs ne {data_a data_b}} {
  error "expected only data inputs, got {$data_inputs}"
}
if {$::input_delay != 1.5} {
  error "expected 1.5 ns input delay, got $::input_delay"
}
if {$::input_delay_args ne {-clock dfpvu_vclk {data_a data_b}}} {
  error "unexpected input delay arguments: $::input_delay_args"
}
if {$::false_path_args ne {-from {clock reset}}} {
  error "unexpected false-path arguments: $::false_path_args"
}
