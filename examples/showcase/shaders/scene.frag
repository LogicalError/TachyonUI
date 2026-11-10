#version 450
#extension GL_EXT_buffer_reference : require

// Flat colors lit from one side, with some light everywhere; the ground grid unlit.

struct Vertex
{
    vec4 position;
    vec4 normal;
};

layout (buffer_reference, std430) readonly buffer Vertices { Vertex v[]; };

layout (push_constant) uniform Push
{
    layout(row_major) mat4 mvp;
    vec4     color;
    vec4     light;
    Vertices vertices;
} push;

layout (location = 0) in vec3 inNormal;
layout (location = 0) out vec4 outColor;

void main()
{
    float shade = 1.0;
    if (push.light.w > 0.0)
        shade = 0.3 + 0.7 * max(dot(normalize(inNormal), normalize(push.light.xyz)), 0.0);
    outColor = vec4(push.color.rgb * shade, push.color.a);
}
