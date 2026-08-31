# Contributing to Compatibility Registry

Thank you for considering a contribution to **Compatibility Registry**. This project is built around a single, non-negotiable idea: the registry must answer a narrow question — *what exact evidence proves that two components are compatible, conditionally compatible, incompatible, or unknown* — and it must answer that question **deterministically**. Please keep that idea front and center in every change you propose.

## Getting started

1. Read the public headers in `include/compat/` (start with `compat.hpp`, then `canonical.hpp`, `rule.hpp`, `decision.hpp`, `registry.hpp`). The documentation mirrors exactly what is declared there; if the headers do not declare it, it is not part of the API.
2. Read `CMakeLists.txt` and `tests/core.cpp` to understand the build graph and how the registry is exercised.
3. Configure and build with CMake (MSVC-friendly, Windows-first, C++20):

   ```
   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
   cmake --build build --config Release
   ctest --test-dir build -C Release
   ```

## License and legal

- The project is licensed under the **Apache License 2.0**. See `LICENSE` for the full text and `NOTICE` for the attribution notice.
- Contributions are accepted **without requiring a Contributor License Agreement (CLA)**. By submitting a contribution you agree that it is subject to the terms of the Apache License 2.0.
- Retain all existing copyright, patent, trademark, and attribution notices. Do not add an unintended "Co-authored-by" trailer to commits — keep the commit authorship attributable to the *actual* author of the change. Git trailers should only be present when the change genuinely involves multiple authorship.

## Build and code-quality expectations

- **Warning-free builds are mandatory.** Both **Release** and **Debug** configurations must compile cleanly under **MSVC with `/W4` and `/WX`** (warnings-as-errors). A change that adds a warning anywhere in the build — library, CLI, tests, examples, or benchmarks — is a regression and will be rejected.
- Keep the code C++20 (no extensions off by default per `CMakeLists.txt`).
- Do not introduce new files outside the existing directory layout unless necessary: public headers in `include/compat/`, implementation in `src/compat/`, tests in `tests/`, examples in `examples/`, benchmarks in `benchmarks/`.

## Determinism is a hard invariant

The registry's core promise is reproducibility. **A change that makes a canonical compatibility identity or rule evaluation nondeterministic is not a bug fix — it is a correctness violation.** In particular:

- **Canonical identities must stay deterministic.** The canonical encoding (`canonical_encode` / `canonical_decode`), the SHA-256 `canonical_fingerprint`, and the typed 128-bit identities must produce identical bytes for identical semantic content, regardless of insertion order, platform, or run. Do not introduce any dependence on map iteration order, pointer values, wall-clock time, thread scheduling, or locale.
- **Rule semantics must stay deterministic.** Rule resolution orders candidates by priority (descending) then by `rule_id`. Evaluation must not depend on hash iteration order of the registry's internal containers. Keep the deterministic tie-break intact.
- **Decision digests must stay reproducible.** `CompatibilityDecision::compute_digest` hashes the *semantic outcome fields*, not the free-form explanation text. Do not move the explanation into the digest, and do not change which fields are included without a migration story and an accompanying test.
- Every new feature must be accompanied by a test that asserts deterministic behavior (for example: build the same content in two different orderings, and assert identical fingerprints and identical decision digests).

## Distinctions that must never be collapsed

The registry deliberately keeps several outcomes and states distinct. **Preserve these distinctions exactly:**

- **Exact vs Compatible vs CompatibleWithAdaptation vs Conditional vs Incompatible vs Unknown vs InsufficientEvidence.** Never collapse `Unknown` onto `InsufficientEvidence`, or either onto `Incompatible`. They carry different semantics and different consequences.
- **Missing evidence must remain visible.** `missing_evidence` is populated whenever a rule references a field that is not present. Do not silently upgrade missing evidence into a positive result.
- **Unknown must remain unknown.** Absence of evidence is **not** evidence of compatibility. Missing evidence must never be treated as implicit compatibility.
- **Absent capability ≠ false.** `CapabilitySet` uses tri-state semantics: a tag that is absent is *unknown/unsupported-not-observed* and is distinct from a present value of `false`. Preserve that distinction; getters return `std::nullopt` for unknown rather than collapsing onto `false`.

## Provenance and evidence

- `EvidenceRecord` carries `source`, `source_generation`, `kind` (measured / reported / derived / validated / reconstructed / unavailable), `validated`, and a fingerprint. Keep these fields populated honestly — do not re-label evidence in a way that would misrepresent how it was obtained.
- When your change affects how evidence is collected or replayed, confirm that stale evidence cannot mutate a current profile and that historical decisions remain reproducible.

## Thread safety and locking

- All mutable registry state is guarded by a single `std::mutex`. Do not add a second lock that can be acquired while the registry lock is held.
- **No network I/O (or any blocking I/O) may occur under the registry lock.** Read `src/compat/registry.cpp` to confirm this invariant holds; preserve it.

## Testing and examples

- Add or update tests in `tests/` (see `tests/core.cpp` for the harness conventions: `TEST(...)`, `CHECK(...)`, `RUN_TESTS()`).
- Keep the runnable examples under `examples/` working (they are built by the `COMPAT_BUILD_EXAMPLES` option) and their walkthroughs in `EXAMPLES.md` accurate.
- If you change serialization, update the recovery tests to assert that malformed, truncated, corrupted, duplicate, invalid-enum, impossible-generation, dependency-cycle, NaN/Inf/overflow, unsupported-version, and trailing-garbage inputs are all rejected.

## Commit hygiene

- Keep commits focused and self-contained; one logical change per commit.
- Do not add "Co-authored-by", "Co-authored-with", or similar trailers unless the commit genuinely represents joint authorship by the named people.
- Update the relevant documentation (`README.md`, `ARCHITECTURE.md`, `EXAMPLES.md`, `BENCHMARKS.md`) when you change observable behavior.

Thank you for keeping Compatibility Registry deterministic, auditable, and honest.
