#pragma once
//////////////////////////////////////////////////////////////////////
// Wrapper for OpenGL textures that expose the limited set of 
// required functionality.
//////////////////////////////////////////////////////////////////////

#include <TFE_System/types.h>
#include <TFE_RenderBackend/textureGpu.h>

class RenderTarget
{
public:
	RenderTarget() : m_texture{ nullptr }, m_textureCount(0), m_gpuHandle(0), m_depthBufferHandle(0) {}
	~RenderTarget();

	bool create(s32 textureCount, TextureGpu** textures, bool depthBuffer);
	void bind();
	void clear(const f32* color, f32 depth, u8 stencil = 0, bool clearColor = true);
	void clearDepth(f32 depth);
	void clearStencil(u8 stencil);
	static void copy(RenderTarget* dst, RenderTarget* src, MagFilter filter = MAG_FILTER_NONE);
	static void copyBackbufferToTarget(RenderTarget* dst);
	static void unbind();

	inline const TextureGpu* getTexture(s32 index = 0) const { return m_texture[index]; }

private:
	enum RTConst
	{
		MAX_ATTACHMENT = 4,
	};

	const TextureGpu* m_texture[MAX_ATTACHMENT];
	u32 m_textureCount;
	u32 m_gpuHandle;
	u32 m_depthBufferHandle;
};
