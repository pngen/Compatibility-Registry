#pragma once
#include "compat/canonical.hpp"
#include "compat/id.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include <stdexcept>

namespace compat {

enum class ProfileKind : std::uint8_t {
    Model, Tokenizer, Tensor, Kv, Kernel, Graph, Adapter, Backend, Device, Protocol, Policy, CapabilitySet
};

inline const char* profile_kind_name(ProfileKind k) noexcept {
    switch (k) {
        case ProfileKind::Model: return "model";
        case ProfileKind::Tokenizer: return "tokenizer";
        case ProfileKind::Tensor: return "tensor";
        case ProfileKind::Kv: return "kv";
        case ProfileKind::Kernel: return "kernel";
        case ProfileKind::Graph: return "graph";
        case ProfileKind::Adapter: return "adapter";
        case ProfileKind::Backend: return "backend";
        case ProfileKind::Device: return "device";
        case ProfileKind::Protocol: return "protocol";
        case ProfileKind::Policy: return "policy";
        case ProfileKind::CapabilitySet: return "capability_set";
    }
    return "?";
}

// Deterministic per-domain field tags. Tags are unique within a single profile
// record but may be reused across domains (each profile is its own record).
namespace modelf { enum : std::uint32_t {
    ModelId=1, RevisionId=2, Architecture=3, Family=4, Quantization=5,
    TokenizerId=6, VocabularyId=7, ParameterCount=8, AdapterIds=9, Dtype=10,
    ExecutionReq=11 };
}
namespace tokf { enum : std::uint32_t {
    TokenizerId=1, VocabularyId=2, VocabSize=3, SpecialTokens=4,
    Normalization=5, ChatTemplate=6 };
}
namespace tns { enum : std::uint32_t {
    SchemaId=1, Rank=2, Shape=3, DynamicDims=4, Dtype=5, Layout=6,
    Strides=7, Alignment=8, Endianness=9, SemanticRole=10 };
}
namespace kvf { enum : std::uint32_t {
    KVFormatId=1, ModelRevision=2, LayerCount=3, HeadCount=4, HeadDim=5,
    Dtype=6, Layout=7, BlockGeometry=8, Positional=9, SeqContext=10,
    TokenizerId=11, Generation=12 };
}
namespace kernf { enum : std::uint32_t {
    KernelArtifactId=1, Operation=2, Architecture=3, ComputeCapability=4,
    Abi=5, Runtime=6, Compiler=7, CompilerVersion=8, Dtype=9, Layout=10,
    ShapeSpec=11, LaunchGeometry=12, Quantization=13, Interface=14,
    Generation=15, ArtifactDigest=16 };
}
namespace graphf { enum : std::uint32_t {
    GraphArtifactId=1, Topology=2, Dependencies=3, Runtime=4, Backend=5,
    Architecture=6, Abi=7, Dtype=8, Layout=9, ShapeSpec=10, LaunchConfig=11,
    MemoryBinding=12, Generation=13 };
}
namespace adapf { enum : std::uint32_t {
    AdapterId=1, BaseModelRevision=2, Kind=3, RankConfig=4, TargetModules=5,
    Dtype=6, Composition=7, Activation=8, Generation=9, ArtifactDigest=10 };
}
namespace backf { enum : std::uint32_t {
    BackendId=1, Runtime=2, Compiler=3, CompilerVersion=4, Abi=5,
    CodegenTarget=6, Capabilities=7, ProtocolVersion=8, InterfaceVersion=9 };
}
namespace devf { enum : std::uint32_t {
    DeviceId=1, Vendor=2, Architecture=3, ComputeCapability=4, MemoryModel=5,
    TotalMemory=6, SupportedDtypes=7, InstructionClasses=8, ExecutionFeatures=9,
    RuntimeMin=10, DriverMin=11 };
}
namespace protf { enum : std::uint32_t {
    ProtocolId=1, Family=2, Major=3, Minor=4, MessageSchemaGeneration=5,
    FeatureBits=6, RequiredSemantics=7, OptionalSemantics=8 };
}
namespace polf { enum : std::uint32_t {
    PolicyId=1, Generation=2, Strictness=3, PermittedAdaptations=4, Fallback=5 };
}

// ---------------------------------------------------------------------------
// Model profile
// ---------------------------------------------------------------------------
struct ModelProfile {
    ModelId model_id;
    ModelRevisionId revision_id;
    std::string architecture;
    std::string family;
    std::string quantization;
    TokenizerId tokenizer_id;
    VocabularyId vocabulary_id;
    std::optional<std::uint64_t> parameter_count;
    std::vector<AdapterId> adapter_ids;
    std::string dtype;
    Canon::Record execution_req;   // nested record, e.g. backend/device/dtype

