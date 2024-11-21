#include <TFE_RenderBackend/renderBackend.h>
#include <TFE_RenderBackend/vertexBuffer.h>
#include "gl.h"
#include <memory.h>

static const GLenum c_glType[] =
{
	GL_FLOAT,			// ATYPE_FLOAT
	GL_UNSIGNED_INT,	// ATYPE_UINT32
	GL_INT,				// ATYPE_INT32
	GL_UNSIGNED_SHORT,	// ATYPE_UINT16
	GL_SHORT,			// ATYPE_INT16
	GL_UNSIGNED_BYTE,	// ATYPE_UINT8
	GL_BYTE,			// ATYPE_INT8
};

static const u32 c_glTypeSize[]=
{
	4, // ATYPE_FLOAT
	4, // ATYPE_UINT32
	4, // ATYPE_INT32
	2, // ATYPE_UINT16
	2, // ATYPE_INT16
	1, // ATYPE_UINT8
	1, // ATYPE_INT8
};

VertexBuffer::~VertexBuffer()
{
	destroy();
}

bool VertexBuffer::create(u32 count, u32 stride, u32 attrCount, const AttributeMapping* attrMapping, bool dynamic, void* initData)
{
	if (!attrMapping || !count || !stride || !attrCount) { return false; }

	// Track vertex buffer attributes.
	m_size = count * stride;
	m_count = count;
	m_stride = stride;
	m_attrCount = attrCount;
	m_dynamic = dynamic;

	// Copy the attribute mapping.
	m_attrMapping = new AttributeMapping[m_attrCount];
	if (!m_attrMapping) { return false; }
	memcpy(m_attrMapping, attrMapping, sizeof(AttributeMapping)*m_attrCount);

	// Fill in vertex attribute offsets.
	u32 offset = 0;
	for (u32 i = 0; i < m_attrCount; i++)
	{
		if (m_attrMapping[i].offset == 0) { m_attrMapping[i].offset = offset; }
									 else { offset = m_attrMapping[i].offset; }
		offset += c_glTypeSize[m_attrMapping[i].type] * m_attrMapping[i].channels;
	}

	// Build the GPU buffer and copy the initial data.
	glGenBuffers(1, &m_gpuHandle);
	glBindBuffer(GL_ARRAY_BUFFER, m_gpuHandle);
	glBufferData(GL_ARRAY_BUFFER, m_size, initData, dynamic ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	return true;
}

void VertexBuffer::destroy()
{
	if (m_gpuHandle) { glDeleteBuffers(1, &m_gpuHandle); }
	m_gpuHandle = 0;

	delete[] m_attrMapping;
	m_attrMapping = nullptr;
}

void VertexBuffer::update(const void* buffer, size_t size)
{
	glBindBuffer(GL_ARRAY_BUFFER, m_gpuHandle);
	glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)size, (const GLvoid*)buffer, m_dynamic ? GL_STREAM_DRAW : GL_STATIC_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void VertexBuffer::bind() const
{
	TFE_RenderBackend::bindGlobalVAO();	// for macOS GL

	glBindBuffer(GL_ARRAY_BUFFER, m_gpuHandle);
	for (u32 i = 0; i < m_attrCount; i++)
	{
		glEnableVertexAttribArray(m_attrMapping[i].id);
		glVertexAttribPointer(m_attrMapping[i].id, m_attrMapping[i].channels, c_glType[m_attrMapping[i].type], m_attrMapping[i].normalized, m_stride, (void*)(iptr)m_attrMapping[i].offset);
	}
}

void VertexBuffer::unbind() const
{
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	for (u32 i = 0; i < m_attrCount; i++)
	{
		glDisableVertexAttribArray(m_attrMapping[i].id);
	}
}
