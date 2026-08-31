#include "compat/rule.hpp"
#include "compat/capability.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>

namespace compat {

static bool nameeq(std::string_view a, std::string_view b) noexcept { return a == b; }

std::optional<std::uint32_t> tag_for(ProfileKind k, std::string_view name) {
    switch (k) {
        case ProfileKind::Model: {
            if (nameeq(name,"model_id")) return modelf::ModelId;
            if (nameeq(name,"revision_id")) return modelf::RevisionId;
            if (nameeq(name,"architecture")) return modelf::Architecture;
            if (nameeq(name,"family")) return modelf::Family;
            if (nameeq(name,"quantization")) return modelf::Quantization;
            if (nameeq(name,"tokenizer_id")) return modelf::TokenizerId;
            if (nameeq(name,"vocabulary_id")) return modelf::VocabularyId;
            if (nameeq(name,"parameter_count")) return modelf::ParameterCount;
            if (nameeq(name,"dtype")) return modelf::Dtype;
            if (nameeq(name,"execution_req")) return modelf::ExecutionReq;
            return std::nullopt;
        }
        case ProfileKind::Tokenizer: {
            if (nameeq(name,"tokenizer_id")) return tokf::TokenizerId;
            if (nameeq(name,"vocabulary_id")) return tokf::VocabularyId;
            if (nameeq(name,"vocab_size")) return tokf::VocabSize;
            if (nameeq(name,"normalization")) return tokf::Normalization;
            if (nameeq(name,"chat_template")) return tokf::ChatTemplate;
            return std::nullopt;
        }
        case ProfileKind::Tensor: {
            if (nameeq(name,"schema_id")) return tns::SchemaId;
            if (nameeq(name,"rank")) return tns::Rank;
            if (nameeq(name,"dtype")) return tns::Dtype;
            if (nameeq(name,"layout")) return tns::Layout;
            if (nameeq(name,"alignment")) return tns::Alignment;
            if (nameeq(name,"endianness")) return tns::Endianness;
            if (nameeq(name,"semantic_role")) return tns::SemanticRole;
            return std::nullopt;
        }
        case ProfileKind::Kv: {
            if (nameeq(name,"kv_format_id")) return kvf::KVFormatId;
            if (nameeq(name,"model_revision")) return kvf::ModelRevision;
            if (nameeq(name,"layer_count")) return kvf::LayerCount;
            if (nameeq(name,"head_count")) return kvf::HeadCount;
            if (nameeq(name,"head_dim")) return kvf::HeadDim;
            if (nameeq(name,"dtype")) return kvf::Dtype;
            if (nameeq(name,"layout")) return kvf::Layout;
            if (nameeq(name,"positional")) return kvf::Positional;
            if (nameeq(name,"seq_context")) return kvf::SeqContext;
            return std::nullopt;
        }
        case ProfileKind::Kernel: {
            if (nameeq(name,"kernel_artifact_id")) return kernf::KernelArtifactId;
            if (nameeq(name,"operation")) return kernf::Operation;
            if (nameeq(name,"architecture")) return kernf::Architecture;
            if (nameeq(name,"compute_capability")) return kernf::ComputeCapability;
            if (nameeq(name,"abi")) return kernf::Abi;
            if (nameeq(name,"runtime")) return kernf::Runtime;
            if (nameeq(name,"compiler")) return kernf::Compiler;
            if (nameeq(name,"compiler_version")) return kernf::CompilerVersion;
            if (nameeq(name,"dtype")) return kernf::Dtype;
            if (nameeq(name,"layout")) return kernf::Layout;
            if (nameeq(name,"shape_spec")) return kernf::ShapeSpec;
            if (nameeq(name,"quantization")) return kernf::Quantization;
            if (nameeq(name,"interface")) return kernf::Interface;
            if (nameeq(name,"generation")) return kernf::Generation;
            return std::nullopt;
        }
        case ProfileKind::Graph: {
            if (nameeq(name,"graph_artifact_id")) return graphf::GraphArtifactId;
            if (nameeq(name,"topology")) return graphf::Topology;
            if (nameeq(name,"runtime")) return graphf::Runtime;
            if (nameeq(name,"backend")) return graphf::Backend;
            if (nameeq(name,"architecture")) return graphf::Architecture;
            if (nameeq(name,"abi")) return graphf::Abi;
            if (nameeq(name,"dtype")) return graphf::Dtype;
            if (nameeq(name,"layout")) return graphf::Layout;
            if (nameeq(name,"memory_binding")) return graphf::MemoryBinding;
            return std::nullopt;
        }
        case ProfileKind::Adapter: {
            if (nameeq(name,"adapter_id")) return adapf::AdapterId;
            if (nameeq(name,"base_model_revision")) return adapf::BaseModelRevision;
            if (nameeq(name,"kind")) return adapf::Kind;
            if (nameeq(name,"dtype")) return adapf::Dtype;
            if (nameeq(name,"composition")) return adapf::Composition;
            if (nameeq(name,"generation")) return adapf::Generation;
            return std::nullopt;
        }
        case ProfileKind::Backend: {
            if (nameeq(name,"backend_id")) return backf::BackendId;
            if (nameeq(name,"runtime")) return backf::Runtime;
            if (nameeq(name,"compiler")) return backf::Compiler;
            if (nameeq(name,"compiler_version")) return backf::CompilerVersion;
            if (nameeq(name,"abi")) return backf::Abi;
            if (nameeq(name,"codegen_target")) return backf::CodegenTarget;
            if (nameeq(name,"protocol_version")) return backf::ProtocolVersion;
            if (nameeq(name,"interface_version")) return backf::InterfaceVersion;
            return std::nullopt;
        }
        case ProfileKind::Device: {
            if (nameeq(name,"device_id")) return devf::DeviceId;
            if (nameeq(name,"vendor")) return devf::Vendor;
            if (nameeq(name,"architecture")) return devf::Architecture;
            if (nameeq(name,"compute_capability")) return devf::ComputeCapability;
            if (nameeq(name,"memory_model")) return devf::MemoryModel;
            if (nameeq(name,"total_memory")) return devf::TotalMemory;
            if (nameeq(name,"runtime_min")) return devf::RuntimeMin;
            if (nameeq(name,"driver_min")) return devf::DriverMin;
            return std::nullopt;
        }
        case ProfileKind::Protocol: {
            if (nameeq(name,"protocol_id")) return protf::ProtocolId;
            if (nameeq(name,"family")) return protf::Family;
            if (nameeq(name,"major")) return protf::Major;
            if (nameeq(name,"minor")) return protf::Minor;
            if (nameeq(name,"message_schema_generation")) return protf::MessageSchemaGeneration;
            return std::nullopt;
        }
        case ProfileKind::Policy: {
            if (nameeq(name,"policy_id")) return polf::PolicyId;
            if (nameeq(name,"generation")) return polf::Generation;
            if (nameeq(name,"strictness")) return polf::Strictness;
            if (nameeq(name,"fallback")) return polf::Fallback;
            return std::nullopt;
        }
        case ProfileKind::CapabilitySet: return std::nullopt;
    }
    return std::nullopt;
}

std::optional<Canon> resolve_capability_subfield(const Canon& rec, std::string_view name);

std::optional<Canon> resolve_profile_field(ProfileKind kind, const Canon& profile, std::string_view dotted_name) {
    if (dotted_name.empty()) return std::nullopt;
    std::size_t dot = dotted_name.find('.');
    std::string_view head = dotted_name.substr(0, dot);
    std::string_view tail = dot == std::string_view::npos ? std::string_view() : dotted_name.substr(dot + 1);
    auto tag = tag_for(kind, head);
    if (!tag) return std::nullopt;
    const Canon* v = profile.field(*tag);
    if (!v) return std::nullopt;
    if (tail.empty()) return *v;
    if (v->kind() != CanonKind::Record) return std::nullopt;
    return resolve_capability_subfield(*v, tail);
}

std::optional<Canon> resolve_capability_subfield(const Canon& rec, std::string_view name) {
    struct Entry { const char* n; std::uint32_t tag; };
    static constexpr Entry entries[] = {
        { "compute_capability", 1 }, { "max_shared_memory", 2 },
        { "graph_support", 3 }, { "supported_dtype_classes", 4 },
        { "kernel_target_arch", 5 }, { "protocol_features", 6 },
        { "memory_feature_support", 7 }, { "max_threads_per_block", 8 },
        { "max_blocks_per_sm", 9 }, { "regs_per_thread", 10 }
    };
    for (const auto& e : entries) {
        if (nameeq(name, e.n)) {
            const Canon* v = rec.field(e.tag);
            if (v) return *v;
            return std::nullopt;
        }
    }
    return std::nullopt;
}

struct Ver { std::uint64_t major = 0; std::uint64_t minor = 0; };

static bool parse_num(std::string_view s, std::uint64_t& out) {
    if (s.empty()) return false;
    const char* b = s.data();
    const char* e = b + s.size();
    auto res = std::from_chars(b, e, out);
    return res.ec == std::errc() && res.ptr == e;
}

static Ver parse_ver(std::string_view s) {
    Ver v;
    if (s.empty()) return v;
    std::string t;
    for (char c : s) { if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') t.push_back(c); }
    std::size_t dot = t.find('.');
    std::uint64_t mj = 0, mn = 0;
    if (dot == std::string::npos) { parse_num(t, mj); }
    else {
        std::string a = t.substr(0, dot);
        std::string b = t.substr(dot + 1);
        parse_num(a, mj); parse_num(b, mn);
    }
    v.major = mj; v.minor = mn;
    return v;
}

static int cmp_ver(const Canon& a, const Canon& b) {
    Ver av = a.kind()==CanonKind::Uint ? Ver{a.as_uint(), 0} : parse_ver(a.as_string());
    Ver bv = b.kind()==CanonKind::Uint ? Ver{b.as_uint(), 0} : parse_ver(b.as_string());
    if (av.major != bv.major) return av.major < bv.major ? -1 : 1;
    if (av.minor != bv.minor) return av.minor < bv.minor ? -1 : 1;
    return 0;
}

static bool canon_equal(const Canon& a, const Canon& b) {
    if (a.kind() != b.kind()) return false;
    switch (a.kind()) {
        case CanonKind::Null: return true;
        case CanonKind::Uint: return a.as_uint() == b.as_uint();
        case CanonKind::Int: return a.as_int() == b.as_int();
        case CanonKind::Bool: return a.as_bool() == b.as_bool();
        case CanonKind::Str: return a.as_string() == b.as_string();
        case CanonKind::Bytes: return a.as_string() == b.as_string();
        case CanonKind::Uuid: return a.as_uuid() == b.as_uuid();
        case CanonKind::Float: return a.as_float() == b.as_float();
        case CanonKind::Record: return canonical_fingerprint(a) == canonical_fingerprint(b);
        case CanonKind::Sequence: return canonical_fingerprint(a) == canonical_fingerprint(b);
    }
    return false;
}

bool Constraint::evaluate(const Canon& left, const Canon& right, ProfileKind lk, ProfileKind rk,
                          std::string& why_ok, std::string& why_not) const {
    std::optional<Canon> lv = resolve_profile_field(lk, left, field);
    std::optional<Canon> rv;
    if (!right_field.empty()) rv = resolve_profile_field(rk, right, right_field);

    switch (op) {
        case PredOp::Eq: {
            if (!lv) { why_not = "field '" + field + "' unknown on left"; return false; }
            if (!right_field.empty()) {
                if (!rv) { why_not = "field '" + right_field + "' unknown on right"; return false; }
                if (canon_equal(*lv,*rv)) { why_ok = "field values equal"; return true; }
                why_not = "field values differ"; return false;
            }
            if (canon_equal(*lv, expected)) { why_ok = "field equals expected"; return true; }
            why_not = "field differs from expected"; return false;
        }
        case PredOp::Ne: {
            if (!lv) { why_not = "field '" + field + "' unknown on left"; return false; }
            if (!right_field.empty()) {
                if (!rv) { why_not = "field '" + right_field + "' unknown on right"; return false; }
                if (canon_equal(*lv,*rv)) { why_not = "field values equal"; return false; }
                why_ok = "field values differ"; return true;
            }
            if (canon_equal(*lv, expected)) { why_not = "field equals expected"; return false; }
            why_ok = "field differs from expected"; return true;
        }
        case PredOp::InSet: {
            if (!lv) { why_not = "field '" + field + "' unknown on left"; return false; }
            for (const Canon& s : set) { if (canon_equal(*lv, s)) { why_ok = "value in allowed set"; return true; } }
            why_not = "value not in allowed set"; return false;
        }
        case PredOp::Min: {
            if (!lv) { why_not = "field '" + field + "' unknown on left"; return false; }
            auto a = lv->kind()==CanonKind::Float ? lv->as_float() : static_cast<double>(lv->kind()==CanonKind::Uint ? lv->as_uint() : lv->as_int());
            auto b = expected.kind()==CanonKind::Float ? expected.as_float() : static_cast<double>(expected.kind()==CanonKind::Uint ? expected.as_uint() : expected.as_int());
            if (a >= b) { why_ok = "meets minimum"; return true; }
            why_not = "below minimum"; return false;
        }
        case PredOp::Max: {
            if (!lv) { why_not = "field '" + field + "' unknown on left"; return false; }
            auto a = lv->kind()==CanonKind::Float ? lv->as_float() : static_cast<double>(lv->kind()==CanonKind::Uint ? lv->as_uint() : lv->as_int());
            auto b = expected.kind()==CanonKind::Float ? expected.as_float() : static_cast<double>(expected.kind()==CanonKind::Uint ? expected.as_uint() : expected.as_int());
            if (a <= b) { why_ok = "within maximum"; return true; }
            why_not = "exceeds maximum"; return false;
        }
        case PredOp::VersionLt: case PredOp::VersionLe: case PredOp::VersionGt: case PredOp::VersionGe: {
            if (!lv) { why_not = "field '" + field + "' unknown on left"; return false; }
            if (!right_field.empty() && !rv) { why_not = "field '" + right_field + "' unknown on right"; return false; }
            const Canon& rhs = right_field.empty() ? expected : *rv;
            int c = cmp_ver(*lv, rhs);
            bool ok = false;
            if (op==PredOp::VersionLt) ok = c < 0;
            else if (op==PredOp::VersionLe) ok = c <= 0;
            else if (op==PredOp::VersionGt) ok = c > 0;
            else if (op==PredOp::VersionGe) ok = c >= 0;
            if (ok) why_ok = "version relation holds"; else why_not = "version relation fails";
            return ok;
        }
        case PredOp::ArchFamily: {
            if (!lv) { why_not = "field '" + field + "' unknown on left"; return false; }
            if (lv->kind()!=CanonKind::Str) { why_not = "architecture not a string"; return false; }
            std::string val = lv->as_string();
            for (const Canon& s : set) {
                if (s.kind()==CanonKind::Str) {
                    const std::string& fam = s.as_string();
                    if (val == fam || (val.size()>fam.size() && val.compare(0, fam.size(), fam)==0)) { why_ok = "architecture in family"; return true; }
                }
            }
            why_not = "architecture not in family"; return false;
        }
        case PredOp::FeatureReq: case PredOp::Truthy: {
            if (!lv) { why_not = "feature '" + field + "' unknown/not observed"; return false; }
            bool v = lv->kind()==CanonKind::Bool ? lv->as_bool() : (lv->kind()==CanonKind::Uint ? lv->as_uint()!=0 : false);
            if (!v) { why_not = "feature absent/unsupported"; return false; }
            why_ok = "feature present"; return true;
        }
    }
    why_not = "unknown predicate"; return false;
}

// ---------------------------------------------------------------------------
// Profile payload edit (counterfactual support)
// ---------------------------------------------------------------------------
Canon apply_profile_field_edit(ProfileKind kind, const Canon& payload, const std::string& dotted_field, const Canon& value) {
    std::size_t dot = dotted_field.find('.');
    std::string head = dotted_field.substr(0, dot);
    auto tag = tag_for(kind, head);
    if (!tag) throw std::runtime_error("apply_profile_field_edit: unknown field '" + dotted_field + "'");
    if (payload.kind() != CanonKind::Record) throw std::runtime_error("apply_profile_field_edit: payload not a record");
    Canon::Record rec = payload.as_record();
    bool found = false;
    for (auto& ent : rec) { if (ent.first == *tag) { ent.second = value; found = true; } }
    if (!found) rec.emplace_back(*tag, value);
    return Canon::mk_record(std::move(rec));
}

} // namespace compat
