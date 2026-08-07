"""One-hidden-layer value MLP in numpy, mirroring SpzNet from our C++ SPZ bot.

Architecture and training rule are a direct port of `SpzNet`
(src/selfplay_zero.cpp): tanh hidden layer, sigmoid output, binary
cross-entropy loss, minibatch SGD with classical momentum 0.9, weights
initialised uniform(-1/sqrt(fan_in), +1/sqrt(fan_in)) with zero biases.
No torch dependency; save/load is a .npz file.
"""

import numpy as np

MOMENTUM = 0.9


class Net:
    def __init__(self, inputs, hidden=64, seed=0):
        if inputs <= 0 or hidden <= 0:
            raise ValueError("Net requires nonzero dimensions")
        rng = np.random.default_rng(seed)
        s1 = 1.0 / np.sqrt(inputs)
        s2 = 1.0 / np.sqrt(hidden)
        self.w1 = rng.uniform(-s1, s1, size=(hidden, inputs))
        self.b1 = np.zeros(hidden)
        self.w2 = rng.uniform(-s2, s2, size=hidden)
        self.b2 = 0.0
        self._vel = [np.zeros_like(self.w1), np.zeros_like(self.b1),
                     np.zeros_like(self.w2), 0.0]

    @property
    def inputs(self):
        return self.w1.shape[1]

    def value_batch(self, X):
        """X: (n, inputs) float array -> (n,) win probabilities."""
        X = np.asarray(X, dtype=np.float64)
        h = np.tanh(X @ self.w1.T + self.b1)
        return 1.0 / (1.0 + np.exp(-(h @ self.w2 + self.b2)))

    def value(self, x):
        return float(self.value_batch(np.asarray(x)[None, :])[0])

    def train_batch(self, X, targets, learning_rate):
        """One SGD-with-momentum step on mean binary cross-entropy.

        Returns the mean BCE loss over the batch (pre-update), exactly as
        SpzNet::train_batch does.
        """
        X = np.asarray(X, dtype=np.float64)
        y = np.asarray(targets, dtype=np.float64)
        n = len(y)
        if n == 0 or X.shape[0] != n:
            raise ValueError("Net batch size mismatch")

        pre = X @ self.w1.T + self.b1          # (n, hidden)
        h = np.tanh(pre)
        p = 1.0 / (1.0 + np.exp(-(h @ self.w2 + self.b2)))

        eps = 1e-7
        loss = -np.mean(y * np.log(p + eps) + (1.0 - y) * np.log(1.0 - p + eps))

        d_out = p - y                          # (n,)
        g_w2 = d_out @ h / n                   # (hidden,)
        g_b2 = d_out.mean()
        d_h = np.outer(d_out, self.w2) * (1.0 - h * h)  # (n, hidden)
        g_w1 = d_h.T @ X / n                   # (hidden, inputs)
        g_b1 = d_h.mean(axis=0)

        v = self._vel
        v[0] = MOMENTUM * v[0] - learning_rate * g_w1
        v[1] = MOMENTUM * v[1] - learning_rate * g_b1
        v[2] = MOMENTUM * v[2] - learning_rate * g_w2
        v[3] = MOMENTUM * v[3] - learning_rate * g_b2
        self.w1 += v[0]
        self.b1 += v[1]
        self.w2 += v[2]
        self.b2 += v[3]
        return float(loss)

    # -- persistence -----------------------------------------------------

    def save(self, path, **metadata):
        np.savez(
            path, w1=self.w1, b1=self.b1, w2=self.w2, b2=np.float64(self.b2),
            **{f"meta_{k}": np.asarray(v) for k, v in metadata.items()},
        )

    @classmethod
    def load(cls, path):
        data = np.load(path, allow_pickle=False)
        net = cls.__new__(cls)
        net.w1 = data["w1"]
        net.b1 = data["b1"]
        net.w2 = data["w2"]
        net.b2 = float(data["b2"])
        net._vel = [np.zeros_like(net.w1), np.zeros_like(net.b1),
                    np.zeros_like(net.w2), 0.0]
        net.metadata = {
            k[len("meta_"):]: data[k] for k in data.files if k.startswith("meta_")
        }
        return net

    # -- weight transport for multiprocessing ----------------------------

    def get_weights(self):
        return (self.w1.copy(), self.b1.copy(), self.w2.copy(), self.b2)

    def set_weights(self, weights):
        self.w1, self.b1, self.w2, self.b2 = (
            weights[0].copy(), weights[1].copy(), weights[2].copy(), weights[3])
