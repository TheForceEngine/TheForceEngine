#pragma once
//////////////////////////////////////////////////////////////////////
// GPU 2D antialiased line drawing
//////////////////////////////////////////////////////////////////////

#include <TFE_System/types.h>
#include <TFE_RenderBackend/renderBackend.h>

namespace TFE_RenderShared
{
	bool quadInit();
	void quadDestroy();

	void quadDraw2d_begin(u32 width, u32 height);
	void quadDraw2d_add(u32 count, const Vec2f* vertices, const u32* colors, f32 u0, f32 u1, TextureGpu* texture, bool greyScale = false);
	void quadDraw2d_add(const Vec2f* vertices, const u32* colors, TextureGpu* texture, bool greyScale = false);
	void quadDraw2d_add(const Vec2f* vertices, const u32* colors, f32 u0, f32 u1, TextureGpu* texture, bool greyScale = false);

	void quadDraw2d_draw();
}
