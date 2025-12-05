#include "glslParser.h"
#include <TFE_RenderBackend/shader.h>
#include <TFE_RenderBackend/vertexBuffer.h>
#include <TFE_System/system.h>
#include <TFE_FileSystem/filestream.h>
#include <TFE_FileSystem/paths.h>
#include <TFE_RenderBackend/renderBackend.h>
#include "gl.h"
#include <assert.h>
#include <vector>
#include <string>
#include <SDL.h> 

namespace ShaderGL
{
	static const char* c_shaderAttrName[]=
	{
		"vtx_pos",   // ATTR_POS
		"vtx_nrm",   // ATTR_NRM
		"vtx_uv",    // ATTR_UV
		"vtx_uv1",   // ATTR_UV1
		"vtx_uv2",   // ATTR_UV2
		"vtx_uv3",   // ATTR_UV3
		"vtx_color", // ATTR_COLOR
	};

	static const s32 c_glslVersion[] = { 130, 330, 450 };
	static const GLchar* c_glslVersionString[] = { "#version 130\n", "#version 330\n", "#version 450\n" };
	static std::vector<char> s_buffers[2];
	static std::string s_defineString;
	static std::string s_vertexFile, s_fragmentFile;
}

bool Shader::create(const char* vertexShaderGLSL, const char* fragmentShaderGLSL, const char* defineString/* = nullptr*/, ShaderVersion version/* = SHADER_VER_COMPTABILE*/)
{
	// Create shaders
	m_shaderVersion = version;

	const GLchar* version_string;
	if (strcmp(SDL_GetPlatform(), "Mac OS X") == 0) {
		// Force GLSL version 410 for macOS
		version_string = "#version 410\n";
	} else {
		version_string = ShaderGL::c_glslVersionString[m_shaderVersion];
	}

	const GLchar *vertex_shader_with_version[3] = { version_string, defineString ? defineString : "", vertexShaderGLSL };
	u32 vertHandle = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(vertHandle, 3, vertex_shader_with_version, NULL);
	glCompileShader(vertHandle);

	GLint success = 0;
	glGetShaderiv(vertHandle, GL_COMPILE_STATUS, &success);
	if (!success)
	{
		GLchar infoLog[512];
		glGetShaderInfoLog(vertHandle, 512, NULL, infoLog);
		TFE_System::logWrite(LOG_ERROR, "Shader", "Vertex shader compilation failed:\n%s", infoLog);
		return false;
	}

	const GLchar *fragment_shader_with_version[3] = { version_string, defineString ? defineString : "", fragmentShaderGLSL };
	u32 fragHandle = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(fragHandle, 3, fragment_shader_with_version, NULL);
	glCompileShader(fragHandle);
	glGetShaderiv(fragHandle, GL_COMPILE_STATUS, &success);
	if (!success)
	{
		GLchar infoLog[512];
		glGetShaderInfoLog(fragHandle, 512, NULL, infoLog);
		TFE_System::logWrite(LOG_ERROR, "Shader", "Fragment shader compilation failed:\n%s", infoLog);
		return false;
	}

	m_gpuHandle = glCreateProgram();
	glAttachShader(m_gpuHandle, vertHandle);
	glAttachShader(m_gpuHandle, fragHandle);
	// Bind vertex attribute names to slots.
	for (u32 i = 0; i < ATTR_COUNT; i++)
	{
		glBindAttribLocation(m_gpuHandle, i, ShaderGL::c_shaderAttrName[i]);
	}

	glLinkProgram(m_gpuHandle);

	glGetProgramiv(m_gpuHandle, GL_LINK_STATUS, &success);
	if (!success)
	{
		GLchar infoLog[512];
		glGetProgramInfoLog(m_gpuHandle, 512, NULL, infoLog);
		TFE_System::logWrite(LOG_ERROR, "Shader", "Shader program linking failed:\n%s", infoLog);
		return false;
	}

	// Clean up shader objects
	glDeleteShader(vertHandle);
	glDeleteShader(fragHandle);

	return m_gpuHandle != 0;
}

bool Shader::load(const char* vertexShaderFile, const char* fragmentShaderFile, u32 defineCount/* = 0*/, ShaderDefine* defines/* = nullptr*/, ShaderVersion version/* = SHADER_VER_COMPTABILE*/)
{
	m_shaderVersion = version;
	ShaderGL::s_buffers[0].clear();
	ShaderGL::s_buffers[1].clear();

	GLSLParser::parseFile(vertexShaderFile,   ShaderGL::s_buffers[0]);
	GLSLParser::parseFile(fragmentShaderFile, ShaderGL::s_buffers[1]);

	ShaderGL::s_vertexFile = vertexShaderFile;
	ShaderGL::s_fragmentFile = fragmentShaderFile;

	ShaderGL::s_buffers[0].push_back(0);
	ShaderGL::s_buffers[1].push_back(0);

	// Build a string of defines.
	ShaderGL::s_defineString.clear();
	if (defineCount)
	{
		ShaderGL::s_defineString += "\r\n";
		for (u32 i = 0; i < defineCount; i++)
		{
			ShaderGL::s_defineString += "#define ";
			ShaderGL::s_defineString += defines[i].name;
			ShaderGL::s_defineString += " ";
			ShaderGL::s_defineString += defines[i].value;
			ShaderGL::s_defineString += "\r\n";
		}
		ShaderGL::s_defineString += "\r\n";
	}

	return create(ShaderGL::s_buffers[0].data(), ShaderGL::s_buffers[1].data(), ShaderGL::s_defineString.c_str(), m_shaderVersion);
}