    Canon to_canon() const {
        Canon::Record r = Canon::rec();
        Canon::put_uuid(r, modelf::ModelId, model_id.value);
        Canon::put_uuid(r, modelf::RevisionId, revision_id.value);
        Canon::put_str(r, modelf::Architecture, architecture);
        Canon::put_str(r, modelf::Family, family);
        Canon::put_str(r, modelf::Quantization, quantization);
        Canon::put_uuid(r, modelf::TokenizerId, tokenizer_id.value);
        Canon::put_uuid(r, modelf::VocabularyId, vocabulary_id.value);
        if (parameter_count) Canon::put_uint(r, modelf::ParameterCount, *parameter_count);
        { Canon::Seq s; for (auto& a : adapter_ids) s.push_back(Canon::mk_uuid(a.value)); Canon::put_seq(r, modelf::AdapterIds, std::move(s)); }
        Canon::put_str(r, modelf::Dtype, dtype);
        Canon::put_record(r, modelf::ExecutionReq, execution_req);
        return Canon::mk_record(std::move(r));
    }
    static ModelProfile from_canon(const Canon& c) {
        ModelProfile p;
        const Canon* v;
        v = c.field(modelf::ModelId); if (!v || v->kind()!=CanonKind::Uuid) throw std::runtime_error("ModelProfile: missing model_id");
        p.model_id = ModelId(v->as_uuid());
        v = c.field(modelf::RevisionId); if (!v || v->kind()!=CanonKind::Uuid) throw std::runtime_error("ModelProfile: missing revision_id");
        p.revision_id = ModelRevisionId(v->as_uuid());
        p.architecture = str(c, modelf::Architecture);
        p.family = str(c, modelf::Family);
        p.quantization = str(c, modelf::Quantization);
        v = c.field(modelf::TokenizerId); if (!v || v->kind()!=CanonKind::Uuid) throw std::runtime_error("ModelProfile: missing tokenizer_id");
        p.tokenizer_id = TokenizerId(v->as_uuid());
        v = c.field(modelf::VocabularyId); if (!v || v->kind()!=CanonKind::Uuid) throw std::runtime_error("ModelProfile: missing vocabulary_id");
        p.vocabulary_id = VocabularyId(v->as_uuid());
        v = c.field(modelf::ParameterCount); if (v && v->kind()==CanonKind::Uint) p.parameter_count = v->as_uint();
        v = c.field(modelf::AdapterIds); if (v && v->kind()==CanonKind::Sequence) { for (auto& e : v->as_seq()) if (e.kind()==CanonKind::Uuid) p.adapter_ids.push_back(AdapterId(e.as_uuid())); }
        p.dtype = str(c, modelf::Dtype);
        v = c.field(modelf::ExecutionReq); if (v && v->kind()==CanonKind::Record) p.execution_req = v->as_record();
        return p;
    }
private:
    static std::string str(const Canon& c, std::uint32_t tag) {
        const Canon* v = c.field(tag); if (!v || v->kind()!=CanonKind::Str) throw std::runtime_error("ModelProfile: missing string field");
        return v->as_string();
    }
};

// --- tokenizer / vocabulary ---
struct TokenizerProfile {
    TokenizerId tokenizer_id;
    VocabularyId vocabulary_id;
    std::uint64_t vocab_size = 0;
    Canon::Record special_tokens;
    std::string normalization;
    std::string chat_template;
    Canon to_canon() const {
        Canon::Record r = Canon::rec();
        Canon::put_uuid(r, tokf::TokenizerId, tokenizer_id.value);
        Canon::put_uuid(r, tokf::VocabularyId, vocabulary_id.value);
        Canon::put_uint(r, tokf::VocabSize, vocab_size);
        Canon::put_record(r, tokf::SpecialTokens, special_tokens);
        Canon::put_str(r, tokf::Normalization, normalization);
        Canon::put_str(r, tokf::ChatTemplate, chat_template);
        return Canon::mk_record(std::move(r));
    }
    static TokenizerProfile from_canon(const Canon& c) {
        TokenizerProfile p;
        const Canon* v = c.field(tokf::TokenizerId); if (!v||v->kind()!=CanonKind::Uuid) throw std::runtime_error("TokenizerProfile: tokenizer_id");
        p.tokenizer_id = TokenizerId(v->as_uuid());
        v = c.field(tokf::VocabularyId); if (!v||v->kind()!=CanonKind::Uuid) throw std::runtime_error("TokenizerProfile: vocabulary_id");
        p.vocabulary_id = VocabularyId(v->as_uuid());
        v = c.field(tokf::VocabSize); if (v && v->kind()==CanonKind::Uint) p.vocab_size = v->as_uint();
        v = c.field(tokf::SpecialTokens); if (v && v->kind()==CanonKind::Record) p.special_tokens = v->as_record();
        v = c.field(tokf::Normalization); if (v && v->kind()==CanonKind::Str) p.normalization = v->as_string();
        v = c.field(tokf::ChatTemplate); if (v && v->kind()==CanonKind::Str) p.chat_template = v->as_string();
        return p;
    }
};

// --- tensor ---
struct TensorProfile {
    TensorSchemaId schema_id;
    std::uint64_t rank = 0;
    std::vector<std::int64_t> shape;
    std::vector<std::uint8_t> dynamic_dims;
    std::string dtype;
    std::string layout;
    std::vector<std::int64_t> strides;
    std::optional<std::uint64_t> alignment;
    std::string endianness;
    std::string semantic_role;
    Canon to_canon() const {
        Canon::Record r = Canon::rec();
        Canon::put_uuid(r, tns::SchemaId, schema_id.value);
        Canon::put_uint(r, tns::Rank, rank);
        { Canon::Seq s; for (auto v : shape) s.push_back(Canon::mk_int(v)); Canon::put_seq(r, tns::Shape, std::move(s)); }
        { Canon::Seq s; for (bool b : dynamic_dims) s.push_back(Canon::mk_bool(b)); Canon::put_seq(r, tns::DynamicDims, std::move(s)); }
        Canon::put_str(r, tns::Dtype, dtype);
        Canon::put_str(r, tns::Layout, layout);
        { Canon::Seq s; for (auto v : strides) s.push_back(Canon::mk_int(v)); Canon::put_seq(r, tns::Strides, std::move(s)); }
        if (alignment) Canon::put_uint(r, tns::Alignment, *alignment);
        Canon::put_str(r, tns::Endianness, endianness);
        Canon::put_str(r, tns::SemanticRole, semantic_role);
        return Canon::mk_record(std::move(r));
    }
    static TensorProfile from_canon(const Canon& c) {
        TensorProfile p;
        const Canon* v = c.field(tns::SchemaId); if (!v||v->kind()!=CanonKind::Uuid) throw std::runtime_error("TensorProfile: schema_id");
        p.schema_id = TensorSchemaId(v->as_uuid());
        v = c.field(tns::Rank); if (v && v->kind()==CanonKind::Uint) p.rank = v->as_uint();
        v = c.field(tns::Shape); if (v && v->kind()==CanonKind::Sequence) for (auto& e : v->as_seq()) if (e.kind()==CanonKind::Int) p.shape.push_back(e.as_int());
        v = c.field(tns::DynamicDims); if (v && v->kind()==CanonKind::Sequence) for (auto& e : v->as_seq()) if (e.kind()==CanonKind::Bool) p.dynamic_dims.push_back(e.as_bool()?1:0);
        v = c.field(tns::Dtype); if (v && v->kind()==CanonKind::Str) p.dtype = v->as_string();
        v = c.field(tns::Layout); if (v && v->kind()==CanonKind::Str) p.layout = v->as_string();
        v = c.field(tns::Strides); if (v && v->kind()==CanonKind::Sequence) for (auto& e : v->as_seq()) if (e.kind()==CanonKind::Int) p.strides.push_back(e.as_int());
        v = c.field(tns::Alignment); if (v && v->kind()==CanonKind::Uint) p.alignment = v->as_uint();
        v = c.field(tns::Endianness); if (v && v->kind()==CanonKind::Str) p.endianness = v->as_string();
        v = c.field(tns::SemanticRole); if (v && v->kind()==CanonKind::Str) p.semantic_role = v->as_string();
        return p;
    }
};

// --- KV ---
struct KvProfile {
    KVFormatId kv_format_id;
    std::string model_revision;
    std::uint64_t layer_count = 0;
    std::uint64_t head_count = 0;
    std::uint64_t head_dim = 0;
    std::string dtype;
    std::string layout;
    Canon::Record block_geometry;
    std::string positional;
    std::uint64_t seq_context = 0;
    std::string tokenizer_id;
    std::uint64_t generation = 0;
    Canon to_canon() const {
        Canon::Record r = Canon::rec();
        Canon::put_uuid(r, kvf::KVFormatId, kv_format_id.value);
        Canon::put_str(r, kvf::ModelRevision, model_revision);
        Canon::put_uint(r, kvf::LayerCount, layer_count);
        Canon::put_uint(r, kvf::HeadCount, head_count);
        Canon::put_uint(r, kvf::HeadDim, head_dim);
        Canon::put_str(r, kvf::Dtype, dtype);
        Canon::put_str(r, kvf::Layout, layout);
        Canon::put_record(r, kvf::BlockGeometry, block_geometry);
        Canon::put_str(r, kvf::Positional, positional);
        Canon::put_uint(r, kvf::SeqContext, seq_context);
        Canon::put_str(r, kvf::TokenizerId, tokenizer_id);
        Canon::put_uint(r, kvf::Generation, generation);
        return Canon::mk_record(std::move(r));
    }
    static KvProfile from_canon(const Canon& c) {
        KvProfile p;
        const Canon* v = c.field(kvf::KVFormatId); if (!v||v->kind()!=CanonKind::Uuid) throw std::runtime_error("KvProfile: kv_format_id");
        p.kv_format_id = KVFormatId(v->as_uuid());
        p.model_revision = simple_str(c, kvf::ModelRevision);
        v = c.field(kvf::LayerCount); if (v&&v->kind()==CanonKind::Uint) p.layer_count=v->as_uint();
        v = c.field(kvf::HeadCount); if (v&&v->kind()==CanonKind::Uint) p.head_count=v->as_uint();
        v = c.field(kvf::HeadDim); if (v&&v->kind()==CanonKind::Uint) p.head_dim=v->as_uint();
        p.dtype = simple_str(c, kvf::Dtype);
        p.layout = simple_str(c, kvf::Layout);
        v = c.field(kvf::BlockGeometry); if (v && v->kind()==CanonKind::Record) p.block_geometry = v->as_record();
        p.positional = simple_str(c, kvf::Positional);
        v = c.field(kvf::SeqContext); if (v&&v->kind()==CanonKind::Uint) p.seq_context=v->as_uint();
        p.tokenizer_id = simple_str(c, kvf::TokenizerId);
        v = c.field(kvf::Generation); if (v&&v->kind()==CanonKind::Uint) p.generation=v->as_uint();
        return p;
    }
private:
    static std::string simple_str(const Canon& c, std::uint32_t tag) {
        const Canon* v = c.field(tag); if (!v||v->kind()!=CanonKind::Str) throw std::runtime_error("KvProfile: missing string"); return v->as_string();
    }
};

// --- kernel ---
struct KernelProfile {
    KernelArtifactId kernel_artifact_id;
    std::string operation;
    std::string architecture;
    std::string compute_capability;
    std::string abi;
    std::string runtime;
    std::string compiler;
    std::string compiler_version;
    std::string dtype;
    std::string layout;
    std::string shape_spec;
    Canon::Record launch_geometry;
    std::string quantization;
    std::string interface;
    std::uint64_t generation = 0;
    std::string artifact_digest;
    Canon to_canon() const {
        Canon::Record r = Canon::rec();
        Canon::put_uuid(r, kernf::KernelArtifactId, kernel_artifact_id.value);
        Canon::put_str(r, kernf::Operation, operation);
        Canon::put_str(r, kernf::Architecture, architecture);
        Canon::put_str(r, kernf::ComputeCapability, compute_capability);
        Canon::put_str(r, kernf::Abi, abi);
        Canon::put_str(r, kernf::Runtime, runtime);
        Canon::put_str(r, kernf::Compiler, compiler);
        Canon::put_str(r, kernf::CompilerVersion, compiler_version);
        Canon::put_str(r, kernf::Dtype, dtype);
        Canon::put_str(r, kernf::Layout, layout);
        Canon::put_str(r, kernf::ShapeSpec, shape_spec);
        Canon::put_record(r, kernf::LaunchGeometry, launch_geometry);
        Canon::put_str(r, kernf::Quantization, quantization);
        Canon::put_str(r, kernf::Interface, interface);
        Canon::put_uint(r, kernf::Generation, generation);
        Canon::put_str(r, kernf::ArtifactDigest, artifact_digest);
        return Canon::mk_record(std::move(r));
    }
    static KernelProfile from_canon(const Canon& c) {
        KernelProfile p;
        const Canon* v = c.field(kernf::KernelArtifactId); if (!v||v->kind()!=CanonKind::Uuid) throw std::runtime_error("KernelProfile: kernel_artifact_id");
        p.kernel_artifact_id = KernelArtifactId(v->as_uuid());
        p.operation=gs(c,kernf::Operation); p.architecture=gs(c,kernf::Architecture);
        p.compute_capability=gs(c,kernf::ComputeCapability); p.abi=gs(c,kernf::Abi);
        p.runtime=gs(c,kernf::Runtime); p.compiler=gs(c,kernf::Compiler); p.compiler_version=gs(c,kernf::CompilerVersion);
        p.dtype=gs(c,kernf::Dtype); p.layout=gs(c,kernf::Layout); p.shape_spec=gs(c,kernf::ShapeSpec);
        v = c.field(kernf::LaunchGeometry); if (v && v->kind()==CanonKind::Record) p.launch_geometry = v->as_record();
        p.quantization=gs(c,kernf::Quantization); p.interface=gs(c,kernf::Interface);
        v = c.field(kernf::Generation); if (v && v->kind()==CanonKind::Uint) p.generation=v->as_uint();
        p.artifact_digest=gs(c,kernf::ArtifactDigest);
        return p;
    }
private:
    static std::string gs(const Canon& c, std::uint32_t tag) { const Canon* v=c.field(tag); if(!v||v->kind()!=CanonKind::Str) throw std::runtime_error("KernelProfile: missing str"); return v->as_string(); }
};

// --- graph ---
struct GraphProfile {
    GraphArtifactId graph_artifact_id;
    std::string topology;
    std::vector<KernelArtifactId> dependencies;
    std::string runtime;
    std::string backend;
    std::string architecture;
    std::string abi;
    std::string dtype;
    std::string layout;
    std::string shape_spec;
    Canon::Record launch_config;
    std::string memory_binding;
    std::uint64_t generation = 0;
    Canon to_canon() const {
        Canon::Record r = Canon::rec();
        Canon::put_uuid(r, graphf::GraphArtifactId, graph_artifact_id.value);
        Canon::put_str(r, graphf::Topology, topology);
        { Canon::Seq s; for (auto& d : dependencies) s.push_back(Canon::mk_uuid(d.value)); Canon::put_seq(r, graphf::Dependencies, std::move(s)); }
        Canon::put_str(r, graphf::Runtime, runtime);
        Canon::put_str(r, graphf::Backend, backend);
        Canon::put_str(r, graphf::Architecture, architecture);
        Canon::put_str(r, graphf::Abi, abi);
        Canon::put_str(r, graphf::Dtype, dtype);
        Canon::put_str(r, graphf::Layout, layout);
        Canon::put_str(r, graphf::ShapeSpec, shape_spec);
        Canon::put_record(r, graphf::LaunchConfig, launch_config);
        Canon::put_str(r, graphf::MemoryBinding, memory_binding);
        Canon::put_uint(r, graphf::Generation, generation);
        return Canon::mk_record(std::move(r));
    }
    static GraphProfile from_canon(const Canon& c) {
        GraphProfile p;
        const Canon* v = c.field(graphf::GraphArtifactId); if (!v||v->kind()!=CanonKind::Uuid) throw std::runtime_error("GraphProfile: graph_artifact_id");
        p.graph_artifact_id = GraphArtifactId(v->as_uuid());
        p.topology=gs(c,graphf::Topology);
        v = c.field(graphf::Dependencies); if (v && v->kind()==CanonKind::Sequence) for (auto& e : v->as_seq()) if (e.kind()==CanonKind::Uuid) p.dependencies.push_back(KernelArtifactId(e.as_uuid()));
        p.runtime=gs(c,graphf::Runtime); p.backend=gs(c,graphf::Backend); p.architecture=gs(c,graphf::Architecture);
        p.abi=gs(c,graphf::Abi); p.dtype=gs(c,graphf::Dtype); p.layout=gs(c,graphf::Layout); p.shape_spec=gs(c,graphf::ShapeSpec);
        v = c.field(graphf::LaunchConfig); if (v && v->kind()==CanonKind::Record) p.launch_config = v->as_record();
        p.memory_binding=gs(c,graphf::MemoryBinding);
        v = c.field(graphf::Generation); if (v && v->kind()==CanonKind::Uint) p.generation=v->as_uint();
        return p;
    }
private:
    static std::string gs(const Canon& c, std::uint32_t tag) { const Canon* v=c.field(tag); if(!v||v->kind()!=CanonKind::Str) throw std::runtime_error("GraphProfile: missing str"); return v->as_string(); }
};

// --- adapter ---
struct AdapterProfile {
    AdapterId adapter_id;
    std::string base_model_revision;
    std::string kind;
    Canon::Record rank_config;
    std::vector<std::string> target_modules;
    std::string dtype;
    std::string composition;
    Canon::Record activation;
    std::uint64_t generation = 0;
    std::string artifact_digest;
    Canon to_canon() const {
        Canon::Record r = Canon::rec();
        Canon::put_uuid(r, adapf::AdapterId, adapter_id.value);
        Canon::put_str(r, adapf::BaseModelRevision, base_model_revision);
        Canon::put_str(r, adapf::Kind, kind);
        Canon::put_record(r, adapf::RankConfig, rank_config);
        { Canon::Seq s; for (auto& m : target_modules) s.push_back(Canon::mk_str(m)); Canon::put_seq(r, adapf::TargetModules, std::move(s)); }
        Canon::put_str(r, adapf::Dtype, dtype);
        Canon::put_str(r, adapf::Composition, composition);
        Canon::put_record(r, adapf::Activation, activation);
        Canon::put_uint(r, adapf::Generation, generation);
        Canon::put_str(r, adapf::ArtifactDigest, artifact_digest);
        return Canon::mk_record(std::move(r));
    }
    static AdapterProfile from_canon(const Canon& c) {
        AdapterProfile p;
        const Canon* v = c.field(adapf::AdapterId); if (!v||v->kind()!=CanonKind::Uuid) throw std::runtime_error("AdapterProfile: adapter_id");
        p.adapter_id = AdapterId(v->as_uuid());
        p.base_model_revision=gs(c,adapf::BaseModelRevision); p.kind=gs(c,adapf::Kind);
        v = c.field(adapf::RankConfig); if (v && v->kind()==CanonKind::Record) p.rank_config = v->as_record();
        v = c.field(adapf::TargetModules); if (v && v->kind()==CanonKind::Sequence) for (auto& e : v->as_seq()) if (e.kind()==CanonKind::Str) p.target_modules.push_back(e.as_string());
        p.dtype=gs(c,adapf::Dtype); p.composition=gs(c,adapf::Composition);
        v = c.field(adapf::Activation); if (v && v->kind()==CanonKind::Record) p.activation = v->as_record();
        v = c.field(adapf::Generation); if (v && v->kind()==CanonKind::Uint) p.generation=v->as_uint();
        p.artifact_digest=gs(c,adapf::ArtifactDigest);
        return p;
    }
private:
    static std::string gs(const Canon& c, std::uint32_t tag) { const Canon* v=c.field(tag); if(!v||v->kind()!=CanonKind::Str) throw std::runtime_error("AdapterProfile: missing str"); return v->as_string(); }
};

// --- backend / toolchain ---
struct BackendProfile {
    BackendId backend_id;
    std::string runtime;
    std::string compiler;
    std::string compiler_version;
    std::string abi;
    std::string codegen_target;
    Canon::Record capabilities;
    std::string protocol_version;
    std::string interface_version;
    Canon to_canon() const {
        Canon::Record r = Canon::rec();
        Canon::put_uuid(r, backf::BackendId, backend_id.value);
        Canon::put_str(r, backf::Runtime, runtime);
        Canon::put_str(r, backf::Compiler, compiler);
        Canon::put_str(r, backf::CompilerVersion, compiler_version);
        Canon::put_str(r, backf::Abi, abi);
        Canon::put_str(r, backf::CodegenTarget, codegen_target);
        Canon::put_record(r, backf::Capabilities, capabilities);
        Canon::put_str(r, backf::ProtocolVersion, protocol_version);
        Canon::put_str(r, backf::InterfaceVersion, interface_version);
        return Canon::mk_record(std::move(r));
    }
    static BackendProfile from_canon(const Canon& c) {
        BackendProfile p;
        const Canon* v = c.field(backf::BackendId); if (!v||v->kind()!=CanonKind::Uuid) throw std::runtime_error("BackendProfile: backend_id");
        p.backend_id = BackendId(v->as_uuid());
        p.runtime=gs(c,backf::Runtime); p.compiler=gs(c,backf::Compiler); p.compiler_version=gs(c,backf::CompilerVersion);
        p.abi=gs(c,backf::Abi); p.codegen_target=gs(c,backf::CodegenTarget);
        v = c.field(backf::Capabilities); if (v && v->kind()==CanonKind::Record) p.capabilities = v->as_record();
        p.protocol_version=gs(c,backf::ProtocolVersion); p.interface_version=gs(c,backf::InterfaceVersion);
        return p;
    }
private:
    static std::string gs(const Canon& c, std::uint32_t tag) { const Canon* v=c.field(tag); if(!v||v->kind()!=CanonKind::Str) throw std::runtime_error("BackendProfile: missing str"); return v->as_string(); }
};

// --- device ---
struct DeviceProfile {
    DeviceId device_id;
    std::string vendor;
    std::string architecture;
    std::string compute_capability;
    std::string memory_model;
    std::optional<std::uint64_t> total_memory;
    std::vector<std::string> supported_dtypes;
    std::vector<std::string> instruction_classes;
    std::vector<std::string> execution_features;
    std::string runtime_min;
    std::string driver_min;
    Canon to_canon() const {
        Canon::Record r = Canon::rec();
        Canon::put_uuid(r, devf::DeviceId, device_id.value);
        Canon::put_str(r, devf::Vendor, vendor);
        Canon::put_str(r, devf::Architecture, architecture);
        Canon::put_str(r, devf::ComputeCapability, compute_capability);
        Canon::put_str(r, devf::MemoryModel, memory_model);
        if (total_memory) Canon::put_uint(r, devf::TotalMemory, *total_memory);
        { Canon::Seq s; for (auto& d : supported_dtypes) s.push_back(Canon::mk_str(d)); Canon::put_seq(r, devf::SupportedDtypes, std::move(s)); }
        { Canon::Seq s; for (auto& i : instruction_classes) s.push_back(Canon::mk_str(i)); Canon::put_seq(r, devf::InstructionClasses, std::move(s)); }
        { Canon::Seq s; for (auto& f : execution_features) s.push_back(Canon::mk_str(f)); Canon::put_seq(r, devf::ExecutionFeatures, std::move(s)); }
        Canon::put_str(r, devf::RuntimeMin, runtime_min);
        Canon::put_str(r, devf::DriverMin, driver_min);
        return Canon::mk_record(std::move(r));
    }
    static DeviceProfile from_canon(const Canon& c) {
        DeviceProfile p;
        const Canon* v = c.field(devf::DeviceId); if (!v||v->kind()!=CanonKind::Uuid) throw std::runtime_error("DeviceProfile: device_id");
        p.device_id = DeviceId(v->as_uuid());
        p.vendor=gs(c,devf::Vendor); p.architecture=gs(c,devf::Architecture); p.compute_capability=gs(c,devf::ComputeCapability);
        p.memory_model=gs(c,devf::MemoryModel);
        v = c.field(devf::TotalMemory); if (v && v->kind()==CanonKind::Uint) p.total_memory = v->as_uint();
        v = c.field(devf::SupportedDtypes); if (v && v->kind()==CanonKind::Sequence) for (auto& e : v->as_seq()) if (e.kind()==CanonKind::Str) p.supported_dtypes.push_back(e.as_string());
        v = c.field(devf::InstructionClasses); if (v && v->kind()==CanonKind::Sequence) for (auto& e : v->as_seq()) if (e.kind()==CanonKind::Str) p.instruction_classes.push_back(e.as_string());
        v = c.field(devf::ExecutionFeatures); if (v && v->kind()==CanonKind::Sequence) for (auto& e : v->as_seq()) if (e.kind()==CanonKind::Str) p.execution_features.push_back(e.as_string());
        p.runtime_min=gs(c,devf::RuntimeMin); p.driver_min=gs(c,devf::DriverMin);
        return p;
    }
private:
    static std::string gs(const Canon& c, std::uint32_t tag) { const Canon* v=c.field(tag); if(!v||v->kind()!=CanonKind::Str) throw std::runtime_error("DeviceProfile: missing str"); return v->as_string(); }
};

// --- protocol ---
struct ProtocolProfile {
    ProtocolId protocol_id;
    std::string family;
    std::uint64_t major = 0;
    std::uint64_t minor = 0;
    std::optional<std::uint64_t> message_schema_generation;
    Canon::Record feature_bits;
    std::vector<std::string> required_semantics;
    std::vector<std::string> optional_semantics;
    Canon to_canon() const {
        Canon::Record r = Canon::rec();
        Canon::put_uuid(r, protf::ProtocolId, protocol_id.value);
        Canon::put_str(r, protf::Family, family);
        Canon::put_uint(r, protf::Major, major);
        Canon::put_uint(r, protf::Minor, minor);
        if (message_schema_generation) Canon::put_uint(r, protf::MessageSchemaGeneration, *message_schema_generation);
        Canon::put_record(r, protf::FeatureBits, feature_bits);
        { Canon::Seq s; for (auto& x : required_semantics) s.push_back(Canon::mk_str(x)); Canon::put_seq(r, protf::RequiredSemantics, std::move(s)); }
        { Canon::Seq s; for (auto& x : optional_semantics) s.push_back(Canon::mk_str(x)); Canon::put_seq(r, protf::OptionalSemantics, std::move(s)); }
        return Canon::mk_record(std::move(r));
    }
    static ProtocolProfile from_canon(const Canon& c) {
        ProtocolProfile p;
        const Canon* v = c.field(protf::ProtocolId); if (!v||v->kind()!=CanonKind::Uuid) throw std::runtime_error("ProtocolProfile: protocol_id");
        p.protocol_id = ProtocolId(v->as_uuid());
        p.family=gs(c,protf::Family);
        v = c.field(protf::Major); if (v&&v->kind()==CanonKind::Uint) p.major=v->as_uint();
        v = c.field(protf::Minor); if (v&&v->kind()==CanonKind::Uint) p.minor=v->as_uint();
        v = c.field(protf::MessageSchemaGeneration); if (v && v->kind()==CanonKind::Uint) p.message_schema_generation = v->as_uint();
        v = c.field(protf::FeatureBits); if (v && v->kind()==CanonKind::Record) p.feature_bits = v->as_record();
        v = c.field(protf::RequiredSemantics); if (v && v->kind()==CanonKind::Sequence) for (auto& e : v->as_seq()) if (e.kind()==CanonKind::Str) p.required_semantics.push_back(e.as_string());
        v = c.field(protf::OptionalSemantics); if (v && v->kind()==CanonKind::Sequence) for (auto& e : v->as_seq()) if (e.kind()==CanonKind::Str) p.optional_semantics.push_back(e.as_string());
        return p;
    }
private:
    static std::string gs(const Canon& c, std::uint32_t tag) { const Canon* v=c.field(tag); if(!v||v->kind()!=CanonKind::Str) throw std::runtime_error("ProtocolProfile: missing str"); return v->as_string(); }
};

// --- policy ---
struct PolicyProfile {
    PolicyId policy_id;
    std::uint64_t generation = 0;
    std::string strictness;
    std::vector<std::string> permitted_adaptations;
    std::string fallback;
    Canon to_canon() const {
        Canon::Record r = Canon::rec();
        Canon::put_uuid(r, polf::PolicyId, policy_id.value);
        Canon::put_uint(r, polf::Generation, generation);
        Canon::put_str(r, polf::Strictness, strictness);
        { Canon::Seq s; for (auto& a : permitted_adaptations) s.push_back(Canon::mk_str(a)); Canon::put_seq(r, polf::PermittedAdaptations, std::move(s)); }
        Canon::put_str(r, polf::Fallback, fallback);
        return Canon::mk_record(std::move(r));
    }
    static PolicyProfile from_canon(const Canon& c) {
        PolicyProfile p;
        const Canon* v = c.field(polf::PolicyId); if (!v||v->kind()!=CanonKind::Uuid) throw std::runtime_error("PolicyProfile: policy_id");
        p.policy_id = PolicyId(v->as_uuid());
        v = c.field(polf::Generation); if (v&&v->kind()==CanonKind::Uint) p.generation=v->as_uint();
        p.strictness=gs(c,polf::Strictness);
        v = c.field(polf::PermittedAdaptations); if (v && v->kind()==CanonKind::Sequence) for (auto& e : v->as_seq()) if (e.kind()==CanonKind::Str) p.permitted_adaptations.push_back(e.as_string());
        p.fallback=gs(c,polf::Fallback);
        return p;
    }
private:
    static std::string gs(const Canon& c, std::uint32_t tag) { const Canon* v=c.field(tag); if(!v||v->kind()!=CanonKind::Str) throw std::runtime_error("PolicyProfile: missing str"); return v->as_string(); }
};

} // namespace compat
