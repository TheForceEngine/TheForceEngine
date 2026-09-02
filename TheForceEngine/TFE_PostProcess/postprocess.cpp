#include <cstring>

#include "postprocess.h"
#include "postprocesseffect.h"
#include "overlay.h"
#include <TFE_RenderBackend/vertexBuffer.h>
#include <TFE_RenderBackend/indexBuffer.h>
#include <TFE_System/profiler.h>
#include <assert.h>
#include <vector>

namespace TFE_PostProcess
{
	enum EffectFlags
	{
		EFLAG_NONE = 0,
		EFLAG_USE_DYNAMIC_INPUT = (1 << 0),
	};

	struct PostEffectInstance
	{
		PostProcessEffect* effect;
		u32 flags;
		u32 inputCount;
		PostEffectInput* inputs;
		RenderTargetHandle output;
		s32 x;
		s32 y;
		s32 width;
		s32 height;
		bool forceLinearFilter;
	};

	struct EffectVertex
	{
		f32 x, y;
		f32 u, v;
	};
	static const AttributeMapping c_effectAttrMapping[] =
	{
		{ATTR_POS, ATYPE_FLOAT, 2, 0, false},
		{ATTR_UV,  ATYPE_FLOAT, 2, 0, false},
	};
	static const u32 c_effectAttrCount = TFE_ARRAYSIZE(c_effectAttrMapping);

	static VertexBuffer s_vertexBuffer;
	static IndexBuffer s_indexBuffer;
	static f32 s_screenScale[2];
	static std::vector<PostEffectInstance> s_effects;

	static Overlay* s_overlayEffect = nullptr;

	////////////////////////////////////////////////
	// Forward Declarations
	////////////////////////////////////////////////
	void updateRenderTargetScale(RenderTargetHandle renderTarget);
	void setScaleOffset(Shader* shader, s32 variableId, s32 x, s32 y, s32 width, s32 height);

	////////////////////////////////////////////////
	// Implementation
	////////////////////////////////////////////////
	bool init()
	{
		// Upload vertex/index buffers
		const EffectVertex vertices[] =
		{
			{0.0f, 0.0f, 0.0f, 1.0f},
			{1.0f, 0.0f, 1.0f, 1.0f},
			{1.0f, 1.0f, 1.0f, 0.0f},
			{0.0f, 1.0f, 0.0f, 0.0f},
		};
		const u16 indices[] =
		{
			0, 1, 2,
			0, 2, 3
		};
		s_vertexBuffer.create(4, sizeof(EffectVertex), c_effectAttrCount, c_effectAttrMapping, false, (void*)vertices);
		s_indexBuffer.create(6, sizeof(u16), false, (void*)indices);
		s_overlayEffect = new Overlay();
		s_overlayEffect->init();

		return true;
	}

	void destroy()
	{
		clearEffectStack();
		removeAllOverlays();
		s_vertexBuffer.destroy();
		s_indexBuffer.destroy();
		s_overlayEffect->destroy();
		delete s_overlayEffect;
		s_overlayEffect = nullptr;
	}

	void clearEffectStack()
	{
		s_effects.clear();
	}

	void appendEffect(PostProcessEffect* effect, u32 inputCount, const PostEffectInput* inputs, RenderTargetHandle output, s32 x, s32 y, s32 width, s32 height, bool forceLinearFilter)
	{
		PostEffectInstance instance =
		{
			effect,
			0,
			inputCount,
			nullptr,
			output,
			x, y,
			width, height,
			forceLinearFilter
		};
		instance.inputs = new PostEffectInput[inputCount];
		memcpy(instance.inputs, inputs, inputCount * sizeof(PostEffectInput));

		s_effects.push_back(instance);
	}

	struct OverlayEffect
	{
		OverlayID id;
		OverlayImage overlay;
		bool active;
		f32 tint[4] = { 0 };
		f32 x = 0.0f, y = 0.0f;
		f32 scale = 1.0f;
	};
	std::vector<OverlayEffect> s_overlayEffects;
	std::vector<OverlayID> s_freeOverlayEffects;

	// Overlays - images drawn on top after the post process stack has executed.
	OverlayID addOverlayTexture(OverlayImage* overlay, f32* tint, f32 screenX, f32 screenY, f32 scale)
	{
		OverlayEffect effect = {};
		effect.active = true;
		effect.overlay = *overlay;
		for (s32 i = 0; i < 4; i++) { effect.tint[i] = tint ? tint[i] : 1.0f; }
		effect.x = screenX;
		effect.y = screenY;
		effect.scale = scale;

		OverlayID id;
		if (s_freeOverlayEffects.empty())
		{
			id = (OverlayID)s_overlayEffects.size();
			effect.id = id;
			s_overlayEffects.push_back(effect);
		}
		else
		{
			id = s_freeOverlayEffects.back();
			s_freeOverlayEffects.pop_back();

			effect.id = id;
			s_overlayEffects[id] = effect;
		}
		return id;
	}

