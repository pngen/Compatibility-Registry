# Examples

Each section is a runnable walkthrough that links to the intended example under `examples/` (built by the `COMPAT_BUILD_EXAMPLES` CMake option as `example_<name>`). The walkthroughs are written directly against the real public API declared in `include/compat/`; the same calls you see here are what each example program performs. A small convenience helper used throughout,

```cpp
#include "compat/compat.hpp"
using namespace compat;

static void show(const CompatibilityRegistry& r, const CompatibilityDecision& d) {
    std::printf("%s\n", r.explain_text(d).c_str());
}
```

prints the deterministic explanation for a decision.

## 1. Exact tensor compatibility

**File:** `examples/exact_tensor.cpp`

Two tensors are `EXACT` when their canonical descriptors are byte-identical. Build a `TensorProfile`, convert to `Canon` with `to_canon()`, register it twice (or register a second copy with the same fields), and evaluate the pair. Because the registry is idempotent on identical content, both registers return the same identity, and `evaluate_pair(left, left)` returns `Outcome::Exact`.

```cpp
TensorProfile t;
t.schema_id = TensorSchemaId::genseed();
t.rank = 2;
t.shape = {4096, 4096};
t.dtype = "f16";
t.layout = "row_major";
t.strides = {4096, 1};
t.endianness = "little";
t.semantic_role = "weight";

ProfileId a = reg.register_profile(ProfileKind::Tensor, t.to_canon());
ProfileId b = reg.register_profile(ProfileKind::Tensor, t.to_canon());   // identical content

CompatibilityDecision d = reg.evaluate_pair(a.value, a.value);   // exact match
show(reg, d);  // -> outcome: EXACT
```

**Expected outcome:** `Outcome::Exact`.
The explanation shows the identical canonical fingerprint; the registry short-circuits before considering any rule.

## 2. Tensor mismatch

**File:** `examples/tensor_mismatch.cpp`

A semantic change — a different `dtype` or `layout` — changes the fingerprint. Register two tensors that differ in one field, and register a rule requiring equal `dtype`. The pair is now evaluated by the rule; a mismatch surfaces as a failed constraint.

```cpp
TensorProfile t1; t1.dtype = "f16"; /* ... */
TensorProfile t2; t2.dtype = "bf16"; /* ... */
ProfileId p1 = reg.register_profile(ProfileKind::Tensor, t1.to_canon());
ProfileId p2 = reg.register_profile(ProfileKind::Tensor, t2.to_canon());

CompatibilityRule eq; eq.scope = "pair";
eq.left_kind = ProfileKind::Tensor; eq.right_kind = ProfileKind::Tensor;
eq.kind = RuleKind::Equality; eq.outcome = Outcome::Compatible;
Constraint c; c.op = PredOp::Eq; c.field = "dtype"; c.right_field = "dtype";
eq.required.push_back(c);
reg.register_rule(eq);

show(reg, reg.evaluate_pair(p1.value, p2.value)); // -> failed: field values differ
```

**Expected outcome:** `Outcome::Unknown` (no definite rule establishes compatibility and no incompatibility clause fires) with a visible failed constraint on `dtype`, and — if you instead register an explicit `incompatible_with` clause — `Outcome::Incompatible`. Semantically different canonical identities are never `EXACT`.

## 3. Model / tokenizer compatibility

**File:** `examples/model_tokenizer.cpp`

A `ModelProfile` references a `TokenizerId` and `VocabularyId`. Register a tokenizer and a model that points at it, then add a rule that a model's tokenizer must match the tokenizer's `tokenizer_id`.

