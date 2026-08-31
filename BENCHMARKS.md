# Benchmarks

`benchmarks/bench.cpp` builds the `compat_bench` executable when `COMPAT_BUILD_BENCH=ON`. It measures the throughput and latency of the registry's core operations on a synthetic corpus so that the cost of a compatibility claim is known and comparable. Like the library, the benchmark is compiled with `/W4 /WX` on MSVC; reported numbers are real measurements from the machine that ran the harness.

## Harness

The harness is a self-contained, no-dependency executable that links only the `compat` library. It constructs a deterministic synthetic workload (model, tokenizer, tensor, KV, kernel, graph, adapter, backend, device, protocol, policy profiles plus a set of rules and evidence records), warms up where relevant, then times each benchmark with a monotonic clock. Where the operation is timing-sensitive (e.g. concurrent reads) it uses multiple reader threads.

Each metric is reported as a **per-operation** latency and (where applicable) an **operations-per-second** throughput. The layout of the corpus is fixed so a change in the registry shows up in the numbers, not in the workload. The harness prints a table of `name | iterations | mean (ns/op) | total (ms) | ops/s` and exits non-zero on any check failure so results are trustworthy.

## Metrics

The harness measures the following operations. Measured values are captured at final closure; placeholders like `[MEASURED_IN_CLOSURE]` are filled in from the actual run.

### Canonical profile construction

Building a `Canon` record from a profile's fields (`to_canon()`) for each domain. This is the cost of turning a typed profile into a canonical, hashable value tree.

- **Methodology:** construct the same profile repeatedly; time only the `to_canon()` call (no registry interaction).
- **Result (profile construction):** `[MEASURED_IN_CLOSURE] ns/op`

### SHA-256 fingerprint

`canonical_fingerprint` / `canonical_fingerprint_hex` over a canonical value. This is the unit of content identity used for exact-match checks, decision digests, and persistence integrity.

- **Methodology:** fingerprint a fixed canonical record (single record and a larger nested record) `N` times.
- **Result (single record):** `[MEASURED_IN_CLOSURE] ns/op`
- **Result (nested record):** `[MEASURED_IN_CLOSURE] ns/op`

### Exact identity lookup

`find_profile` (newest active) after the registry has been populated. Measures the cost of resolving an identity to its current profile.

- **Methodology:** repeatedly `find_profile(id)` across the corpus.
- **Result:** `[MEASURED_IN_CLOSURE] ns/op`

### Pairwise evaluation

`evaluate_pair(left, right)` — the full rule-resolution path: newest-active lookup, fingerprint short-circuit, candidate-rule collection, deterministic sort, rule evaluation, and decision digest/explanation.

- **Methodology:** evaluate a set of representative pairs that include exact, compatible, incompatible, and unknown outcomes.
- **Result (exact match):** `[MEASURED_IN_CLOSURE] ns/op`
- **Result (rule-resolved pair):** `[MEASURED_IN_CLOSURE] ns/op`

### Requirement matching

`evaluate_requirement(candidate, requirement)` — a candidate is checked against a requirement profile under the `requirement` scope.

- **Methodology:** iterate candidate/requirement pairs with the rule set scoped to `requirement`.
- **Result:** `[MEASURED_IN_CLOSURE] ns/op`

### Rule evaluation

The raw cost of `eval_rule` over a single rule (constraints resolved and evaluated) without the registry's collection/sort overhead.

- **Methodology:** evaluate one rule against a fixed pair many times.
- **Result:** `[MEASURED_IN_CLOSURE] ns/op`

### Evidence lookup

`find_evidence(id)` in a populated evidence store.

- **Methodology:** repeatedly look up evidence records by id.
- **Result:** `[MEASURED_IN_CLOSURE] ns/op`

### Dependency invalidation

`propagate_invalidation(id)` in a dependency graph of realistic depth and breadth.

- **Methodology:** build a graph with several levels of dependents; time a full propagation from a leaf.
- **Result:** `[MEASURED_IN_CLOSURE] ns/op (per invalidation)`

