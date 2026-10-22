Regression guard for the standard (binaural input) behaviour.

ref_v07.bin holds the filters designed by version 0.7 for 64 parameter sets (4 sample rates x 2 head models
x 8 settings). Later versions must reproduce it byte for byte as long as the new features are left off.

Run (from the repository root):
  g++ -O2 -std=c++17 -Isrc tests/regression/dump_filters.cpp src/ctc_core.cpp -o dump_filters
  ./dump_filters new.bin
  cmp tests/regression/ref_v07.bin new.bin && echo OK
