"""Train the winner-imitation policy head on a gen_policy_archive.npz.

Same architecture as the value net (net.py: tanh hidden, sigmoid out,
BCE + momentum SGD) over the SAME afterstate features -- no new schema.
Target: 1 = this afterstate was the eventual winner's choice at that
decision, 0 = offered to the winner but declined. Reported: BCE and
holdout RANK accuracy (fraction of holdout decisions where the chosen
candidate scores highest), vs the 1/k random baseline.

Usage:
    python3 train_policy_head.py --archive policy_archive.npz \
        --out policy_head.npz
"""

import argparse

import numpy as np

from net import Net


def rank_accuracy(net, X, y, group):
    """Fraction of decisions whose chosen row scores strictly highest."""
    scores = net.value_batch(X.astype(np.float64))
    correct = total = 0
    start = 0
    n = len(group)
    while start < n:
        end = start
        while end < n and group[end] == group[start]:
            end += 1
        seg = slice(start, end)
        if y[seg].sum() == 1.0 and end - start > 1:
            total += 1
            correct += y[seg][np.argmax(scores[seg])] == 1.0
        start = end
    return correct / max(1, total), total


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--archive", default="policy_archive.npz")
    parser.add_argument("--out", default="policy_head.npz")
    parser.add_argument("--hidden", type=int, default=64)
    parser.add_argument("--lr", type=float, default=0.01)
    parser.add_argument("--batch", type=int, default=256)
    parser.add_argument("--epochs", type=int, default=3)
    parser.add_argument("--holdout-frac", type=float, default=0.1)
    args = parser.parse_args()

    data = np.load(args.archive, allow_pickle=False)
    X = data["X"].astype(np.float32)
    y = data["y"].astype(np.float32)
    group = data["group"]
    n_dec = int(group[-1]) + 1
    print(f"archive: {len(y)} rows, {n_dec} decisions, "
          f"{int(y.sum())} positives ({y.mean():.1%}), "
          f"avg candidates {len(y) / n_dec:.1f}")

    # Holdout split on DECISION boundaries (deterministic).
    rng = np.random.default_rng(11)
    hold_dec = set(rng.choice(n_dec, size=int(n_dec * args.holdout_frac),
                              replace=False).tolist())
    hold_mask = np.array([g in hold_dec for g in group])
    Xtr, ytr = X[~hold_mask], y[~hold_mask]
    Xho, yho, gho = X[hold_mask], y[hold_mask], group[hold_mask]

    net = Net(X.shape[1], hidden=args.hidden, seed=5)
    steps_per_epoch = max(1, len(ytr) // args.batch)
    for epoch in range(args.epochs):
        losses = []
        for _ in range(steps_per_epoch):
            pick = rng.integers(0, len(ytr), size=args.batch)
            losses.append(net.train_batch(Xtr[pick], ytr[pick], args.lr))
        acc, total = rank_accuracy(net, Xho, yho, gho)
        print(f"epoch {epoch + 1}: BCE {np.mean(losses):.4f}  "
              f"holdout rank-acc {acc:.1%} over {total} decisions "
              f"(random ~{np.mean(1.0 / np.bincount(group)[np.bincount(group) > 0]):.1%})")
    net.save(args.out, kind="policy_head", archive=args.archive,
             hidden=args.hidden, epochs=args.epochs)
    print(f"saved {args.out}")


if __name__ == "__main__":
    main()
