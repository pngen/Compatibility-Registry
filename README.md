# Compatibility Registry 1.0.0

An open-source, vendor-neutral registry that answers one narrow question with one authoritative, reproducible answer: **what exact evidence proves that two components (a kernel and a device, a model and a tokenizer, a graph and a runtime, a protocol and a peer, an adapter and a base model) are compatible, conditionally compatible, incompatible, or unknown?**

C++20 · Windows-first · MSVC `/W4 /WX` · Apache License 2.0 · Summon Software Labs.

## Systems boundary

The registry **owns** only what is needed to answer the compatibility question honestly and reproducibly:

- **Canonical compatibility identities** of model, tokenizer, tensor, KV, kernel, graph, adapter, backend, device, protocol, policy, and capability-set profiles.
- **Evidence** (first-class records) about those identities.
- **Rules** that decide compatibility between identities.
- **Decisions** with a deterministic digest and a full explanation.
- **Generations**, **provenance**, **persistence** (snapshot / save / load / recover), and **historical replay**.

The registry does **not** own caching, residency, scheduling, compilation, placement, or execution — it decides *whether* a pairing is compatible; it neither runs it nor decides where or when it runs. Those concerns belong to the consumer.

## The core question

Given two identities, what exact evidence proves:

- **Compatible** — a rule establishes compatibility, or the two are the same canonical identity.
- **Conditional** — compatible only subject to stated conditions.
- **Incompatible** — a rule establishes incompatibility (rejected before any execution).
- **Unknown** — no rule applies and there is no positive evidence.

The registry never has to guess. `Outcome` enumerates exactly: `Exact`, `Compatible`, `CompatibleWithAdaptation`, `Conditional`, `Incompatible`, `Unknown`, and `InsufficientEvidence`.

## Compatibility doctrine

- A decision is a **correctness** decision, not a probabilistic or heuristic one.
- **Never infer compatibility from a filename, name, family, vendor, or semantic similarity.** Identity is canonical; judgments come from evidence and explicit rules only.
- Distinguish, and never collapse: **exact** (identical canonical identity) · **compatible-by-rule** (`Compatible`) · **compatible-with-adaptation** (`CompatibleWithAdaptation`) · **conditional** (`Conditional`) · **incompatible** (`Incompatible`) · **unknown** (`Unknown`) · **insufficient-evidence** (`InsufficientEvidence`).
- **Unknown must remain unknown.** Absence of evidence is not evidence of compatibility.
- **Missing evidence never becomes implicit compatibility.** Missing fields are surfaced in `missing_evidence` and in every explanation.
- **Every decision is reproducible.** Each decision carries a deterministic digest over its semantic outcome fields, so replays reproduce it byte-for-byte.

## Canonical identities

Identities are **128-bit typed IDs** built on `compat::Uuid` (16 opaque bytes). The `COMPAT_DECLARE_ID` macro produces strongly-typed wrappers — `ModelId`, `TokenizerId`, `KernelArtifactId`, `DeviceId`, `AdapterId`, `ProfileId`, `CapabilitySetId`, and so on — so a model ID can never be silently used where a device ID is expected. Serialization is deterministic and round-trips exactly (32 hex digits or canonical 8-4-4-4-12 form).

## Canonical encoding

`compat::Canon` is a **deterministic, strict, binary, hashable** value tree:

- Deterministic: a record is an ordered map of `uint32` tag → value, always encoded **sorted by tag** (order-stable). Sequence preserves insertion order (semantically meaningful).
- Versioned: encoded bytes start with magic `CRCF` and a version byte.
- Hashable: `canonical_fingerprint` / `canonical_fingerprint_hex` return the SHA-256 over the canonical encoding; identical content always produces an identical fingerprint regardless of insertion order.
- Strict: `canonical_decode` and `Canon::mk_record` **reject** duplicate tags, malformed lengths, invalid enum values, unsupported versions, NaN/Inf floats, and trailing garbage.

## Profiles

