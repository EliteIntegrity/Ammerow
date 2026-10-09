# Random-number streams

Ammerow uses xoshiro128** 1.1 by David Blackman and Sebastiano Vigna, seeded
using SplitMix64 by Sebastiano Vigna.

Reference sources: <https://prng.di.unimi.it/xoshiro128starstar.c> and
<https://prng.di.unimi.it/splitmix64.c>. Complete permission notices are retained
in `third_party/licenses/Xoshiro-SplitMix-public-domain.txt`.

The interface is `src/z-rand.h`; generator state is private to `src/z-rand.c`.

- Gameplay calls the established random/dice helpers. This stream is saved.
- Deterministic content generation uses paired `Rand_begin_deterministic()`
  and `Rand_end_deterministic()` calls. Scopes cannot nest.
- Presentation uses `Rand_simple()`, a separate cosmetic stream that must not
  determine gameplay.

Bounded draws use rejection sampling. The saved gameplay snapshot contains
the algorithm ID and four 32-bit state words, not a compiler-dependent
structure. The version-2 RNG save block rejects an invalid algorithm or zero
state. Known-answer tests cover reproducibility and stream isolation.

Changes to the generator or saved representation require an explicit
compatibility decision and updated tests. This is a simulation generator,
not a cryptographic generator; do not use it for security-sensitive secrets.