	const OverlayImage* getOverlayImage(OverlayID id)
	{
		if (id >= s_overlayEffects.size())
		{
			return nullptr;
		}
		return &s_overlayEffects[id].overlay;
	}

	void setOverlayImage(OverlayID id, const OverlayImage* image)
	{
		if (id >= s_overlayEffects.size())
		{
			return;
		}
		s_overlayEffects[id].overlay = *image;
	}

	void modifyOverlay(OverlayID id, f32* tint, f32 screenX, f32 screenY, f32 scale)
	{
		if (id >= s_overlayEffects.size())
		{
			return;
		}

		for (s32 i = 0; i < 4; i++) { s_overlayEffects[id].tint[i] = tint ? tint[i] : 1.0f; }
		s_overlayEffects[id].x = screenX;
		s_overlayEffects[id].y = screenY;
		s_overlayEffects[id].scale = scale;
	}

	void getOverlaySettings(OverlayID id, f32* tint, f32* screenX, f32* screenY, f32* scale)
	{
		if (id >= s_overlayEffects.size())
		{
			return;
		}
		if (tint)
		{
			for (s32 i = 0; i < 4; i++) { tint[i] = s_overlayEffects[id].tint[i]; }
		}
		if (screenX)
		{
			*screenX = s_overlayEffects[id].x;
		}
		if (screenY)
		{
			*screenY = s_overlayEffects[id].y;
		}
		if (scale)
		{
			*scale = s_overlayEffects[id].scale;
		}
	}

	void enableOverlay(OverlayID id, bool enable)
	{
		if (id >= s_overlayEffects.size())
		{
			return;
		}
		s_overlayEffects[id].active = enable;
	}

	void removeOverlay(OverlayID id)
	{
		if (id >= s_overlayEffects.size() || !s_overlayEffects[id].active)
		{
			return;
		}
		s_overlayEffects[id].active = false;
		s_freeOverlayEffects.push_back(id);
	}

	void removeAllOverlays()
	{
		s_overlayEffects.clear();
		s_freeOverlayEffects.clear();
	}

