# Architecture

This document describes how Compatibility Registry is layered, how the pieces compose, how concurrency is handled, and where the determinism guarantees come from. Named types and functions are exactly the public API declared in `include/compat/`.

## Layering

The library is built bottom-up as small, single-purpose layers. Each layer depends only on the layers below it; nothing depends upward.

### 1. Identity (`uuid.hpp`, `id.hpp`)

- `compat::Uuid` is a 128-bit identifier stored as 16 opaque bytes. Serialization is deterministic (`to_string`, `from_string` accept 32 hex digits or the canonical 8-4-4-4-12 form) and round-trips exactly. `generate_v4` builds an RFC-4122 v4 UUID from a 128-bit seed; `genseed` is a thread-safe source. Equality, ordering, and FNV-1a `hash` are all defined.
- `compat::Generation` is a monotonic, comparable generation counter. Generations are never rewritten: a new generation is a new value.
- `COMPAT_DECLARE_ID(NAME)` expands to a strongly-typed wrapper over `Uuid` — `ModelId`, `TokenizerId`, `KernelArtifactId`, `DeviceId`, `CapabilitySetId`, `ProfileId`, `CompatibilityRuleId`, `CompatibilityDecisionId`, `EvidenceId`, and the rest. Type safety prevents accidentally using a model identity where a device identity is required, while `operator Uuid()` lets a typed ID flow into APIs that take a raw `Uuid`.
- Distinct generation kinds (`RegistryGeneration`, `RuleGeneration`, `EvidenceGeneration`, `CapabilityGeneration`, `PolicyGeneration`) keep the *meaning* of a generation explicit at each level.

### 2. Canonical encoding (`canonical.hpp`, `sha256.hpp`)

- `compat::Canon` is a deterministic value tree: `Record` is an ordered map of `uint32` tag → value (always sorted by tag), `Sequence` is an ordered list (insertion order is semantically meaningful). Supported kinds: `Null`, `Uint`, `Int`, `Bool`, `Str`, `Bytes`, `Uuid`, `Float`, `Record`, `Sequence`.
- `canonical_encode` / `canonical_decode` produce and consume a strict, versioned binary format (magic `CRCF` + version byte). Encoded records are always sorted by tag, so semantically identical records encode identically regardless of field insertion order.
- `canonical_fingerprint` / `canonical_fingerprint_hex` return SHA-256 (`compat::Sha256`, FIPS 180-4, header-only) over the encoding. This is the content fingerprint used everywhere — profile fingerprints, decision digests, and integrity checks.
- `canonical_to_json` renders a deterministic, human-readable form for explanations and debugging.
- Strictness is a first-class property. `Canon::mk_record` sorts and **rejects duplicates**; `Canon::mk_float` **rejects NaN/Inf**; `canonical_decode` **rejects** malformed lengths, invalid enum values, unsupported versions, and trailing garbage.

### 3. Profiles (`profile.hpp`)

Each domain is its own thin profile struct with a `to_canon()` / `from_canon()` pair: `ModelProfile`, `TokenizerProfile`, `TensorProfile`, `KvProfile`, `KernelProfile`, `GraphProfile`, `AdapterProfile`, `BackendProfile`, `DeviceProfile`, `ProtocolProfile`, `PolicyProfile`. Field tags are namespaced per domain (`modelf`, `tokf`, `tns`, `kvf`, `kernf`, `graphf`, `adapf`, `backf`, `devf`, `protf`, `polf`), so the same numeric tag can be reused across domains without collision. Profiles compose by reference rather than by inheritance or monoliths: a model references a `TokenizerId`/`VocabularyId` and lists `AdapterId`s; a graph lists kernel artifact dependencies; a device exposes supported dtypes / instruction classes / execution features; a protocol exposes a family, major/minor, and required/optional semantics.

Heading the set is `ProfileKind`, which enumerates each domain and supplies `profile_kind_name`.

### 4. Capability sets (`capability.hpp`)