### Matrix evaluation

`matrix(lefts, rights)` over a grid of identities.

- **Methodology:** evaluate a `K x M` matrix and report the total and per-cell cost.
- **Result (per cell):** `[MEASURED_IN_CLOSURE] ns/op`
- **Result (total for KxM):** `[MEASURED_IN_CLOSURE] ms`

### Explanation generation

`explain_text` and `explain_json` for a representative decision.

- **Methodology:** render the explanation for a fixed decision.
- **Result (text):** `[MEASURED_IN_CLOSURE] ns/op`
- **Result (json):** `[MEASURED_IN_CLOSURE] ns/op`

### Persistence save / recovery

`snapshot()` (+ `save`) and `recover()` (+ `load`) over the full corpus.

- **Methodology:** snapshot the registry and recover from the bytes; time serialization and deserialization separately.
- **Result (snapshot/save):** `[MEASURED_IN_CLOSURE] ms`
- **Result (recover/load):** `[MEASURED_IN_CLOSURE] ms`

### Historical replay

`replay_decision(id)` after a snapshot/recover cycle.

- **Methodology:** replay a set of historical decisions from a recovered registry.
- **Result:** `[MEASURED_IN_CLOSURE] ns/op`

### Concurrent read throughput

Read-only calls issued concurrently from multiple threads (e.g. `evaluate_pair` and `find_profile`), measuring throughput under the single registry lock.

- **Methodology:** spawn `T` reader threads that each run a fixed number of read operations and measure aggregate throughput.
- **Result (1 thread):** `[MEASURED_IN_CLOSURE] ops/s`
- **Result (T threads):** `[MEASURED_IN_CLOSURE] ops/s`
- **Result (scaling vs 1 thread):** `[MEASURED_IN_CLOSURE] x`

## Interpreting the numbers

- All operations are **single-locked**: the registry uses one mutex, so reads benefit from the compact critical section but do not scale with more reader cores beyond the lock's throughput. The concurrent-read metric makes this explicit.
- **No network I/O is measured or performed under the lock.** Persistence and distributed-exchange costs are excluded from the per-operation CPU metrics; `save`/`load` are measured separately as file I/O plus serialization.
- **Determinism is not a cost trade-off.** The harness does not relax determinism to get a better number; the ordered candidate-rule sort and canonical sorting are part of the measured path.
- Timed values are wall-clock on the host; the harness records the machine/compiler/configuration in its output header so results are reproducible. Treat placeholders as values to be filled at final closure.

## Reproducing

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCOMPAT_BUILD_BENCH=ON
cmake --build build --config Release
./build/Release/compat_bench
```

The executable prints its header (platform, compiler, build type, corpus size) and the metric table, then exits with a status reflecting whether any internal sanity check failed.

## Measured results (MSVC 19.44 / Release x64 / host = RTX 5090)

Run with `compat_bench.exe <N>` (N = number of kernel/device profile pairs). Matrix sized 300x300.

| Metric | N=1k | N=10k | N=100k |
|---|---|---|---|
| Register 2N profiles (incl. SHA-256 fingerprint) | 14.27 ms | 121.36 ms | 1248.95 ms |
| Exact identity lookup | 0.15 us/op | 0.13 us/op | 0.16 us/op |
| Pairwise evaluation | 12.23 us/op | 10.75 us/op | 11.04 us/op |
| Requirement matching (2N ops) | 5.38 ms | 55.68 ms | 589.80 ms |
| Matrix (90k cells) | 844 ms | 849 ms | 897 ms |
| Explanation generation (1000 ops) | 1.71 ms | 1.68 ms | 1.83 ms |
| Snapshot+recover | 53.9 ms | 616.7 ms | 7266 ms |
| Replayed decisions (1000 ops) | 9.17 ms | 9.11 ms | 10.29 ms |
| Dependency invalidation (999 nodes) | 0.41 ms | 0.58 ms | 0.71 ms |
| Snapshot size | 1.69 MB | 16.9 MB | 169 MB |
