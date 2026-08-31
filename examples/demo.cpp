#include "compat/compat.hpp"
#include <cstdio>
using namespace compat;

// Runnable examples for the Compatibility Registry. Each section exercises one
// documented case and prints the outcome.
int main() {
    CompatibilityRegistry reg;

    std::printf("== exact tensor compatibility ==\n");
    TensorProfile t1; t1.schema_id=TensorSchemaId::genseed(); t1.rank=2; t1.shape={2,3};
    t1.dtype="f32"; t1.layout="row-major"; t1.semantic_role="activations";
    ProfileId tid = reg.register_profile(ProfileKind::Tensor, t1.to_canon());
    // registration guarantees a stable content fingerprint
    auto d = reg.evaluate_pair(tid, tid);
    std::printf("  self-pair outcome: %s (expect EXACT)\n", outcome_name(d.outcome));

    std::printf("== tensor mismatch ==\n");
    TensorProfile t2 = t1; t2.schema_id=TensorSchemaId::genseed(); t2.dtype="f16";
    ProfileId tid2 = reg.register_profile(ProfileKind::Tensor, t2.to_canon());
    // register a rule that requires an exact dtype match
    CompatibilityRule r; r.rule_id=CompatibilityRuleId::genseed(); r.scope="pair";
    r.left_kind=ProfileKind::Tensor; r.outcome=Outcome::Compatible; r.priority=10;
    Constraint c; c.op=PredOp::Eq; c.field="dtype"; c.right_field="dtype"; r.required.push_back(c);
    reg.register_rule(r);
    d = reg.evaluate_pair(tid, tid2);
    std::printf("  f32 vs f16 outcome: %s (expect UNKNOWN after required dtype equality fails)\n", outcome_name(d.outcome));

    std::printf("== model/tokenizer compatibility ==\n");
    ModelProfile m; m.model_id=ModelId::genseed(); m.revision_id=ModelRevisionId::genseed();
    m.architecture="transformer"; m.family="llm"; m.quantization="fp16";
    m.tokenizer_id=TokenizerId::from_string("00000000-0000-0000-0000-00000000000a");
    m.vocabulary_id=VocabularyId::from_string("00000000-0000-0000-0000-00000000000b"); m.dtype="f16";
    ProfileId mid = reg.register_profile(ProfileKind::Model, m.to_canon());
    TokenizerProfile tok; tok.tokenizer_id = m.tokenizer_id; tok.vocabulary_id=m.vocabulary_id; tok.vocab_size=128000;
    tok.normalization="none"; tok.chat_template="chatml";
    ProfileId tokid = reg.register_profile(ProfileKind::Tokenizer, tok.to_canon());
    CompatibilityRule mt; mt.rule_id=CompatibilityRuleId::genseed(); mt.scope="pair";
    mt.left_kind=ProfileKind::Model; mt.right_kind=ProfileKind::Tokenizer; mt.outcome=Outcome::Compatible;
    Constraint mtc; mtc.op=PredOp::Eq; mtc.field="tokenizer_id"; mtc.right_field="tokenizer_id"; mt.required.push_back(mtc);
    reg.register_rule(mt);
    d = reg.evaluate_pair(mid, tokid);
    std::printf("  model/tokenizer outcome: %s (expect COMPATIBLE)\n", outcome_name(d.outcome));

    std::printf("== adapter/base-model compatibility ==\n");
    AdapterProfile ad; ad.adapter_id=AdapterId::genseed(); ad.base_model_revision="rev-2";
    ad.kind="lora"; ad.dtype="f16"; ad.composition="additive"; ad.rank_config=Canon::rec();
    ProfileId adid = reg.register_profile(ProfileKind::Adapter, ad.to_canon());
    CompatibilityRule ar; ar.rule_id=CompatibilityRuleId::genseed(); ar.scope="pair";
    ar.left_kind=ProfileKind::Adapter; ar.outcome=Outcome::Compatible;
    Constraint arc; arc.op=PredOp::Eq; arc.field="base_model_revision"; arc.expected=Canon::mk_str("rev-2"); ar.required.push_back(arc);
    reg.register_rule(ar);
    std::printf("  adapter registered with base_model_revision=rev-2 (rule requires rev-2)\n");

    std::printf("== unknown due to missing evidence ==\n");
    CompatibilityRule kr; kr.rule_id=CompatibilityRuleId::genseed(); kr.scope="pair"; kr.outcome=Outcome::Compatible;
    Constraint kc; kc.op=PredOp::FeatureReq; kc.field="missing_feature"; kr.required.push_back(kc);
    reg.register_rule(kr);
    d = reg.evaluate_pair(mid, mid);  // rule demands a feature that is not present
    std::printf("  outcome: %s (missing evidence -> UNKNOWN/INSUFFICIENT_EVIDENCE, never compatible)\n", outcome_name(d.outcome));

    std::printf("== conditional compatibility with adaptation ==\n");
    CompatibilityRule cond; cond.rule_id=CompatibilityRuleId::genseed(); cond.scope="pair"; cond.kind=RuleKind::Conditional;
    cond.outcome=Outcome::Compatible; cond.adaptations={"cast f16->f32"}; cond.left_kind=ProfileKind::Tensor;
    Constraint cc; cc.op=PredOp::Eq; cc.field="dtype"; cc.expected=Canon::mk_str("f32"); cond.conditions.push_back(cc);
    reg.register_rule(cond);
    d = reg.evaluate_pair(tid2, tid); // tensor f16 vs f32; condition requires f32 on the left
    std::printf("  outcome: %s (expect CONDITIONAL with adaptation 'cast f16->f32')\n", outcome_name(d.outcome));
    if (!d.adaptations.empty()) std::printf("    adaptation: %s\n", d.adaptations[0].c_str());

    std::printf("== rule supersession ==\n");
    CompatibilityRule oldRule; oldRule.rule_id=CompatibilityRuleId::genseed(); oldRule.scope="pair"; oldRule.outcome=Outcome::Compatible; oldRule.priority=10;
    Constraint o; o.op=PredOp::Eq; o.field="dtype"; o.right_field="dtype"; oldRule.required.push_back(o);
    CompatibilityRuleId oid = reg.register_rule(oldRule);
    CompatibilityRule newRule = oldRule; newRule.outcome=Outcome::CompatibleWithAdaptation; newRule.adaptations={"transpose"};
    reg.supersede_rule(oid, newRule);
    d = reg.evaluate_pair(tid, tid2);
    std::printf("  superseded rule now yields: %s (rule generation advanced)\n", outcome_name(d.outcome));

    std::printf("== dependency invalidation ==\n");
    ProfileId parent = reg.register_profile(ProfileKind::Model, m.to_canon(), {}, "");
    ProfileId child = reg.register_profile(ProfileKind::Adapter, ad.to_canon(), {parent.value}, "");
    std::printf("  child dependencies: %zu\n", reg.dependencies_of(child.value).size());
    reg.propagate_invalidation(parent.value);
    std::printf("  after invalidating parent, child active: %s\n", reg.find_profile(child.value) ? "yes" : "no");
    std::printf("  dependency cycle detected? %s\n", reg.has_cycle() ? "yes" : "no");

    std::printf("== historical replay (persistence) ==\n");
    auto before = reg.evaluate_pair(tid, tid);
    std::vector<std::uint8_t> snap = reg.snapshot();
    auto reg2 = CompatibilityRegistry::recover(snap);
    auto after = reg2->evaluate_pair(tid, tid);
    std::printf("  replay digest preserved: %s\n", before.digest == after.digest ? "YES" : "NO");

    std::printf("\n[Runnable example completed]\n");
    return 0;
}