`compat::CapabilitySet` is a typed, deterministic set stored as a canonical record, carrying its own `id`, `generation`, and `Kind`. It is **tri-state**: an absent tag is *unknown / unsupported-not-observed*, which is distinct from a present value of `false`. Getters return `std::nullopt` for unknown rather than collapsing onto `false`. Authoritative changes never mutate the set in place — a changed set produces a fresh generation and identity (`to_canon` includes both, so the content identity changes).

### 5. Rule engine (`rule.hpp`)

`CompatibilityRule` carries a `domain`, a `scope` (`pair` / `requirement` / `counterfactual`), a `priority`, `active`, a `RuleKind`, optional `left_kind`/`right_kind`, and four constraint groups: `required` (conjunction), `any_of` (disjunction), `incompatible_with` (explicit incompatibility), and `conditions` (for conditional rules), plus a list of `adaptations`, an `Outcome`, and `provenance`.

`Constraint` is the atomic predicate, parameterized by `PredOp` (`Eq`, `Ne`, `InSet`, `Min`, `Max`, `VersionLt/Le/Gt/Ge`, `ArchFamily`, `FeatureReq`, `Truthy`). A constraint references a `field` (dotted path into the profile) and optionally a `right_field` to compare against the other side. `resolve_profile_field` maps a dotted name like `compute_capability` onto a numeric tag via `tag_for` (per profile kind) and, for nested capability records, `resolve_capability_subfield`. `Constraint::evaluate(left, right, …)` returns a verdict plus human-readable `why_ok` / `why_not` strings used to build the explanation.

The engine evaluates a rule in a fixed order: explicit-incompatibility clauses first (an **any** match short-circuits to `Incompatible`), then the `required` conjunction, then the `any_of` disjunction, then a kind-specific result for `Conditional`, `Adaptation`, `ExplicitIncompat`, or the rule's default outcome. When a needed field is absent, the rule is marked missing and the decision records `InsufficientEvidence` rather than guessing.

### 6. Decision (`decision.hpp`, `outcome.hpp`)

`Outcome` is the explicit answer set: `Exact`, `Compatible`, `CompatibleWithAdaptation`, `Conditional`, `Incompatible`, `Unknown`, `InsufficientEvidence`. `is_positive` marks the positive outcomes. `CompatibilityDecision` bundles a deterministic `decision_id`, left/right identities, outcome, the applied `rule_id`/`rule_generation`, `policy_id`/`policy_generation`, `satisfied`/`failed` constraints, `adaptations`, `missing_evidence`, `provenance`, the `registry_generation`, a `counterfactual` flag, a deterministic `explanation`, and a `digest`.

`compute_digest` hashes the **semantic outcome fields** (not the free-form explanation text), so replays reproduce it exactly. Evidence is typed by `EvidenceKind` (`Measured`, `Reported`, `Derived`, `Validated`, `Reconstructed`, `Unavailable`) and stored as `EvidenceRecord`, which carries `subject`, `field`, `value`, `provenance`, `source`, `source_generation`, timestamp, validation flag, and a content fingerprint.

### 7. Registry (`registry.hpp`)

`CompatibilityRegistry` is the authoritative, thread-safe owner of everything above. It exposes profile lifecycle (`register_profile`, `supersede_profile`, `invalidate_profile`, `find_profile`, `query_by_kind`, `history_of`), rule lifecycle (`register_rule`, `supersede_rule`, `disable_rule`, `active_rules`), evidence (`register_evidence`, `find_evidence`), policy (`set_policy`), evaluation (`evaluate_pair`, `evaluate_requirement`, `find_decision`, `replay_decision`), the dependency graph (`dependencies_of`, `dependents_of`, `has_cycle`, `propagate_invalidation`), counterfactual (`counterfactual_pair`), persistence (`snapshot`, `save`, `load`, `recover`), the matrix (`matrix`), and diagnostics (`stats`, `current_registry_generation`, `explain_text`, `explain_json`).

### 8. Dependency graph

Profiles register a list of dependency identities. The registry keeps a forward map (`id → deps`) and a reverse map (`id → dependents`). `add_dependency` / `remove_dependency` keep both in sync. `create_cycle` builds a temporary graph with the prospective edges and runs a DFS (`dfs_cycle`); if a cycle would form, the mutation is **rejected before any authoritative state changes**. `has_cycle` reports whether the current graph has a cycle. `propagate_invalidation` walks the reverse map from a seed, marking every dependent superseded with a recorded `Invalidation`.

