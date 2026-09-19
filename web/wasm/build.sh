#!/bin/bash
# Builds Proximate's DSP (the plugin's own Source/DSP/Compensator.cpp, unmodified) plus the
# demo's proximity simulator to WebAssembly. No shim is needed: Source/DSP depends on
# nothing but the C++ standard library, by design (see AGENTS.md §3). Output is a single
# self-contained ES module (wasm embedded, synchronous compile) so the same file loads in
# an AudioWorklet, on the main thread, and in Node for the verification harness.
set -euo pipefail
cd "$(dirname "$0")"

SRC=../../Source

emcc -O3 -std=c++20 \
  -I "$SRC" \
  proximate_web.cpp \
  "$SRC/DSP/Compensator.cpp" \
  -sMODULARIZE=1 \
  -sEXPORT_ES6=1 \
  -sEXPORT_NAME=createProximateModule \
  -sSINGLE_FILE=1 \
  -sWASM_ASYNC_COMPILATION=0 \
  -sALLOW_MEMORY_GROWTH=1 \
  -sENVIRONMENT=web,worker,node \
  -sEXPORTED_RUNTIME_METHODS=cwrap,ccall,HEAPF32 \
  -sINCOMING_MODULE_JS_API=instantiateWasm,locateFile \
  -o ../public/proximate.js

echo "Built ../public/proximate.js ($(du -h ../public/proximate.js | cut -f1))"
