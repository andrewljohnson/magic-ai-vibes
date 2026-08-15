"""Export a .npz SpzNet to the flat little-endian binary spz-core/net.rs
reads: u64 hidden, u64 inputs, then w1 (hidden*inputs, row-major),
b1 (hidden), w2 (hidden), b2 (1) -- all f64.

Usage: python3 export_weights.py penta_net.npz penta_net.spzw
"""
import sys
import numpy as np
from net import Net

net = Net.load(sys.argv[1])
hidden, inputs = net.w1.shape
with open(sys.argv[2], "wb") as f:
    f.write(np.array([hidden, inputs], dtype="<u8").tobytes())
    f.write(np.ascontiguousarray(net.w1, dtype="<f8").tobytes())
    f.write(np.ascontiguousarray(net.b1, dtype="<f8").tobytes())
    f.write(np.ascontiguousarray(net.w2, dtype="<f8").tobytes())
    f.write(np.array([net.b2], dtype="<f8").tobytes())
print(f"exported {sys.argv[1]} ({hidden}x{inputs}) -> {sys.argv[2]}")
