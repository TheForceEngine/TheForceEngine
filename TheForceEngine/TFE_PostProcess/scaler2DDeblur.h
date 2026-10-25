#pragma once
//////////////////////////////////////////////////////////////////////
// 2D-Scaler (pass 2: edge-aware deblur/sharpen).
//
// GPU port of the "2D-Scaler" ReShade shader by guest(r) (2019),
// released under the GNU GPL v2 (or later). Original source:
// https://github.com/guestrr (ReShade shader repository "2D-Scaler.fx").
//
// This is pass 2 of 2 ("DEB" in the original .fx file). It reads the
// output of Scaler2DResample and sharpens it back up in an edge-aware
// way (a local min/max clamp driven deblur), counteracting the softening
// introduced by pass 1 while avoiding ringing on flat regions.
//
// Like Bloom, this only affects the game framebuffer post process chain
// and never TFE's own ImGui front-end or main menu (see
// TFE_RenderBackend::swap()).
//////////////////////////////////////////////////////////////////////

#include <TFE_System/types.h>
#include "postprocesseffect.h"

class Scaler2DDeblur : public PostProcessEffect
{
public:
	bool init() override;
	void destroy() override;
	void setEffectState() override;

	// 'Filter Width' parameter from the original shader (ui_min = 0.25, ui_max = 2.0).
	void setFilterWidth(f32 filterWidth) { m_filterWidth = filterWidth; }
	// 'Deblur' parameter from the original shader (ui_min = 1.0, ui_max = 5.0).
	void setDeblur(f32 deblur) { m_deblur = deblur; }
	// Reciprocal of the *window* resolution this pass operates on (ReShade::PixelSize equivalent).
	void setPixelSize(f32 invWidth, f32 invHeight) { m_pixelSize[0] = invWidth; m_pixelSize[1] = invHeight; }

private:
	Shader m_shaderInternal;
	s32 m_pixelSizeId = -1;
	s32 m_filterWidthId = -1;
	s32 m_deblurId = -1;
	f32 m_filterWidth = 0.75f;
	f32 m_deblur = 3.0f;
	f32 m_pixelSize[2] = { 1.0f, 1.0f };

	void setupShader();
};