	void execute()
	{
		const size_t count = s_effects.size();
		if (!count) { return; }
		TFE_ZONE("Post Process");

		RenderTargetHandle curRenderTarget = nullptr;
		updateRenderTargetScale(curRenderTarget);

		s_vertexBuffer.bind();
		s_indexBuffer.bind();

		PostEffectInstance* effectInst = s_effects.data();
		for (size_t e = 0; e < count; e++, effectInst++)
		{
			PostProcessEffect* effect = effectInst->effect;
			if (!effect)
			{
				TFE_System::logWrite(LOG_ERROR, "PostFX", "Effect instance has NULL effect. Index = %u", e);
				continue;
			}

			Shader* shader = effect->m_shader;
			if (!shader)
			{
				// Defensive guard: an effect whose init() failed to load/compile
				// its shader(s) would otherwise leave m_shader uninitialized,
				// crashing here (and, on some GPU drivers, inside the driver
				// itself rather than with a clean access violation in TFE).
				TFE_System::logWrite(LOG_ERROR, "PostFX", "Effect instance has NULL shader. Index = %u", e);
				continue;
			}
			shader->bind();

			if (curRenderTarget != effectInst->output)
			{
				curRenderTarget = effectInst->output;
				updateRenderTargetScale(curRenderTarget);
			}
			setScaleOffset(shader, effect->m_scaleOffsetId, effectInst->x, effectInst->y, effectInst->width, effectInst->height);

			// Set effect output.
			if (effectInst->output)
				TFE_RenderBackend::bindRenderTarget(effectInst->output);
			else
				TFE_RenderBackend::unbindRenderTarget();

			// Bind inputs.
			for (u32 i = 0; i < effectInst->inputCount; i++)
			{
				if (!effectInst->inputs[i].ptr) { continue; }

				if (effectInst->inputs[i].type == PTYPE_TEXTURE)
				{
					effectInst->inputs[i].tex->bind(i);
					if (effectInst->forceLinearFilter) { effectInst->inputs[i].tex->setFilter(MAG_FILTER_LINEAR, MIN_FILTER_LINEAR); }
				}
				else if (effectInst->inputs[i].type == PTYPE_DYNAMIC_TEX)
				{
					effectInst->inputs[i].dyntex->bind(i);
				}
			}

			// Setup any effect specific state.
			effect->setEffectState();

			// Draw the rectangle.
			drawRectangle();

			// Cleanup
			for (u32 i = 0; i < effectInst->inputCount; i++)
			{
				if (!effectInst->inputs[i].ptr) { continue; }
				if (effectInst->inputs[i].type == PTYPE_TEXTURE && effectInst->forceLinearFilter)
				{
					effectInst->inputs[i].tex->bind(i);
					if (effectInst->forceLinearFilter) { effectInst->inputs[i].tex->setFilter(MAG_FILTER_NONE, MIN_FILTER_NONE); }
				}
				TextureGpu::clear(i);
			}
		}

		const size_t overlayCount = s_overlayEffects.size();
		OverlayEffect* effect = s_overlayEffects.data();

		Shader* shader = s_overlayEffect->m_shader;
		s_overlayEffect->setEffectState();
		shader->bind();

		for (size_t i = 0; i < overlayCount; i++, effect++)
		{
			if (!effect->active) { continue; }

			// Overlay Image.
			effect->overlay.texture->bind();
			
			// Scale Offset.
			f32 scale = effect->scale;
			f32 scaledWidth  = scale * f32(effect->overlay.overlayWidth)  * 2.0f * s_screenScale[0];
			f32 scaledHeight = scale * f32(effect->overlay.overlayHeight) * 2.0f * s_screenScale[1];
			f32 halfWidth  = scaledWidth * 0.5f;
			f32 halfHeight = scaledHeight * 0.5f;
			f32 x = 2.0f * f32(effect->x) - 1.0f - halfWidth;
			f32 y = 2.0f * f32(effect->y) - 1.0f - halfHeight;
			const f32 scaleOffset[] = { scaledWidth, scaledHeight, x, y };
			shader->setVariable(s_overlayEffect->m_scaleOffsetId, SVT_VEC4, scaleOffset);

			// Uv Scale Offset.
			f32 uScale = f32(effect->overlay.overlayWidth)  / f32(effect->overlay.texture->getWidth());
			f32 vScale = f32(effect->overlay.overlayHeight) / f32(effect->overlay.texture->getHeight());
			f32 uOffset = f32(effect->overlay.overlayX) / f32(effect->overlay.texture->getWidth());
			f32 vOffset = f32(effect->overlay.overlayY) / f32(effect->overlay.texture->getHeight());
			const f32 uvScaleOffset[] = { uScale, vScale, uOffset, vOffset };
			shader->setVariable(s_overlayEffect->m_uvOffsetSizeId, SVT_VEC4, uvScaleOffset);

			// Tint.
			shader->setVariable(s_overlayEffect->m_tintId, SVT_VEC4, effect->tint);

			drawRectangle();
		}

		TextureGpu::clear();
		Shader::unbind();
		s_vertexBuffer.unbind();
		s_indexBuffer.unbind();
		TFE_RenderBackend::unbindRenderTarget();
	}
	
	void drawRectangle()
	{
		TFE_RenderBackend::drawIndexedTriangles(2, sizeof(u16));
	}

	////////////////////////////////////////////////
	// Internal
	////////////////////////////////////////////////
	void updateRenderTargetScale(RenderTargetHandle renderTarget)
	{
		if (renderTarget)
		{
			u32 w, h;
			TFE_RenderBackend::getRenderTargetDim(renderTarget, &w, &h);
			s_screenScale[0] = 1.0f / f32(w);
			s_screenScale[1] = 1.0f / f32(h);
		}
		else
		{
			DisplayInfo display;
			TFE_RenderBackend::getDisplayInfo(&display);
			s_screenScale[0] = 1.0f / f32(display.width);
			s_screenScale[1] = 1.0f / f32(display.height);
		}
	}

	void setScaleOffset(Shader* shader, s32 variableId, s32 x, s32 y, s32 width, s32 height)
	{
		const f32 scaleX = 2.0f * f32(width)  * s_screenScale[0];
		const f32 scaleY = 2.0f * f32(height) * s_screenScale[1];
		const f32 offsetX = 2.0f * f32(x) * s_screenScale[0] - 1.0f;
		const f32 offsetY = 2.0f * f32(y) * s_screenScale[1] - 1.0f;
		const f32 scaleOffset[] = { scaleX, scaleY, offsetX, offsetY };
		shader->setVariable(variableId, SVT_VEC4, scaleOffset);
	}
}