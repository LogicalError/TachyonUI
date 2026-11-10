#version 450
#extension GL_EXT_buffer_reference : require

// The showcase's 3D scene: a mesh's vertices read from a buffer by address, moved by the
// object's matrix to the screen. Row-major matrices, as TachyonUI's are.

struct Vertex
{
    vec4 position;   // xyz; w unused
    vec4 normal;     // xyz, in the object's own space
};

layout (buffer_reference, std430) readonly buffer Vertices { Vertex v[]; };

layout (push_constant) uniform Push
{
    layout(row_major) mat4 mvp;
    vec4     color;
    vec4     light;      // xyz: toward the light, in the object's space; w 0: unlit
    Vertices vertices;
} push;

layout (location = 0) out vec3 outNormal;

void main()
{
    Vertex vertex = push.vertices.v[gl_VertexIndex];
    gl_Position = push.mvp * vec4(vertex.position.xyz, 1.0);
    outNormal = vertex.normal.xyz;
}
