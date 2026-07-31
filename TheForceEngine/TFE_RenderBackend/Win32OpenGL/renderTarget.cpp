#include "renderTarget.h"
#include <TFE_RenderBackend/renderState.h>
#include <TFE_RenderBackend/renderBackend.h>
#include "gl.h"
#include <assert.h>
#include <algorithm>

namespace
{
	u32 createDepthBuffer(u32 width, u32 height)
	{
		u32 handle = 0;
		glGenRenderbuffers(1, &handle);
		glBindRenderbuffer(GL_RENDERBUFFER, handle);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
		glBindRenderbuffer(GL_RENDERBUFFER, 0);

		return handle;
	}
}

RenderTarget::~RenderTarget()
{
	glDeleteFramebuffers(1, &m_gpuHandle);
	m_gpuHandle = 0;

	if (m_depthBufferHandle)
	{
		glDeleteRenderbuffers(1, &m_depthBufferHandle);
		m_depthBufferHandle = 0;
	}
}

bool RenderTarget::create(s32 textureCount, TextureGpu** textures, bool depthBuffer)
{
	if (textureCount < 1 || !textures || !textures[0]) { return false; }
	m_textureCount = textureCount;
	for (u32 i = 0; i < m_textureCount; i++)
	{
		m_texture[i] = textures[i];
	}

	glGenFramebuffers(1, &m_gpuHandle);
	glBindFramebuffer(GL_FRAMEBUFFER, m_gpuHandle);
	for (u32 i = 0; i < m_textureCount; i++)
	{
		glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, m_texture[i]->getHandle(), 0);
	}

	m_depthBufferHandle = 0;
	if (depthBuffer)
	{
		m_depthBufferHandle = createDepthBuffer(m_texture[0]->getWidth(), m_texture[0]->getHeight());
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, m_depthBufferHandle);
	}

	// Set the list of draw buffers.
	GLenum drawBuffers[MAX_ATTACHMENT] = { 0 };
	for (u32 i = 0; i < m_textureCount; i++)
	{
		drawBuffers[i] = GL_COLOR_ATTACHMENT0 + i;
	}
	glDrawBuffers(m_textureCount, drawBuffers);

	// check FBO status
	GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	assert(status == GL_FRAMEBUFFER_COMPLETE);
	if (status != GL_FRAMEBUFFER_COMPLETE)
	{
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		return false;
	}

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	return true;
}

void RenderTarget::bind()
{
	glBindFramebuffer(GL_FRAMEBUFFER, m_gpuHandle);
	glViewport(0, 0, m_texture[0]->getWidth(), m_texture[0]->getHeight());
	glDepthRange(0.0f, 1.0f);
}

void RenderTarget::clear(const f32* color, f32 depth, u8 stencil, bool clearColor)
{
	if (color)
		glClearColor(color[0], color[1], color[2], color[3]);
	else
		glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

	u32 clearFlags = clearColor ? GL_COLOR_BUFFER_BIT : 0;
	if (m_depthBufferHandle)
	{
		clearFlags |= GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT;
		TFE_RenderState::setStateEnable(true, STATE_DEPTH_WRITE | STATE_STENCIL_WRITE);
		glClearDepth(depth);
		glClearStencil(stencil);
	}

	glClear(clearFlags);
}

 void RenderTarget::clearDepth(f32 depth)
 {
	 if (m_depthBufferHandle)
	 {
		 TFE_RenderState::setStateEnable(true, STATE_DEPTH_WRITE);
		 glClearDepth(depth);
		 glClear(GL_DEPTH_BUFFER_BIT);
	 }
 }

 void RenderTarget::clearStencil(u8 stencil)
 {
	 if (m_depthBufferHandle)
	 {
		 TFE_RenderState::setStateEnable(true, STATE_STENCIL_WRITE);
		 glClearStencil(stencil);
		 glClear(GL_STENCIL_BUFFER_BIT);
	 }
 }

void RenderTarget::unbind()
{
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void RenderTarget::copy(RenderTarget* dst, RenderTarget* src, MagFilter filter)
{
	glBindFramebuffer(GL_READ_FRAMEBUFFER, src->m_gpuHandle);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dst->m_gpuHandle);

	const GLenum glFilter = filter == MAG_FILTER_LINEAR ? GL_LINEAR : GL_NEAREST;
	const u32 count = std::min(src->m_textureCount, dst->m_textureCount);
	for (u32 i = 0; i < count; i++)
	{
		glReadBuffer(GL_COLOR_ATTACHMENT0 + i);
		glDrawBuffer(GL_COLOR_ATTACHMENT0 + i);
		glBlitFramebuffer(0, 0, src->getTexture(i)->getWidth(), src->getTexture(i)->getHeight(),	// src rect
						  0, 0, dst->getTexture(i)->getWidth(), dst->getTexture(i)->getHeight(),	// dst rect
						  GL_COLOR_BUFFER_BIT, glFilter);
	}

	GLenum drawBuffers[MAX_ATTACHMENT] = { 0 };
	for (u32 i = 0; i < dst->m_textureCount; i++)
	{
		drawBuffers[i] = GL_COLOR_ATTACHMENT0 + i;
	}
	glDrawBuffers(dst->m_textureCount, drawBuffers);

	glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
	glReadBuffer(GL_BACK);
	glDrawBuffer(GL_BACK);
}

void RenderTarget::copyBackbufferToTarget(RenderTarget* dst)
{
	glReadBuffer(GL_BACK);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dst->m_gpuHandle);

	DisplayInfo displayInfo;
	TFE_RenderBackend::getDisplayInfo(&displayInfo);
	
	glBlitFramebuffer(0, displayInfo.height, displayInfo.width, 0,	// src rect
		0, 0, dst->getTexture()->getWidth(), dst->getTexture()->getHeight(),	// dst rect
		GL_COLOR_BUFFER_BIT, GL_NEAREST);

	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
}
