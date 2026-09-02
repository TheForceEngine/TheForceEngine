#pragma once
//////////////////////////////////////////////////////////////////////
// GPU Based blit.
//////////////////////////////////////////////////////////////////////

#include <TFE_System/types.h>
#include <TFE_RenderBackend/shader.h>

class TextureGpu;
class DynamicTexture;

enum PostEffectInputType
{
	PTYPE_TEXTURE = 0,
	PTYPE_DYNAMIC_TEX,
	PTYPE_COUNT
};

struct PostEffectInput
{
	PostEffectInputType type;
	union
	{
		void* ptr;				// This void pointer is here so that array initializers work in C++.
		TextureGpu* tex;		// Standard GPU texture.
		DynamicTexture* dyntex;	// Dynamic GPU texture.
	};
};

class PostProcessEffect
{
public:
	// Load shaders, setup geometry.
	virtual bool init() = 0;
	// Free GPU assets.
	virtual void destroy() = 0;

	// Set the post process render state.
	virtual void setEffectState() = 0;

public:
	// Defaulted to null/-1 so that an effect whose init() fails partway
	// (e.g. a shader that failed to compile) leaves m_shader in a safely
	// checkable state instead of uninitialized garbage.
	Shader* m_shader = nullptr;
	s32 m_scaleOffsetId = -1;
};
