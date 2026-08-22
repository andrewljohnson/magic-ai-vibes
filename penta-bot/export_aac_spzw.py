#!/usr/bin/env python3
"""Export an AAC actor .npz to the flat .spzw binary spz-core/net.rs reads.

export_weights.py handles the SpzNet format (w1/b1/w2/b2); the AAC actor is
a torch state_dict, so its keys are f1.weight/f1.bias/f2.weight/f2.bias.
Same architecture, same file layout -- only the key names differ.

CAVEAT worth knowing before trusting a search result built on this: the AAC
actor is trained as a RELATIVE scorer (softmax over a decision's
afterstates), so its raw logit carries no absolute scale. net.rs's
Mlp::value applies a sigmoid, which is monotone -- fine for ordering, but
sigmoid(logit) is NOT a calibrated win probability, and MCTS backup wants
real values. If search underperforms 1-ply, start here.

Usage: export_aac_spzw.py aac_par_belief_best.npz out.spzw
"""
import sys
import numpy as np

d = np.load(sys.argv[1])
w1 = np.asarray(d["f1.weight"], dtype="<f8")          # (hidden, inputs)
b1 = np.asarray(d["f1.bias"], dtype="<f8")            # (hidden,)
w2 = np.asarray(d["f2.weight"], dtype="<f8").reshape(-1)   # (hidden,)
b2 = float(np.asarray(d["f2.bias"]).reshape(-1)[0])
hidden, inputs = w1.shape
assert b1.shape == (hidden,) and w2.shape == (hidden,), "shape mismatch"
with open(sys.argv[2], "wb") as f:
    f.write(np.array([hidden, inputs], dtype="<u8").tobytes())
    f.write(np.ascontiguousarray(w1).tobytes())
    f.write(np.ascontiguousarray(b1).tobytes())
    f.write(np.ascontiguousarray(w2).tobytes())
    f.write(np.array([b2], dtype="<f8").tobytes())
print(f"exported {sys.argv[1]} ({hidden}x{inputs}) -> {sys.argv[2]}")