### 9. Persistence

`snapshot()` builds a canonical record containing the registry generation, policy, all profile history, all rules, all evidence, all invalidations, and the decision cache; encodes it; wraps it in a versioned container (magic `CRREG`, version 1); and appends a SHA-256 checksum. `recover()` validates the container, recomputes and compares the checksum, verifies each profile fingerprint, rebuilds dependency edges, and rejects a snapshot that contains a dependency cycle. `save()` / `load()` are thin `ofstream`/`ifstream` wrappers around `snapshot` / `recover`.

### 10. Generation, invalidation, replay, counterfactual

- **Generation**: every profile, rule, evidence record, capability set, and policy has a generation. Mutations create new generations; they never rewrite old ones. The registry itself advances `registry_gen_` on every authoritative mutation.
- **Invalidation**: `invalidate_profile` and `propagate_invalidation` append a first-class `Invalidation` (`target`, `generation`, `reason`, `fingerprint_before`) and mark the affected profile(s) superseded.
- **Replay**: the decision cache is serialized with the snapshot, so `replay_decision` returns historical decisions, and `recover` regenerates `decision_id`s exactly.
- **Counterfactual**: `counterfactual_pair` copies the newest active profiles, applies `CounterfactualEdit`s via `apply_profile_field_edit` on a private copy, and evaluates under the `counterfactual` scope. The result is flagged `counterfactual = true` and **never mutates authoritative state**.

### 11. Distributed coordinator protocol

The registry itself is single-process and single-locked. Distributed authority is expressed through the data model rather than inside the registry: `EvidenceRecord.source` and `source_generation` identify which producer contributed which generation of evidence, and the registry exposes the relevant generation state (`RegistryGeneration`, per-profile/evidence/policy generations). A coordinator and its sources/consumers exchange framed messages containing profiles, evidence, and generations at the integration boundary. Because the registry guarantees **no network I/O under its lock**, all transport happens outside the registry; the registry only decides, on the basis of generation, whether an incoming mutation is current or stale. A mutation at an older generation than the registry's current one for that source is rejected.

### 12. CUDA discovery

`COMPAT_BUILD_CUDA` (default ON) runs `find_package(CUDAToolkit)`. When found, `COMPAT_HAVE_CUDA` is set, the public includes are added, and the library links `CUDA::cudart` and `CUDA::cuda_driver`. When not found, `COMPAT_HAVE_CUDA=0` and CUDA discovery/proofs are compiled out. Device discovery reads real device properties into a `DeviceProfile` (compute capability, architecture, memory, supported dtypes, instruction classes, execution features, runtime/driver minimums). Unavailable hardware is never fabricated: absent properties stay absent, so they are treated as unknown rather than present.

### 13. CLI

`src/cli/main.cpp` builds the `compat` executable (`COMPAT_BUILD_CLI`) and links the `compat` library. It is a thin driver over the registry API and is compiled with `-W4 /WX` on MSVC.

### 14. Benchmarks

`benchmarks/bench.cpp` builds the `compat_bench` executable (`COMPAT_BUILD_BENCH`). It measures the operations described in `BENCHMARKS.md`. See that file for metrics and methodology.

## Thread safety and locking

All mutable registry state is guarded by a single `std::mutex` member (`mutex_`). Every public method that reads or writes state takes `std::lock_guard`. The evaluator has an internal, lock-free core (`evaluate_locked`) that callers must invoke **without** holding the lock; public `evaluate_pair` / `evaluate_requirement` / `matrix` / `counterfactual_pair` acquire the lock and then call it.

Two invariants follow directly from this design:

1. **Single lock.** There is exactly one registry mutex and no second lock is ever acquired while it is held. No lock-order inversion is possible for the registry itself.
2. **No network (or blocking) I/O under the lock.** The header documents this explicitly and the implementation honors it. Distributed authority, persistence to a remote store, or any producer/consumer exchange must happen **outside** the registry lock — the registry receives the already-fetched bytes and works on them under the lock only for CPU-only validation and mutation.

