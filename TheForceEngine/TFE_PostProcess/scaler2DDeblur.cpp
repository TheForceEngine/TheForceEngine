#include "scaler2DDeblur.h"
#include "postprocess.h"
#include <TFE_RenderBackend/renderBackend.h>
#include <TFE_System/profiler.h>

bool Scaler2DDeblur::init()
{
	if (!m_shaderInternal.load("Shaders/scaler2D.vert", "Shaders/scaler2DDeblur.frag"))
	{
		return false;
	}
	m_shaderInternal.bindTextureNameToSlot("SourceImage", 0);
	m_pixelSizeId = m_shaderInternal.getVariableId("PixelSize");
	m_filterWidthId = m_shaderInternal.getVariableId("FilterWidth");
	m_deblurId = m_shaderInternal.getVariableId("Deblur");
	setupShader();
	return true;
}

void Scaler2DDeblur::destroy()
{
	m_shaderInternal.destroy();
}

void Scaler2DDeblur::setEffectState()
{
	TFE_RenderState::setStateEnable(false, STATE_CULLING | STATE_BLEND | STATE_DEPTH_TEST);
	if (m_shader)
	{
		m_shader->setVariable(m_pixelSizeId, SVT_VEC2, m_pixelSize);
		m_shader->setVariable(m_filterWidthId, SVT_SCALAR, &m_filterWidth);
		m_shader->setVariable(m_deblurId, SVT_SCALAR, &m_deblur);
	}
}

void Scaler2DDeblur::setupShader()
{
	m_shader = &m_shaderInternal;
	m_scaleOffsetId = m_shader->getVariableId("ScaleOffset");
}
