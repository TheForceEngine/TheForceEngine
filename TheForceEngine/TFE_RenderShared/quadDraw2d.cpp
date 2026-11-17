#include "quadDraw2d.h"
#include <TFE_RenderBackend/shader.h>
#include <TFE_RenderBackend/vertexBuffer.h>
#include <TFE_RenderBackend/indexBuffer.h>
#include <TFE_System/system.h>
#include <vector>

#define QUAD_MAX_NUM 1024

namespace TFE_RenderShared
{
	// Vertex Definition
	struct QuadVertex
	{
		Vec2f pos;		// 2D position.
		Vec2f uv;		// UV coordinates.
		u32   color;	// color + opacity.
	};
	struct QuadDraw
	{
		TextureGpu* texture;
		s32 offset;
		s32 count;
		bool greyScale;
	};
	static const AttributeMapping c_quadAttrMapping[]=
	{
		{ATTR_POS,   ATYPE_FLOAT, 2, 0, false},
		{ATTR_UV,    ATYPE_FLOAT, 2, 0, false},
		{ATTR_COLOR, ATYPE_UINT8, 4, 0, true}
	};
	static const u32 c_quadAttrCount = TFE_ARRAYSIZE(c_quadAttrMapping);

	static s32 s_svScaleOffset = -1;

	static Shader s_shader;
	static Shader s_shaderGreyscale;
	static VertexBuffer s_vertexBuffer;
	static IndexBuffer  s_indexBuffer;

	static QuadVertex* s_vertices = nullptr;
	static u32 s_quadCount;
	static u32 s_quadDrawCount;

	static QuadDraw s_quadDraw[QUAD_MAX_NUM];

	static u32 s_width, s_height;

	bool quadInit()
	{
		if (!s_shader.load("Shaders/quad2d.vert", "Shaders/quad2dColor.frag"))
		{
			return false;
		}
		if (!s_shaderGreyscale.load("Shaders/quad2d.vert", "Shaders/quad2dGreyscale.frag"))
		{
			return false;
		}
		s_shader.bindTextureNameToSlot("Image", 0);
		s_shaderGreyscale.bindTextureNameToSlot("Image", 0);

		s_svScaleOffset = s_shader.getVariableId("ScaleOffset");
		if (s_svScaleOffset < 0)
		{
			return false;
		}

		// Create buffers
		// Create vertex and index buffers.
		u32* indices = new u32[6 * QUAD_MAX_NUM];
		u32* outIndices = indices;
		for (u32 i = 0; i < QUAD_MAX_NUM; i++, outIndices += 6)
		{
			outIndices[0] = i * 4 + 0;
			outIndices[1] = i * 4 + 1;
			outIndices[2] = i * 4 + 2;

			outIndices[3] = i * 4 + 0;
			outIndices[4] = i * 4 + 2;
			outIndices[5] = i * 4 + 3;
		}
		s_vertices = new QuadVertex[4 * QUAD_MAX_NUM];

		s_vertexBuffer.create(4 * QUAD_MAX_NUM, sizeof(QuadVertex), c_quadAttrCount, c_quadAttrMapping, true);
		s_indexBuffer.create(6 * QUAD_MAX_NUM, sizeof(u32), false, indices);

		delete[] indices;
		s_quadCount = 0;
		s_quadDrawCount = 0;

		return true;
	}

	void quadDestroy()
	{
		s_vertexBuffer.destroy();
		s_indexBuffer.destroy();
		delete[] s_vertices;
		s_vertices = nullptr;
	}
	
	void quadDraw2d_begin(u32 width, u32 height)
	{
		s_width = width;
		s_height = height;
		s_quadCount = 0;
		s_quadDrawCount = 0;
	}