Each domain is its own thin profile (`ModelProfile`, `TokenizerProfile`, `TensorProfile`, `KvProfile`, `KernelProfile`, `GraphProfile`, `AdapterProfile`, `BackendProfile`, `DeviceProfile`, `ProtocolProfile`, `PolicyProfile`), plus `CapabilitySet`. Each has `to_canon()` and `from_canon()`. Profiles **compose via references** (a model references a tokenizer and vocabulary IDs; a graph lists kernel artifact dependencies; an adapter references a base-model revision) rather than forcing one monolithic type. A profile is immutable by generation; changing it creates a new generation.

## Rule engine

`CompatibilityRule` expresses domain-typed constraints (`required` conjunction, `any_of` disjunction, `incompatible_with`, `conditions`) and an `Outcome`. `Constraint` operators (`PredOp`) cover:

- **Equality** (`eq` / `ne`) and **set membership** (`in_set`).
- **Range** (`min` / `max`) and **min/max capability**.
- **Version relation** (`version_lt/le/gt/ge`, `semver`) and **arch-family**.
- **Feature requirement** (`feature_req`, `truthy`).
- Composition through **conjunction** (`required`), **disjunction** (`any_of`), **explicit incompatibility**, **adaptation**, **conditional**, and **dependency**; plus a **policy override** kind.

Candidate rules are sorted **deterministically** (priority descending, then `rule_id`), and conflicts are surfaced explicitly in the decision's provenance — the highest-priority rule wins, and the tie-break is stable.

## Exact vs derived compatibility

A decision records `provenance`. Exact matches are proven by equal canonical fingerprints; every other outcome names the rule and generation that produced it. Counterfactual queries (`counterfactual_pair`, with `CounterfactualEdit`) are marked `counterfactual = true` and are **derived, never authoritative** — they never mutate registry state.

## Capability sets

`CapabilitySet` is a typed, deterministic set over canonical values with tri-state semantics: **absent ≠ false**. An absent tag is *unknown / unsupported-not-observed*; getters (`get_bool`/`get_uint`/`get_float`/`get_str`/`get_uint_sub`) return `std::nullopt` for unknown so callers never collapse unknown onto `false`. Capability state is never mutated in place by authoritative change — a changed set produces a fresh generation and identity.

## CUDA / hardware discovery

Discovery produces real device profiles from the actual hardware (for example an RTX 5090 has compute capability `12.0` / architecture `sm_120`), with the real device properties observed on that machine. When the CUDA toolkit is present (`COMPAT_HAVE_CUDA`), registry builds link the CUDA runtime and driver; when it is absent, CUDA proofs are **disabled** rather than fabricated. **Never fabricate hardware that is not present** — an unknown or absent property stays unknown.

## Compatibility matrix

`matrix(lefts, rights)` evaluates every pair and returns deterministic `MatrixResult` counts (`exact`, `compatible`, `compatible_with_adaptation`, `conditional`, `incompatible`, `unknown`, `insufficient_evidence`) plus per-outcome reason counts and the individual cells.

## Registry model

`CompatibilityRegistry` is **thread-safe** (one internal `std::mutex`) and **immutable by generation**:

- register_profile / supersede_profile / invalidate_profile / find_profile / query_by_kind / history_of
- register_rule / supersede_rule / disable_rule / active_rules
- register_evidence / find_evidence / set_policy
- evaluate_pair / evaluate_requirement / find_decision / replay_decision
- dependencies_of / dependents_of / has_cycle / propagate_invalidation
- snapshot / save / load / recover
- matrix / stats / current_registry_generation / explain_text / explain_json

Superseding an identity **never mutates a historical record**; it marks the old generation superseded and adds a fresh one at generation+1.

## Evidence

Evidence is **first-class** (`EvidenceRecord`) and typed by `EvidenceKind`: `Measured`, `Reported`, `Derived`, `Validated`, `Reconstructed`, `Unavailable`. Each record carries its subject, field, value, `provenance`, `source`, `source_generation`, observed timestamp, validation flag, and a content fingerprint. Missing evidence is visible in explanations via `missing_evidence`.

## Generation semantics

