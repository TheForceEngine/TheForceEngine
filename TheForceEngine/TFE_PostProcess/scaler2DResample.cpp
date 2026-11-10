#include "scaler2DResample.h"
#include "postprocess.h"
#include <TFE_RenderBackend/renderBackend.h>
#include <TFE_System/profiler.h>

bool Scaler2DResample::init()
{
	if (!m_shaderInternal.load("Shaders/scaler2D.vert", "Shaders/scaler2DResample.frag"))
	{
		return false;
	}
	m_shaderInternal.bindTextureNameToSlot("SourceImage", 0);
	m_pixelSizeId = m_shaderInternal.getVariableId("PixelSize");
	m_filterWidthId = m_shaderInternal.getVariableId("FilterWidth");
	setupShader();
	return true;
}

void Scaler2DResample::destroy()
{
	m_shaderInternal.destroy();
}

void Scaler2DResample::setEffectState()
{
	TFE_RenderState::setStateEnable(false, STATE_CULLING | STATE_BLEND | STATE_DEPTH_TEST);
	if (m_shader)
	{
		m_shader->setVariable(m_pixelSizeId, SVT_VEC2, m_pixelSize);
		m_shader->setVariable(m_filterWidthId, SVT_SCALAR, &m_filterWidth);
	}
}

void Scaler2DResample::setupShader()
{
	m_shader = &m_shaderInternal;
	m_scaleOffsetId = m_shader->getVariableId("ScaleOffset");
}
