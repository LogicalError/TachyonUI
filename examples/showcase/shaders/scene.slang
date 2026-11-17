// The showcase's 3D scene: a mesh's vertices read from a buffer by address, moved by the
// object's matrix to the screen, in flat colors lit from one side with some light everywhere;
// the ground grid unlit.

struct Vertex
{
    float4 position;   // xyz; w unused
    float4 normal;     // xyz, in the object's own space
};

// The matrix is its four rows, as TachyonUI stores it (row-major).
struct Push
{
    float4  mvpRows[4];
    float4  color;
    float4  light;      // xyz: toward the light, in the object's space; w 0: unlit
    Vertex* vertices;
};
[[vk::push_constant]] ConstantBuffer<Push> push;

struct VSOutput
{
    float4 position : SV_Position;
    float3 normal   : NORMAL;
};

[shader("vertex")]
VSOutput vertexMain(int vertexIndex : SV_VulkanVertexID)
{
    Vertex vertex = push.vertices[vertexIndex];
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
