// The showcase's 3D scene: a mesh's vertices read from a buffer, moved by the object's matrix to
// the screen, in flat colors lit from one side with some light everywhere; the ground grid
// unlit. Vulkan reads the buffer by address from the push constants; Direct3D 12 (TACHYON_D3D12)
// gets the same values as root constants (b0) and the buffer as a root view (t0); SDL GPU
// (TACHYON_SDLGPU) as uniforms and a storage buffer; Metal (TACHYON_METAL) as Direct3D 12 does,
// the constants at buffer(0) and the vertices at buffer(1).

struct Vertex
{
    float4 position;   // xyz; w unused
    float4 normal;     // xyz, in the object's own space
};

#if defined(TACHYON_D3D12)
// The matrix is its four rows, as TachyonUI stores it (row-major).
struct Push
{
    float4 mvpRows[4];
    float4 color;
    float4 light;         // xyz: toward the light, in the object's space; w 0: unlit
    uint   firstVertex;   // the draw's first vertex: SV_VertexID starts at 0
};
ConstantBuffer<Push>     push     : register(b0);
StructuredBuffer<Vertex> vertices : register(t0);

struct VertexInput { uint id : SV_VertexID; };
Vertex ReadVertex(VertexInput input) { return vertices[input.id + push.firstVertex]; }
#elif defined(TACHYON_METAL)
// Metal: as Direct3D 12, without registers: slangc numbers the buffers in the order declared.
struct Push
{
    float4 mvpRows[4];
    float4 color;
    float4 light;         // xyz: toward the light, in the object's space; w 0: unlit
    uint   firstVertex;   // the draw's first vertex: SV_VertexID starts at 0
};
ConstantBuffer<Push>     push;       // buffer(0)
StructuredBuffer<Vertex> vertices;   // buffer(1)

struct VertexInput { uint id : SV_VertexID; };
Vertex ReadVertex(VertexInput input) { return vertices[input.id + push.firstVertex]; }
#elif defined(TACHYON_SDLGPU)
// SDL GPU: the same values as uniforms, where SDL puts each stage's (set 1 / space1 for the
// vertex stage, set 3 / space3 for the fragment stage), and the buffer as the vertex stage's
// storage buffer (set 0 / t0, space0).
#if defined(TACHYON_SDLGPU_FRAGMENT)
#define SCENE_UNIFORMS 3
#define SCENE_UNIFORM_SPACE space3
#else
#define SCENE_UNIFORMS 1
#define SCENE_UNIFORM_SPACE space1
#endif
struct Push
{
    float4 mvpRows[4];
    float4 color;
    float4 light;         // xyz: toward the light, in the object's space; w 0: unlit
    uint   firstVertex;   // the draw's first vertex: SV_VertexID starts at 0
};
#if defined(TACHYON_SDLGPU_METAL)
// Metal: the uniforms, then the storage buffer, at buffer(0) and buffer(1) in declaration order.
ConstantBuffer<Push>     push;
StructuredBuffer<Vertex> vertices;
#else
[[vk::binding(0, SCENE_UNIFORMS)]] ConstantBuffer<Push> push : register(b0, SCENE_UNIFORM_SPACE);
[[vk::binding(0, 0)]] StructuredBuffer<Vertex> vertices : register(t0, space0);
#endif

struct VertexInput { uint id : SV_VertexID; };
Vertex ReadVertex(VertexInput input) { return vertices[input.id + push.firstVertex]; }
#else
// The matrix is its four rows, as TachyonUI stores it (row-major).
struct Push
{
    float4  mvpRows[4];
    float4  color;
    float4  light;      // xyz: toward the light, in the object's space; w 0: unlit
    Vertex* vertices;
};
[[vk::push_constant]] ConstantBuffer<Push> push;

struct VertexInput { int id : SV_VulkanVertexID; };
Vertex ReadVertex(VertexInput input) { return push.vertices[input.id]; }
#endif

struct VSOutput
{
    float4 position : SV_Position;
    float3 normal   : NORMAL;
};

[shader("vertex")]
VSOutput vertexMain(VertexInput input)
{
    Vertex vertex = ReadVertex(input);
    float4 p = float4(vertex.position.xyz, 1.0);

    VSOutput output;
    output.position = float4(dot(push.mvpRows[0], p), dot(push.mvpRows[1], p),
                             dot(push.mvpRows[2], p), dot(push.mvpRows[3], p));
    output.normal   = vertex.normal.xyz;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target
{
    float shade = 1.0;
    if (push.light.w > 0.0)
        shade = 0.3 + 0.7 * max(dot(normalize(input.normal), normalize(push.light.xyz)), 0.0);
    return float4(push.color.rgb * shade, push.color.a);
}
