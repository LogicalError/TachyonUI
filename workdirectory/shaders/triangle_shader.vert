#version 450
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_shader_explicit_arithmetic_types_int8 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int16 : require
#extension GL_EXT_shader_explicit_arithmetic_types_float16 : require
#extension GL_GOOGLE_include_directive : require

#include "triangle_common.glsl"

layout (location = 0) out flat TriSurfaceData outSurface;
layout (location = 4) out TriFragmentData outData;
layout (location = 16) out flat float outOpacity;


void main()
{
	int triangleIndex = gl_VertexIndex / 4; // the bounding quad; the fragment shader tests the interior
	int vertexIndex = gl_VertexIndex % 4; // the corner, from the index buffer

	Triangle triangle = PushConstants.triangleBuffer.triangles[triangleIndex];
	
	// use index offsets to calculate triangle vertices
	int index = vertexIndex % 4;
	vec2 uv = vec2(index / 2, 1 - (index % 2));

	 
	vec2 p0 = vec2(triangle.p0);
	vec2 p1 = vec2(triangle.p1);
	vec2 p2 = vec2(triangle.p2);

	// Apply the renderer's content transform, like the box/line/dot shaders.
	mat3x3 transform = transpose(mat3x3(triangle.transform0.xyz, triangle.transform1.xyz, vec3(0,0,1)));
	vec3 p0_t = transform * vec3(p0, 1);
	vec3 p1_t = transform * vec3(p1, 1);
	vec3 p2_t = transform * vec3(p2, 1);
	p0 = p0_t.xy / p0_t.z;
	p1 = p1_t.xy / p1_t.z;
	p2 = p2_t.xy / p2_t.z;

	vec2 triMin = min(min(p0, p1), p2);
	vec2 triMax = max(max(p0, p1), p2);
	vec2 triSize = max(triMax - triMin, vec2(1, 1));


	// The quad drawn: a pixel past the triangle all round, for the soft edge outside it.
	vec2 quadMin  = triMin - 1.0;
	vec2 quadSize = (triMax + 1.0) - quadMin;

	vec2 clipMin = triangle.clipMin;
	vec2 clipMax = triangle.clipMax;

	clipMin.x = max(clipMin.x, quadMin.x);
	clipMin.y = max(clipMin.y, quadMin.y);
	clipMax.x = min(clipMax.x, quadMin.x + quadSize.x);
	clipMax.y = min(clipMax.y, quadMin.y + quadSize.y);


	vec2 clipped_min = vec2(clipMin) - quadMin;
	vec2 clipped_max = vec2(clipMax) - quadMin;
	vec2 clipped_uv = clamp(uv * quadSize, clipped_min, clipped_max);

	vec2 position = quadMin + clipped_uv + 0.5;

	outSurface.triMin = triMin;
	outSurface.triSize = triSize;
	outSurface.triangleIndex = triangleIndex;
	outSurface.textureIndex = triangle.textureIndex;
	outData.position = position;

	gl_Position = PrimitivePosition(position, triangle.transform0.w);
	outOpacity  = PrimitiveOpacity(triangle.transform1);
}