## Determinism guarantees

Determinism is the central correctness property and is enforced at every layer:

- **Canonical identities are byte-deterministic.** Records are sorted by tag at construction and again at encode time; `canonical_encode` is a pure function of semantic content. Identical content produces identical bytes and an identical SHA-256 fingerprint on every run and platform, independent of map iteration order, pointer values, wall clock, scheduling, or locale.
- **Rule resolution is deterministic.** Candidate rules are collected from the storage, filtered by scope and kind, then sorted by `priority` (descending) with a tie-break on `rule_id`. The highest-priority definite result wins, and any conflict is surfaced in the decision's `provenance` rather than resolved silently or arbitrarily.
- **Decision digests are deterministic.** `compute_digest` hashes only the semantic outcome fields, never the free-form `explanation`; so the same inputs under the same registry generation reproduce the same digest, and therefore the same `decision_id`.
- **Generation is part of identity.** Because the registry generation and rule/policy generations feed the decision id and digest, escalating the registry produces a distinct new decision rather than silently reusing an old one.
- **Evaluation is side-effect-free on authoritative state.** `evaluate_pair` / `evaluate_requirement` only mutate the decision cache; `counterfactual_pair` never mutates authoritative state at all.

## File map

```
CMakeLists.txt                      # build, options, install, /W4 /WX, targets
cmake/CompatibilityRegistryConfig.cmake.in  # find_package package config
include/compat/
  compat.hpp                        # public umbrella header
  uuid.hpp                          # 128-bit identifier
  id.hpp                            # Generation, COMPAT_DECLARE_ID, generation kinds
  sha256.hpp                        # FIPS 180-4 SHA-256 (header-only)
  canonical.hpp                     # Canon tree, encode/decode, fingerprint, JSON
  profile.hpp                       # per-domain profiles + field tags
  capability.hpp                    # typed tri-state capability set
  rule.hpp                          # CompatibilityRule, Constraint, PredOp, RuleKind
  outcome.hpp                       # Outcome enum and helpers
  decision.hpp                      # EvidenceRecord, ConstraintItem, CompatibilityDecision
  registry.hpp                      # CompatibilityRegistry, snapshot/recover types
src/compat/
  compat.cpp                        # Uuid::genseed
  canonical.cpp                     # encoding/decoding/fingerprint/JSON
  rule.cpp                          # field resolution + Constraint::evaluate
  registry.cpp                      # registry, evaluation, graph, persistence
src/cli/main.cpp                    # compat CLI
tests/                              # core.cpp, probe.cpp, testutil.hpp
examples/                           # runnable examples (see EXAMPLES.md)
benchmarks/bench.cpp                # compat_bench harness
LICENSE NOTICE CONTRIBUTING.md README.md ARCHITECTURE.md EXAMPLES.md BENCHMARKS.md
```

## How the pieces compose

End to end, a single query flows like this:

1. A consumer builds `Canon` payloads (via a profile's `to_canon()`) and registers them with `register_profile`, optionally listing dependencies. `canonical_fingerprint_hex` derives each profile's content fingerprint.
2. The consumer registers `CompatibilityRule`s (with `Constraint`s) and optionally `EvidenceRecord`s and a `PolicyId`.
3. `evaluate_pair(left, right)` acquires the single lock, calls the lock-free `evaluate_locked`, which: resolves the newest active profiles; short-circuits to `Exact` when fingerprints match; otherwise collects and deterministically orders applicable active rules; evaluates each; selects the best definite result (or falls back to `InsufficientEvidence` / `Unknown`); and records the decision id, digest, and explanation. The decision is cached, and `explain_text` / `explain_json` render it.
4. `matrix(lefts, rights)` repeats step 3 over a grid and tallies deterministic `MatrixResult` counts.
5. `snapshot()` / `save()` capture the whole authoritative state (history included); `recover()` / `load()` re-validate it and rebuild the graph, reproducing historical decisions.

Every layer is interchangeable only through the public API; there is no hidden side channel, and no operation makes a compatibility claim without deterministic evidence and its provenance.