void Shader::enableClipPlanes(s32 count)
{
	m_clipPlaneCount = count;
}

void Shader::destroy()
{
	if (m_gpuHandle)
	{
		glDeleteProgram(m_gpuHandle);
	}
	m_gpuHandle = 0;
}

void Shader::bind()
{
	TFE_RenderBackend::bindGlobalVAO();	// for macOS GL
	glUseProgram(m_gpuHandle);
	TFE_RenderState::enableClipPlanes(m_clipPlaneCount);
}

void Shader::unbind()
{
	glUseProgram(0);
}

s32 Shader::getVariableId(const char* name)
{
	return glGetUniformLocation(m_gpuHandle, name);
}

// For debugging.
s32 Shader::getVariables()
{
	s32 length;
	s32 size;
	GLenum type;
	char name[256];

	s32 count;
	glGetProgramiv(m_gpuHandle, GL_ACTIVE_UNIFORMS, &count);
	printf("Active Uniforms: %d\n", count);

	for (s32 i = 0; i < count; i++)
	{
		glGetActiveUniform(m_gpuHandle, (GLuint)i, 256, &length, &size, &type, name);
		printf("Uniform #%d Type: %u Name: %s\n", i, type, name);
	}

	s32 attribCount;
	glGetProgramiv(m_gpuHandle, GL_ACTIVE_ATTRIBUTES, &attribCount);
	printf("Active Attributes: %d\n", attribCount);

	for (s32 i = 0; i < attribCount; i++)
	{
		glGetActiveAttrib(m_gpuHandle, (GLuint)i, 256, &length, &size, &type, name);
		printf("Attribute #%d Type: %u Name: %s\n", i, type, name);
	}

	return count;
}

void Shader::bindTextureNameToSlot(const char* texName, s32 slot)
{
	const s32 curSlot = glGetUniformLocation(m_gpuHandle, texName);
	if (curSlot < 0 || slot < 0) { return; }

	bind();
	glUniform1i(curSlot, slot);
	unbind();
}

void Shader::setVariable(s32 id, ShaderVariableType type, const f32* data)
{
	if (id < 0) { return; }

	switch (type)
	{
	case SVT_SCALAR:
		glUniform1f(id, data[0]);
		break;
	case SVT_VEC2:
		glUniform2fv(id, 1, data);
		break;
	case SVT_VEC3:
		glUniform3fv(id, 1, data);
		break;
	case SVT_VEC4:
		glUniform4fv(id, 1, data);
		break;
	case SVT_MAT3x3:
		glUniformMatrix3fv(id, 1, false, data);
		break;
	case SVT_MAT4x3:
		glUniformMatrix4x3fv(id, 1, false, data);
		break;
	case SVT_MAT4x4:
		glUniformMatrix4fv(id, 1, false, data);
		break;
	default:
		TFE_System::logWrite(LOG_ERROR, "Shader", "Mismatched parameter type.");
		assert(0);
	}
}

void Shader::setVariableArray(s32 id, ShaderVariableType type, const f32* data, u32 count)
{
	if (id < 0) { return; }

	switch (type)
	{
	case SVT_SCALAR:
		glUniform1fv(id, count, data);
		break;
	case SVT_VEC2:
		glUniform2fv(id, count, data);
		break;
	case SVT_VEC3:
		glUniform3fv(id, count, data);
		break;
	case SVT_VEC4:
		glUniform4fv(id, count, data);
		break;
	case SVT_MAT3x3:
		glUniformMatrix3fv(id, count, false, data);
		break;
	case SVT_MAT4x3:
		glUniformMatrix4x3fv(id, count, false, data);
		break;
	case SVT_MAT4x4:
		glUniformMatrix4fv(id, count, false, data);
		break;
	default:
		TFE_System::logWrite(LOG_ERROR, "Shader", "Mismatched parameter type.");
		assert(0);
	}
}

void Shader::setVariable(s32 id, ShaderVariableType type, const s32* data)
{
	if (id < 0) { return; }

	switch (type)
	{
	case SVT_ISCALAR:
		glUniform1i(id, *(&data[0]));
		break;
	case SVT_IVEC2:
		glUniform2iv(id, 1, data);
		break;
	case SVT_IVEC3:
		glUniform3iv(id, 1, data);
		break;
	case SVT_IVEC4:
		glUniform4iv(id, 1, data);
		break;
	default:
		TFE_System::logWrite(LOG_ERROR, "Shader", "Mismatched parameter type.");
		assert(0);
	}
}

void Shader::setVariable(s32 id, ShaderVariableType type, const u32* data)
{
	if (id < 0) { return; }

	switch (type)
	{
	case SVT_USCALAR:
		glUniform1ui(id, *(&data[0]));
		break;
	case SVT_UVEC2:
		glUniform2uiv(id, 1, data);
		break;
	case SVT_UVEC3:
		glUniform3uiv(id, 1, data);
		break;
	case SVT_UVEC4:
		glUniform4uiv(id, 1, data);
		break;
	default:
		TFE_System::logWrite(LOG_ERROR, "Shader", "Mismatched parameter type.");
		assert(0);
	}
}
