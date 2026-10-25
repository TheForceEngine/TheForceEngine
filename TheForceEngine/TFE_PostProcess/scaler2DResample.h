#pragma once
//////////////////////////////////////////////////////////////////////
// 2D-Scaler (pass 1: edge-aware resample).
//
// GPU port of the "2D-Scaler" ReShade shader by guest(r) (2019),
// released under the GNU GPL v2 (or later). Original source:
// https://github.com/guestrr (ReShade shader repository "2D-Scaler.fx").
//
// This is pass 1 of 2 ("TWODS0" in the original .fx file). It resamples
// the already window-scaled game image with an edge-aware bilinear-like
// filter that softens the blocky look of nearest-neighbor upscaled pixel
// art, without fully blurring away sharp edges. Pass 2 (Scaler2DDeblur)
// then sharpens the result back up.
//
// Like Bloom, this only affects the game framebuffer post process chain
// and never TFE's own ImGui front-end or main menu (see
// TFE_RenderBackend::swap()).
//////////////////////////////////////////////////////////////////////

#include <TFE_System/types.h>
#include "postprocesseffect.h"

class Scaler2DResample : public PostProcessEffect
{
public:
	bool init() override;
	void destroy() override;
	void setEffectState() override;

	// 'Filter Width' parameter from the original shader (ui_min = 0.25, ui_max = 2.0).
	void setFilterWidth(f32 filterWidth) { m_filterWidth = filterWidth; }
	// Reciprocal of the *window* resolution this pass operates on (ReShade::PixelSize equivalent).
	void setPixelSize(f32 invWidth, f32 invHeight) { m_pixelSize[0] = invWidth; m_pixelSize[1] = invHeight; }

private:
	Shader m_shaderInternal;
	s32 m_pixelSizeId = -1;
	s32 m_filterWidthId = -1;
	f32 m_filterWidth = 0.75f;
	f32 m_pixelSize[2] = { 1.0f, 1.0f };

	void setupShader();
};