	void quadDraw2d_add(u32 count, const Vec2f* quads, const u32* quadColors, f32 u0, f32 u1, TextureGpu* texture, bool greyScale)
	{
		if (!s_vertices) { return; }

		s_quadDraw[s_quadDrawCount].texture = texture;
		s_quadDraw[s_quadDrawCount].offset = s_quadCount;
		s_quadDraw[s_quadDrawCount].count = count;
		s_quadDraw[s_quadDrawCount].greyScale = greyScale;
		s_quadDrawCount++;

		QuadVertex* vert = &s_vertices[s_quadCount * 4];
		for (u32 i = 0; i < count && s_quadCount < QUAD_MAX_NUM; i++, quads += 2, quadColors += 2, vert += 4)
		{
			s_quadCount++;

			// Convert to pixels
			f32 x0 = quads[0].x;
			f32 x1 = quads[1].x;

			f32 y0 = quads[0].z;
			f32 y1 = quads[1].z;

			vert[0].pos.x = x0;
			vert[1].pos.x = x1;
			vert[2].pos.x = x1;
			vert[3].pos.x = x0;

			vert[0].pos.z = y0;
			vert[1].pos.z = y0;
			vert[2].pos.z = y1;
			vert[3].pos.z = y1;

			vert[0].uv.x = u0;
			vert[1].uv.x = u1;
			vert[2].uv.x = u1;
			vert[3].uv.x = u0;

			vert[0].uv.z = 0.0f;
			vert[1].uv.z = 0.0f;
			vert[2].uv.z = 1.0f;
			vert[3].uv.z = 1.0f;

			// Copy the colors.
			vert[0].color = quadColors[0];
			vert[1].color = quadColors[0];
			vert[2].color = quadColors[1];
			vert[3].color = quadColors[1];
		}
	}

	void quadDraw2d_add(const Vec2f* vertices, const u32* colors, TextureGpu* texture, bool greyScale)
	{
		quadDraw2d_add(1, vertices, colors, 0.0f, 1.0f, texture, greyScale);
	}

	void quadDraw2d_add(const Vec2f* vertices, const u32* colors, f32 u0, f32 u1, TextureGpu* texture, bool greyScale)
	{
		quadDraw2d_add(1, vertices, colors, u0, u1, texture, greyScale);
	}

	void quadDraw2d_draw()
	{
		if (s_quadCount < 1 || s_quadDrawCount < 1) { return; }

		s_vertexBuffer.update(s_vertices, s_quadCount * 4 * sizeof(QuadVertex));

		// Bind vertex/index buffers and setup attributes for BlitVert
		s_vertexBuffer.bind();
		u32 indexSizeType = s_indexBuffer.bind();

		const f32 scaleX = 2.0f / f32(s_width);
		const f32 scaleY = 2.0f / f32(s_height);
		const f32 offsetX = -1.0f;
		const f32 offsetY = -1.0f;

		TFE_RenderState::setStateEnable(false, STATE_CULLING | STATE_DEPTH_TEST | STATE_DEPTH_WRITE);

		// Enable blending.
		TFE_RenderState::setStateEnable(true, STATE_BLEND);
		TFE_RenderState::setBlendMode(BLEND_ONE, BLEND_ONE_MINUS_SRC_ALPHA);

		// 1. Draw coloured quads
		s_shader.bind();
		// Bind Uniforms & Textures.
		const f32 scaleOffset[] = { scaleX, scaleY, offsetX, offsetY };
		s_shader.setVariable(s_svScaleOffset, SVT_VEC4, scaleOffset);

		for (u32 i = 0; i < s_quadDrawCount; i++)
		{
			QuadDraw* draw = &s_quadDraw[i];
			if (draw->greyScale) { continue; }
			s_quadDraw[i].texture->bind();
			TFE_RenderBackend::drawIndexedTriangles(draw->count * 2, sizeof(u32), draw->offset * 6);
		}
		s_shader.unbind();

		// 2. Draw greyscale quads
		s_shaderGreyscale.bind();
		// Bind Uniforms & Textures.
		s_shaderGreyscale.setVariable(s_svScaleOffset, SVT_VEC4, scaleOffset);

		for (u32 i = 0; i < s_quadDrawCount; i++)
		{
			QuadDraw* draw = &s_quadDraw[i];
			if (!draw->greyScale) { continue; }
			s_quadDraw[i].texture->bind();
			TFE_RenderBackend::drawIndexedTriangles(draw->count * 2, sizeof(u32), draw->offset * 6);
		}
		s_shaderGreyscale.unbind();

		// Cleanup.
		s_vertexBuffer.unbind();
		s_indexBuffer.unbind();

		// Clear
		s_quadCount = 0;
	}
}