- **Stale evidence cannot mutate current profiles.** A profile is immutable by generation; evidence changes create new generations, never edits.
- **Old rule generations do not govern fresh evaluations.** Only active rules participate; superseded or disabled rules are excluded.
- **Historical decisions remain reproducible.** Prior snapshots contain their decisions; `replay_decision` returns them.
- **Reevaluation produces a new decision.** Because the registry generation is part of the decision identity, the same pair evaluated later yields a distinct decision id.

## Invalidation

An invalidation (`invalidate_profile`, `propagate_invalidation`) is recorded as a first-class `Invalidation` with target, generation, reason, and the fingerprint before the change — so **every invalidation is explainable**, not just a silent flip.

## Dependency graph

Profiles register dependencies; `dependencies_of` / `dependents_of` walk them. `has_cycle` runs a DFS and **cycle-creating registrations are rejected** before any authoritative mutation (`create_cycle` checks the prospective edges first). `propagate_invalidation` recursively marks dependents superseded with a recorded reason.

## Historical replay

A snapshot serializes all profiles (including history), rules, evidence, invalidations, and the decision cache. `recover` verifies integrity and **rejects** a snapshot that contains a dependency cycle or a corrupted fingerprint, and regenerates dependency edges and decision ids exactly as before, so replay reproduces historical decisions.

## Distributed authority

Evidence carries a `source` identity and `source_generation`, giving a **source/consumer** model of authority: a registry that receives evidence from multiple producers can track, via generation, which evidence is current for a given source and can **reject stale mutations** (a mutation at an older generation is discarded). The registry itself is single-locked, and by contract **no network I/O ever happens under its lock** — any coordinator exchange (framed transport of profiles, evidence, and generations between a coordinator and its sources/consumers) occurs at the integration boundary, outside the registry. The registry exposes the generation state (`RegistryGeneration`, `source_generation`, per-profile/evidence/policy generations) that such an authority uses to sequence and reconcile updates.

## CUDA compatibility proof

On a target like `sm_120`:

- **Exact match** — a kernel built for `sm_120` on a device with compute capability `12.0` is proven `Compatible` (or `Exact` when fingerprints match).
- **Incompatible rejection before execution** — a kernel that requires a higher compute capability than the device is `Incompatible`, decided *before* any execution happens.
- **dtype / layout mismatch** — dtype and layout are explicit profile fields; mismatches surface as failed constraints.
- **Stale artifact generation** — a kernel/graph artifact carries a `generation`; a stale artifact generation disqualifies it from governing a fresh evaluation.
- **runtime / toolchain generation change** — a changed runtime or compiler version produces a new artifact generation, so an old pairing no longer holds.

## Persistence

Persistence is **strict, versioned, and checksummed**. The container (`CRREG`, version 1) encodes the whole registry, then appends a SHA-256 checksum. `recover` / `load` **reject**: malformed or truncated bytes, corrupt containers (bad checksum), duplicate or invalid enums, impossible generations, dependency cycles, NaN/Inf/float overflow, unsupported container versions, trailing garbage, and profile fingerprint mismatches.

## Benchmarks

See `BENCHMARKS.md` for the harness and the measured metrics (canonical profile construction, SHA-256 fingerprinting, exact identity lookup, pairwise evaluation, requirement matching, rule evaluation, evidence lookup, dependency invalidation, matrix evaluation, explanation generation, persistence save/recovery, historical replay, and concurrent read throughput).

## Build, install, use

Requires CMake ≥ 3.24 and a C++20 compiler (MSVC on Windows). Configure with options:

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release
```

MSVC builds compile with `/W4 /WX` (warnings-as-errors); both **Release and Debug must be warning-free**. Library targets: `compat` and `CompatibilityRegistry::compat`. The CLI `compat` is built when `COMPAT_BUILD_CLI=ON`.

Install and consume via **find_package**:

```
find_package(CompatibilityRegistry 1.0 REQUIRED)
target_link_libraries(my_app PRIVATE CompatibilityRegistry::compat)
```

Include the public umbrella header `compat/compat.hpp` (all types are in namespace `compat`). See `EXAMPLES.md` for runnable walkthroughs and `ARCHITECTURE.md` for the design.

## License
Apache License 2.0. Copyright 2026 Summon Software Labs. No telemetry transmission.
