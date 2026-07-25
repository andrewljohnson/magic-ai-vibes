# Test and Evaluation Notes

These instructions apply to files under `tests/`.

- Tests must be deterministic and name their seed when randomness matters.
- Cover every implemented card's legal play, resolution, and important
  interaction.
- Stack tests must exercise priority passing and countered resolution, not
  only direct helper calls.
- Keep fast correctness tests separate from larger statistical benchmarks.
- Statistical assertions need comfortable margins; the CLI stability and
  benchmark harnesses own the strict bot-strength acceptance decision.
- For tournament tests, verify game accounting, play/draw balance, seat
  balance, and rollout accounting.
- A failed bot experiment is not a reason to loosen an unrelated test.