```cpp
TokenizerProfile tok; tok.tokenizer_id = TokenizerId::genseed();
tok.vocabulary_id = VocabularyId::genseed(); tok.vocab_size = 32000;
tok.normalization = "byte-bpe";
ProfileId tokId = reg.register_profile(ProfileKind::Tokenizer, tok.to_canon());

ModelProfile m; m.model_id = ModelId::genseed();
m.revision_id = ModelRevisionId::genseed();
m.tokenizer_id = tok.tokenizer_id; m.vocabulary_id = tok.vocabulary_id;
m.dtype = "f16";
ProfileId modelId = reg.register_profile(ProfileKind::Model, m.to_canon(), {tokId.value}, "built from official weights");

CompatibilityRule r; r.scope = "pair"; r.domain = "model-tokenizer";
r.left_kind = ProfileKind::Model; r.right_kind = ProfileKind::Tokenizer;
Constraint t; t.op = PredOp::Eq; t.field = "tokenizer_id"; t.right_field = "tokenizer_id";
r.required.push_back(t); r.outcome = Outcome::Compatible;
reg.register_rule(r);

show(reg, reg.evaluate_pair(modelId.value, tokId.value)); // -> COMPATIBLE
```

**Expected outcome:** `Outcome::Compatible`. The model's `tokenizer_id` equals the tokenizer's own `tokenizer_id`; the rule's required equality is satisfied, and the dependency edge from model → tokenizer is available via `dependencies_of(modelId)`.

## 4. Adapter / base-model compatibility

**File:** `examples/adapter_base_model.cpp`

An `AdapterProfile` names a `base_model_revision`. Register a base model and an adapter bound to that revision, then rule-check that the adapter's base-model revision matches the model's `revision_id`.

```cpp
// base model with revision R
ModelProfile base; base.revision_id = ModelRevisionId::genseed(); /* ... */
ProfileId baseId = reg.register_profile(ProfileKind::Model, base.to_canon());

AdapterProfile ad; ad.adapter_id = AdapterId::genseed();
ad.base_model_revision = base.revision_id.to_string();
ad.kind = "lora"; ad.dtype = "f16"; ad.composition = "add";
ProfileId adId = reg.register_profile(ProfileKind::Adapter, ad.to_canon());

CompatibilityRule r; r.scope = "pair"; r.domain = "adapter-base";
r.outcome = Outcome::Compatible;
Constraint c; c.op = PredOp::Eq; c.field = "base_model_revision"; c.right_field = "revision_id";
r.required.push_back(c);
reg.register_rule(r);

show(reg, reg.evaluate_pair(adId.value, baseId.value)); // -> COMPATIBLE
```

**Expected outcome:** `Outcome::Compatible` when the adapter is bound to the base revision; `Outcome::Incompatible` (with an explicit incompatibility clause) when it targets a different revision.

## 5. KV-format compatibility

**File:** `examples/kv_format.cpp`

A `KvProfile` carries `layer_count`, `head_count`, `head_dim`, `dtype`, `layout`, `positional`, and `seq_context`. Verify that two KV caches of matching geometry are compatible, and that a mismatch (e.g. a different `head_dim`) fails.

```cpp
KvProfile k1; k1.layer_count = 32; k1.head_count = 32; k1.head_dim = 128;
k1.dtype = "f16"; k1.layout = "blocked"; k1.positional = "rotary"; k1.seq_context = 8192;
KvProfile k2 = k1; k2.head_dim = 64;   // mismatch
ProfileId a = reg.register_profile(ProfileKind::Kv, k1.to_canon());
ProfileId b = reg.register_profile(ProfileKind::Kv, k2.to_canon());

CompatibilityRule r; r.outcome = Outcome::Compatible;
Constraint c; c.op = PredOp::Eq; c.field = "head_dim"; c.right_field = "head_dim";
r.required.push_back(c);
reg.register_rule(r);

show(reg, reg.evaluate_pair(a.value, b.value)); // -> UNKNOWN, failed on head_dim
```

**Expected outcome:** `Outcome::Unknown` (or `Incompatible` if an explicit incompatibility clause is added). Only identical KV geometry is `EXACT`; any mismatch is surfaced as a failed constraint.

## 6. Kernel / device compatibility

**File:** `examples/kernel_device.cpp`

The canonical CUDA example: a `KernelProfile` targets a compute capability; a `DeviceProfile` exposes what the device supports. Register a kernel and a device, add a rule that the kernel's `compute_capability` must be at most the device's `compute_capability` (`PredOp::VersionLe`), and a matching `incompatible_with` clause (`VersionGt`).

