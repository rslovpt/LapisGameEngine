#pragma once

// Every built-in shader, by name, in each shading language. Backends look
// shaders up here when a pipeline is created. Constant buffer layouts are in
// ShaderTypes.h and must match the blocks declared in both languages.
//
//   GLSL 330: uniform blocks "CB0"/"CB1", samplers named t0..t7, vertex
//             inputs at layout(location = n).
//   HLSL (SM 5.1, D3D12): entry points VSMain / PSMain, cbuffers b0/b1,
//             textures t0..t7, samplers s0..s4 (see RHI::Sampler), vertex
//             inputs use the semantic ATTRIB<n>.

namespace Lapis::ShaderLibrary {

struct Source {
    const char* Name;
    const char* GLSLVertex;
    const char* GLSLFragment;
    const char* HLSL;          // nullptr = not available in HLSL
};

const Source* Find(const char* name);
const Source* All(int& count);

} // namespace Lapis::ShaderLibrary
