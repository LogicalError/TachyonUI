#version 450
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_shader_explicit_arithmetic_types_int8 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int16 : require
#extension GL_EXT_shader_explicit_arithmetic_types_float16 : require
#extension GL_GOOGLE_include_directive : require

#include "box_common.glsl"

layout (location = 0) out flat SurfaceData outSurface;
layout (location = 9) out FragmentData outData;
layout (location = 16) out flat float outOpacity;



void main() 
{
	int boxIndex = gl_VertexIndex / 4;
	int vertexIndex = gl_VertexIndex % 4; // the corner, from the index buffer

	Box box = PushConstants.boxBuffer.boxes[boxIndex];

	// use index offsets to calculate box vertices
	int index = vertexIndex % 4;
	vec2 uv = vec2(     index / 2,   // horizontal 0 or 1
				   1 - (index % 2)); // vertical   0 or 1
				   	
	// transformed box
	mat3x3 transform = transpose(mat3x3(box.transform0.xyz, box.transform1.xyz, vec3(0,0,1))); 
	vec3 boxMin = vec3(box.pos, 1);
	vec3 boxMax = boxMin + vec3(box.size, 0);
	boxMin = transform * boxMin;
	boxMax = transform * boxMax;


	vec2 pos  = (boxMin.xy / boxMin.z);
	vec2 size = (boxMax.xy / boxMax.z) - pos;


	// The drawn box in whole pixels: snapped relative to its parent when the CPU
	// supplies it (gui_pixel_snap.h), else each edge to the nearest pixel (half up).
	// A non-empty box keeps at least one pixel.
	ivec4 parentSnapped = ivec4(box.snappedPx);
	bool  hasSnapped    = parentSnapped.z > parentSnapped.x || parentSnapped.w > parentSnapped.y;
	vec2 snappedMin  = hasSnapped ? vec2(parentSnapped.xy) : floor(pos + 0.5);
	vec2 snappedMax  = hasSnapped ? vec2(parentSnapped.zw) : floor(pos + size + 0.5);
	snappedMax       = max(snappedMax, snappedMin + vec2(greaterThan(size, vec2(0))));
	vec2 snappedSize = snappedMax - snappedMin;

	vec2 corner   = clamp(mix(snappedMin, snappedMax, uv), vec2(box.clipMin), vec2(box.clipMax));
	vec2 position = corner + 0.5; // pixel-center alignment

	vec2 clipped_uv = (corner - snappedMin) / max(snappedSize, vec2(1));


	// Extract the content transform's scale as the geometric mean of X and Y axis lengths.
	// This is 1.0 for identity (no zoom), and equals the zoom factor for uniform scaling.
	float scaleX = length(vec2(box.transform0.x, box.transform1.x));
	float scaleY = length(vec2(box.transform0.y, box.transform1.y));

	outData.UV = clipped_uv;
	outSurface.boxSize   = snappedSize;
	outSurface.axisScale = vec2(scaleX, scaleY);
	outSurface.transformScale = sqrt(scaleX * scaleY);
	// The state styles mixed, each with its share.
	for (int i = 0; i < 4; i++)
	{
		outSurface.styleIndex[i]   = int(box.styles[i] & 0xFFFFFFu);
		outSurface.blendFactors[i] = float(box.styles[i] >> 24) / 255.0;
	}

	outSurface.textureIndex = box.textureIndex;

	outSurface.colorIndex = int(box.colorIndex);

	gl_Position = PrimitivePosition(position, box.transform0.w);
	outOpacity  = PrimitiveOpacity(box.transform1);
}