```cpp
KernelProfile k; k.kernel_artifact_id = KernelArtifactId::genseed();
k.compute_capability = "12.0"; k.architecture = "sm_120"; k.abi = "cuda";
k.runtime = "cudart"; k.compiler = "nvcc"; k.compiler_version = "12.9";
ProfileId kId = reg.register_profile(ProfileKind::Kernel, k.to_canon());

DeviceProfile d; d.device_id = DeviceId::genseed();
d.compute_capability = "12.0"; d.architecture = "sm_120";
ProfileId dId = reg.register_profile(ProfileKind::Device, d.to_canon());

CompatibilityRule r; r.scope = "pair"; r.domain = "kernel-device";
r.left_kind = ProfileKind::Kernel; r.right_kind = ProfileKind::Device;
Constraint le; le.op = PredOp::VersionLe; le.field = "compute_capability"; le.right_field = "compute_capability";
r.required.push_back(le);
Constraint gt; gt.op = PredOp::VersionGt; gt.field = "compute_capability"; gt.right_field = "compute_capability";
r.incompatible_with.push_back(gt);
r.outcome = Outcome::Compatible;
reg.register_rule(r);

show(reg, reg.evaluate_pair(kId.value, dId.value)); // -> COMPATIBLE
```

**Expected outcome:** `Outcome::Compatible` for a kernel built for `12.0` on a device with compute capability `12.0`, and `Outcome::Incompatible` when a kernel requires a higher capability than the device — decided *before* any execution. A kernel built for `10.0` on a `12.0` device is also `Compatible` (capability ≥ requirement).

## 7. Graph / runtime compatibility

**File:** `examples/graph_runtime.cpp`

A `GraphProfile` lists kernel artifact `dependencies`, a `runtime`, a `backend`, and a `generation`. Verify a graph against a runtime by requiring an equal `runtime`.

```cpp
GraphProfile g; g.graph_artifact_id = GraphArtifactId::genseed();
g.runtime = "cudart"; g.backend = "cuda"; g.generation = 3;
g.dependencies = {kId};   // kernel artifact ids
ProfileId gId = reg.register_profile(ProfileKind::Graph, g.to_canon());

CompatibilityRule r; r.outcome = Outcome::Compatible;
Constraint c; c.op = PredOp::Eq; c.field = "runtime"; c.right_field = "runtime";
r.required.push_back(c);
reg.register_rule(r);

show(reg, reg.evaluate_pair(gId.value, runtimeId.value)); // -> COMPATIBLE
```

**Expected outcome:** `Outcome::Compatible` when runtimes match; otherwise `Unknown`/`Incompatible`. The graph's dependencies (`g.dependencies`) drive `dependencies_of(gId)`, and `propagate_invalidation` would cascade a kernel invalidation to dependents.

## 8. Protocol compatibility

**File:** `examples/protocol.cpp`

A `ProtocolProfile` exposes `family`, `major`/`minor`, `message_schema_generation`, `required_semantics`, and `optional_semantics`. Two peers are compatible when the same family is used and the major version matches, with minor version checks (`version_ge`) and required-semantics membership.

```cpp
ProtocolProfile p1; p1.family = "inference"; p1.major = 2; p1.minor = 3;
p1.required_semantics = {"kv", "logits"};
ProtocolProfile p2; p2.family = "inference"; p2.major = 2; p2.minor = 1;
p2.required_semantics = {"kv"};

CompatibilityRule r; r.outcome = Outcome::Compatible;
Constraint fam; fam.op = PredOp::Eq; fam.field = "family"; fam.right_field = "family"; r.required.push_back(fam);
Constraint maj; maj.op = PredOp::Eq; maj.field = "major"; maj.right_field = "major"; r.required.push_back(maj);
Constraint min; min.op = PredOp::VersionGe; min.field = "minor"; min.right_field = "minor"; r.required.push_back(min);
reg.register_rule(r);

show(reg, reg.evaluate_pair(p1id, p2id)); // -> COMPATIBLE
```

**Expected outcome:** `Outcome::Compatible` for same-family, same-major peers; `Incompatible` (or `Unknown`) when families or major versions differ. Feature/required-semantics checks can be added as `FeatureReq` or set-membership constraints.

## 9. Unknown due to missing evidence

