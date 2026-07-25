#!/usr/bin/env bash
set -e

export TRT_ROOT=/home/lyt/cpp_lib/TensorRT-8.6.1.6
export CUDA_HOME=/usr/local/cuda-11.7
export PATH=$TRT_ROOT/bin:$CUDA_HOME/bin:$PATH
export LD_LIBRARY_PATH=$TRT_ROOT/lib:$CUDA_HOME/lib64:$CUDA_HOME/targets/x86_64-linux/lib

TRT_BIN=$TRT_ROOT/bin/trtexec

echo ">>> Building TensorRT engine: 480x640 FP16"
$TRT_BIN \
  --onnx=spnet_480_640_int32_sim.onnx \
  --saveEngine=spnet_480_640.engine \
  --fp16 \
  --builderOptimizationLevel=0 \
  --memPoolSize=workspace:4096

echo ">>> Building TensorRT engine: 512x640 FP16"
$TRT_BIN \
  --onnx=spnet_512_640_int32_sim.onnx \
  --saveEngine=spnet_512_640.engine \
  --fp16 \
  --builderOptimizationLevel=0 \
  --memPoolSize=workspace:4096

echo ">>> Done."