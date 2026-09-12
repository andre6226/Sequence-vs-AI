#!/usr/bin/env python3
"""Estrae i pesi di SequenceNetV2 dal file ONNX in un blob piatto.

net.hpp legge questo blob: niente ONNX Runtime, niente grafo. I pesi delle
convoluzioni 3x3 vengono trasposti da [co][ci][ky][kx] a [ky][kx][ci][co],
cosi' il ciclo piu' interno dell'inferenza gira sui canali di uscita, che
sono contigui, e il compilatore lo vettorizza (SIMD128 in WebAssembly).

    python3 sequence_net/export_weights.py sequence_net.onnx js/game/weights.bin
"""
import sys
import numpy as np
import onnx
from onnx import numpy_helper


def export(onnx_path, out_path):
    g = onnx.load(onnx_path).graph
    w = {t.name: numpy_helper.to_array(t) for t in g.initializer}
    out = []

    def conv3(name):
        out.append(w[name + ".weight"].transpose(2, 3, 1, 0).astype(np.float32).ravel())
        out.append(w[name + ".weight_bias"].astype(np.float32))

    def conv1(name):
        m = w[name + ".weight"]
        out.append(m.reshape(m.shape[0], -1).T.astype(np.float32).ravel())
        out.append(w[name + ".weight_bias"].astype(np.float32))

    def gemm(name):
        out.append(w[name + ".weight"].astype(np.float32).ravel())
        out.append(w[name + ".bias"].astype(np.float32))

    conv3("conv_in")
    for i in range(4):
        conv3(f"res_blocks.{i}.conv1")
        conv3(f"res_blocks.{i}.conv2")
    conv1("policy_conv"); gemm("policy_fc")
    conv1("value_conv");  gemm("value_fc1"); gemm("value_fc2")

    blob = np.concatenate(out)
    blob.tofile(out_path)
    print(f"{out_path}: {blob.size:,} float, {blob.nbytes / 1024:.0f} KB")


if __name__ == "__main__":
    src = sys.argv[1] if len(sys.argv) > 1 else "sequence_net.onnx"
    dst = sys.argv[2] if len(sys.argv) > 2 else "js/game/weights.bin"
    export(src, dst)