**File:** `examples/unknown_missing_evidence.cpp`

A rule requires a field (`PredOp::FeatureReq` on `graph_support`) that is **absent** from the profile. Because the field is absent, it is *unknown/unsupported-not-observed* — never treated as `false` and never as implicit compatibility. The decision is `InsufficientEvidence` and the field is listed in `missing_evidence`.

```cpp
CompatibilityRule r; r.outcome = Outcome::Compatible;
Constraint f; f.op = PredOp::FeatureReq; f.field = "graph_support";
r.required.push_back(f);
reg.register_rule(r);

// device profile has no 'graph_support' capability recorded
CompatibilityDecision d = reg.evaluate_pair(kId.value, dId.value);
show(reg, d); // -> INSUFFICIENT_EVIDENCE, missing_evidence: graph_support
```

**Expected outcome:** `Outcome::InsufficientEvidence`, not `Unknown` and not `Incompatible`. `d.missing_evidence` contains `graph_support`. If instead there is *no applicable rule at all*, the outcome is `Outcome::Unknown` — the registry never fabricates compatibility from missing data.

## 10. Conditional compatibility with adaptation

**File:** `examples/conditional_adaptation.cpp`

A `RuleKind::Conditional` rule sets the outcome to `Conditional` when its `conditions` are not met, and a `RuleKind::Adaptation`-based rule attaches a list of `adaptations` and yields `CompatibleWithAdaptation`.

```cpp
// conditional rule
CompatibilityRule cond; cond.kind = RuleKind::Conditional; cond.outcome = Outcome::Compatible;
Constraint c1; c1.op = PredOp::Eq; c1.field = "dtype"; c1.right_field = "dtype"; cond.required.push_back(c1);
Constraint c2; c2.op = PredOp::Eq; c2.field = "layout"; c2.right_field = "layout"; cond.conditions.push_back(c2);
reg.register_rule(cond);

// adaptation rule
CompatibilityRule ad; ad.kind = RuleKind::Adaptation;
ad.adaptations = {"cast_f16_to_f32", "transpose_to_row_major"};
reg.register_rule(ad);

CompatibilityDecision d = reg.evaluate_pair(a.value, b.value);
show(reg, d); // -> CONDITIONAL, or COMPATIBLE_WITH_ADAPTATION with the adaptation list
```

**Expected outcome:** When the conditions hold, `Outcome::Compatible`; when they do not, `Outcome::Conditional` with the condition surfaced. An adaptation rule yields `Outcome::CompatibleWithAdaptation` and lists the exact adaptations in `d.adaptations`.

## 11. Rule supersession

**File:** `examples/rule_supersession.cpp`

Register a rule, then `supersede_rule` with a changed version. Only the active, newest generation governs fresh evaluations; the old generation is marked superseded and stays in history.

```cpp
CompatibilityRuleId rid = reg.register_rule(r1);          // generation 1, outcome Compatible
CompatibilityDecision d1 = reg.evaluate_pair(a.value, b.value);

r1.outcome = Outcome::Incompatible;                        // new semantics
reg.supersede_rule(rid, r1);                               // generation 2
CompatibilityDecision d2 = reg.evaluate_pair(a.value, b.value);

// d1 was produced by generation 1; d2 by generation 2; registry_generation advanced
```

**Expected outcome:** `d1` (rule generation 1) and `d2` (rule generation 2) reflect **different** outcomes, and because the registry generation is part of the decision identity, the two decisions have distinct `decision_id`s. `active_rules()` returns only the newest active rule.

## 12. Dependency invalidation

**File:** `examples/dependency_invalidation.cpp`

Register a graph that depends on a kernel. Invalidate the kernel; `propagate_invalidation` marks the kernel and its dependents superseded, recording an `Invalidation` with a reason.

```cpp
ProfileId gId = reg.register_profile(ProfileKind::Graph, g.to_canon(), {kId.value}, "graph depends on kernel");
CHECK(reg.dependencies_of(gId.value)[0] == kId.value);

reg.invalidate_profile(kId.value, "kernel rebuilt for new arch");
reg.propagate_invalidation(kId.value);   // cascades to dependents

// both the kernel and the graph are now superseded; each has a recorded Invalidation
```

**Expected outcome:** `dependencies_of(gId)` contains `kId`; after invalidation, `find_profile(kId)` and `find_profile(gId)` return `nullptr` for the newest active record, `history_of` still shows the full history, and the decision that previously governed the pair is no longer produced by a fresh evaluation (the profiles are inactive).

## 13. Historical replay

**File:** `examples/historical_replay.cpp`

Evaluate a pair, snapshot, mutate the registry, then `recover` from the snapshot and replay the historical decision — it must reproduce exactly (same `decision_id` and `digest`).

```cpp
CompatibilityDecision before = reg.evaluate_pair(a.value, b.value);
std::vector<std::uint8_t> snap = reg.snapshot();

reg.register_profile(ProfileKind::Kernel, changed.to_canon());   // mutate

auto reg2 = CompatibilityRegistry::recover(snap);                 // restore
CompatibilityDecision replay = reg2->replay_decision(before.decision_id);

bool identical = replay && replay->digest == before.digest;       // true
```

**Expected outcome:** `replay_decision` returns the historical decision whose `digest` equals the original; the restored registry reproduces `evaluate_pair` outcomes that match the captured state, and `recover` rejects a snapshot with a corrupted checksum or a dependency cycle.

## 14. Real CUDA compatibility

**File:** `examples/cuda_compatibility.cpp`

On a machine actually holding a device (e.g. an RTX 5090 with compute capability `12.0` / architecture `sm_120`), discovery reads **real** device properties into a `DeviceProfile`. The example then proves:

- a kernel built for `sm_120` on that device is `Compatible` (or `Exact` when the canonical fingerprints match);
- a kernel requiring a higher compute capability than the device is `Incompatible` **before** any execution;
- a `dtype`/`layout` mismatch surfaces as a failed constraint;
- a stale artifact (`generation` lower than the current one) no longer governs a fresh evaluation;
- a runtime/toolchain change produces a new artifact generation, so an earlier pairing no longer holds.

```cpp
DeviceProfile dev;                       // populated from this machine's real device
// dev.compute_capability == "12.0", dev.architecture == "sm_120" (real, never fabricated)
ProfileId devId = reg.register_profile(ProfileKind::Device, dev.to_canon());

show(reg, reg.evaluate_pair(sm120Kernel.value, devId.value)); // -> COMPATIBLE
```

**Expected outcome:** `Outcome::Compatible` for a matching `sm_120` kernel; `Outcome::Incompatible` for a higher-capability kernel. When no CUDA toolkit is present, `COMPAT_HAVE_CUDA` is 0 and discovery returns an unknown/absent property rather than inventing hardware.

## 15. Source restart / stale-authority rejection

**File:** `examples/stale_authority.cpp`

A registry receives evidence from a source identified by `EvidenceRecord.source` and `source_generation`. After a restart, the registry has already advanced past a source's generation, so a mutation arriving at an older generation is **rejected as stale** — it cannot overwrite current authoritative state.

```cpp
EvidenceRecord e1; e1.field = "graph_support"; e1.value = Canon::mk_bool(true);
e1.source = "vertex-runtime"; e1.source_generation = Generation{5}; e1.kind = EvidenceKind::Measured;
reg.register_evidence(e1);

// after restart the registry is at generation 7 for that source
EvidenceRecord e1_stale = e1; e1_stale.source_generation = Generation{3};  // stale
bool accepted = false;
try { reg.register_evidence(e1_stale); accepted = true; } catch (...) {}
// accepted == false for a stale generation
```

**Expected outcome:** The registry tracks generation per source; a mutation below the current source generation is treated as stale authority and rejected (alternatively, integrated only as a lower-priority generation that never overrides the current one), keeping a restarted registry from being overwritten by an out-of-date producer. It is consistent with the rule that **no network I/O happens under the registry lock** — the producer/consumer exchange happens at the integration boundary.

Each walkthrough links to an intended `examples/*.cpp`. The set is built by `COMPAT_BUILD_EXAMPLES` and compiled with `/W4 /WX` on MSVC, matching the library's build discipline.
