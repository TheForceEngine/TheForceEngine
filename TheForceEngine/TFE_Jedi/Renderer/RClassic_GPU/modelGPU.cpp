#include <cstring>

#include <TFE_System/profiler.h>
#include <TFE_System/math.h>
#include <TFE_Asset/modelAsset_jedi.h>
#include <TFE_Game/igame.h>
#include <TFE_Jedi/Level/level.h>
#include <TFE_Jedi/Level/rsector.h>
#include <TFE_Jedi/Level/robject.h>
#include <TFE_Jedi/Level/rtexture.h>
#include <TFE_Jedi/Math/fixedPoint.h>
#include <TFE_Jedi/Math/core_math.h>
#include <TFE_Jedi/Renderer/jediRenderer.h>

#include <TFE_Input/input.h>

#include <TFE_RenderBackend/renderBackend.h>
#include <TFE_RenderBackend/vertexBuffer.h>
#include <TFE_RenderBackend/indexBuffer.h>
#include <TFE_RenderBackend/shader.h>
#include <TFE_RenderBackend/shaderBuffer.h>

#include <TFE_Settings/settings.h>

#include "modelGPU.h"
#include "frustum.h"
#include "sectorDisplayList.h"
#include "../rcommon.h"

#include <algorithm>
#include <map>
#include <vector>

using namespace TFE_RenderBackend;

namespace TFE_Jedi
{
	extern s32 s_drawnObjCount;
	extern SecObject* s_drawnObj[];
	extern u32 s_textureSettings;

	enum ModelShader
	{
		MGPU_SHADER_SOLID = 0,
		MGPU_SHADER_HOLOGRAM,
		MGPU_SHADER_TRANS,
		MGPU_SHADER_COUNT
	};

	struct ModelVertex
	{
		Vec3f pos;
		Vec3f nrm;
		Vec2f uv;
		u32 color;
	};

	struct ModelGPU
	{
		ModelShader shader;
		s32 indexStart;
		s32 polyCount;
	};

	struct ModelDraw
	{
		Vec3f posWS;
		Vec2f lightData;
		Vec4f textureOffsets;
		f32 transform[9];
		u32 portalInfo;
		void* modelId;
		void* obj;
	};

	struct ModelShaderSettings
	{
		bool colormapInterp = false;
		bool trueColor = false;
		bool ditheredBilinear = false;
		bool bloom = false;
	};
	
	static const AttributeMapping c_modelAttrMapping[] =
	{
		{ATTR_POS,   ATYPE_FLOAT, 3, 0, false},
		{ATTR_NRM,   ATYPE_FLOAT, 3, 0, false},
		{ATTR_UV,    ATYPE_FLOAT, 2, 0, false},
		{ATTR_COLOR, ATYPE_UINT8, 4, 0, true},
	};
	static const u32 c_modelAttrCount = TFE_ARRAYSIZE(c_modelAttrMapping);

	static Shader s_modelShaders[MGPU_SHADER_COUNT];
	static VertexBuffer s_modelVertexBuffer;	// vertex buffer contains vertices for all loaded models.
	static IndexBuffer  s_modelIndexBuffer;		// index buffer for all loaded models.

	static std::vector<ModelVertex> s_vertexData;
	static std::vector<u32> s_indexData;

	static s32 s_3doRendered = 0;
	static s32 s_3doPolygons = 0;

	static ModelShaderSettings s_shaderSettings = {};

	struct ShaderInputsMGPU
	{
		s32 cameraPosId;
		s32 cameraViewId;
		s32 cameraProjId;
		s32 cameraDirId;
		s32 lightDataId;
		s32 textureOffsetId;
		s32 modelMtxId;
		s32 modelPosId;
		s32 cameraRightId;
		s32 portalInfo;
		s32 texSamplingParamId;
		s32 palFxLumMask;
		s32 palFxFlash;
		s32 textureSettings;
	};
	static ShaderInputsMGPU s_shaderInputs[MGPU_SHADER_COUNT];
	static std::vector<ModelDraw> s_modelDrawList[MGPU_SHADER_COUNT];

	extern Mat3  s_cameraMtx;
	extern Mat4  s_cameraProj;
	extern Vec3f s_cameraPos;
	extern Vec3f s_cameraDir;
	extern Vec3f s_cameraRight;
	
	const char* c_vertexShaders[MGPU_SHADER_COUNT] = 
	{
		"Shaders/gpu_render_modelSolid.vert",
		"Shaders/gpu_render_modelHologram.vert",
		"Shaders/gpu_render_modelSolid.vert",
	};
	const char* c_fragmentShaders[MGPU_SHADER_COUNT] =
	{
		"Shaders/gpu_render_modelSolid.frag",
		"Shaders/gpu_render_modelHologram.frag",
		"Shaders/gpu_render_modelSolid.frag",
	};

	struct CompositeVertex
	{
		u32  index;
		vec3 pos;
		vec2 uv;
		vec3 nrml;
		s32 textureId;
		u8 color;
		u8 planeMode;
	};

	typedef std::map<u32, std::vector<u32>> ModelVertexMap;
	typedef std::vector<CompositeVertex> CompositeVertexList;

	// Building state.
	struct ModelBuildCtx
	{
		JediModel *model;
		s32 *curIndexStart;
		s32 *curVertexStart;
		bool modelTrans;
		ModelVertexMap modelVertexMap;
		CompositeVertexList modelVertexList;
	};

	bool model_updateShaders(bool initialize);

	static ModelGPU *newModelGPU(void)
	{
		ModelGPU* mgpu = (ModelGPU *)malloc(sizeof(ModelGPU));
		if (!mgpu) { return nullptr; }
		memset(mgpu, 0, sizeof(ModelGPU));
		return mgpu;
	}

	static bool model_buildShaderVariant(ModelShader variant, s32 defineCount, ShaderDefine* defines)
	{
		Shader* shader = &s_modelShaders[variant];
		if (!shader->load(c_vertexShaders[variant], c_fragmentShaders[variant], defineCount, defines, SHADER_VER_STD))
		{
			return false;
		}
		shader->enableClipPlanes(MAX_PORTAL_PLANES);

		s_shaderInputs[variant].cameraPosId   = shader->getVariableId("CameraPos");
		s_shaderInputs[variant].cameraViewId  = shader->getVariableId("CameraView");
		s_shaderInputs[variant].cameraProjId  = shader->getVariableId("CameraProj");
		s_shaderInputs[variant].cameraDirId   = shader->getVariableId("CameraDir");
		s_shaderInputs[variant].cameraRightId = shader->getVariableId("CameraRight");
		s_shaderInputs[variant].modelMtxId    = shader->getVariableId("ModelMtx");
		s_shaderInputs[variant].modelPosId    = shader->getVariableId("ModelPos");
		s_shaderInputs[variant].lightDataId   = shader->getVariableId("LightData");
		s_shaderInputs[variant].textureOffsetId = shader->getVariableId("TextureOffsets");
		s_shaderInputs[variant].portalInfo    = shader->getVariableId("PortalInfo");
		s_shaderInputs[variant].texSamplingParamId = shader->getVariableId("TexSamplingParam");
		s_shaderInputs[variant].palFxLumMask = shader->getVariableId("PalFxLumMask");
		s_shaderInputs[variant].palFxFlash   = shader->getVariableId("PalFxFlash");
		s_shaderInputs[variant].textureSettings = shader->getVariableId("TextureSettings");
		
		shader->bindTextureNameToSlot("Palette",  0);
		shader->bindTextureNameToSlot("Colormap", 1);
		shader->bindTextureNameToSlot("Textures", 2);
		shader->bindTextureNameToSlot("TextureTable",   3);
		shader->bindTextureNameToSlot("DrawListPlanes", 4);
		shader->bindTextureNameToSlot("BasePalette",    5);
		return true;
	}

	bool model_init()
	{
		bool result = model_updateShaders(true);
		TFE_COUNTER(s_3doRendered, "3DO Objects Rendered");
		TFE_COUNTER(s_3doPolygons, "3DO Polygons Rendered");
		return result;
	}

	void model_destroy()
	{
		for (s32 i = 0; i < MGPU_SHADER_COUNT; i++)
		{
			s_modelShaders[i].destroy();
		}
	}
		
	bool model_updateShaders(bool initialize)
	{
		TFE_Settings_Graphics* graphics = TFE_Settings::getGraphicsSettings();
		bool needsUpdate = initialize ||
			s_shaderSettings.ditheredBilinear != graphics->ditheredBilinear ||
			s_shaderSettings.bloom != graphics->bloomEnabled || 
			s_shaderSettings.colormapInterp != (graphics->colorMode == COLORMODE_8BIT_INTERP) ||
			s_shaderSettings.trueColor != (graphics->colorMode == COLORMODE_TRUE_COLOR);
		if (!needsUpdate) { return true; }

		// Then update the settings.
		s_shaderSettings.ditheredBilinear = graphics->ditheredBilinear;
		s_shaderSettings.bloom = graphics->bloomEnabled;
		s_shaderSettings.colormapInterp = (graphics->colorMode == COLORMODE_8BIT_INTERP);
		s_shaderSettings.trueColor = (graphics->colorMode == COLORMODE_TRUE_COLOR);

		ShaderDefine defines[16] = {};

		s32 defineCount = 0;
		if (s_shaderSettings.ditheredBilinear)
		{
			defines[defineCount].name = "OPT_BILINEAR_DITHER";
			defines[defineCount].value = "1";
			defineCount++;
		}
		if (s_shaderSettings.bloom)
		{
			defines[defineCount].name = "OPT_BLOOM";
			defines[defineCount].value = "1";
			defineCount++;
		}
		if (s_shaderSettings.colormapInterp || s_shaderSettings.trueColor)
		{
			defines[defineCount].name = "OPT_COLORMAP_INTERP";
			defines[defineCount].value = "1";
			defineCount++;

			defines[defineCount].name = "OPT_SMOOTH_LIGHTRAMP";
			defines[defineCount].value = "1";
			defineCount++;
		}
		if (s_shaderSettings.trueColor)
		{
			defines[defineCount].name = "OPT_TRUE_COLOR";
			defines[defineCount].value = "1";
			defineCount++;
		}

		bool result = true;
		for (s32 i = 0; i < MGPU_SHADER_COUNT - 1; i++)
		{
			result = result && model_buildShaderVariant(ModelShader(i), defineCount, defines);
		}

		defines[defineCount].name = "MODEL_TRANSPARENT_PASS";
		defines[defineCount].value = "1";
		defineCount++;

		result = result && model_buildShaderVariant(MGPU_SHADER_TRANS, defineCount, defines);
		return result;
	}

	static bool buildModelDrawVertices(JediModel* model, s32* indexStart, s32* vertexStart)
	{
		// In this version, we render one quad per vertex.
		// Store store 4 vertices per quad, but store corner in uv.
		u32 vcount = 4 * model->vertexCount;
		u32 icount = 6 * model->vertexCount;
		u32 vidx = *vertexStart;
		(*vertexStart) += vcount;
		(*indexStart)  += icount;

		const size_t curVtxSize = s_vertexData.size();
		const size_t curIdxSize = s_indexData.size();
		s_vertexData.resize(curVtxSize + vcount);
		s_indexData.resize( curIdxSize + icount);

		const vec3* srcVtx = model->vertices;
		const u32 color = model->polygons[0].color;
		const Vec3f nrm = { 0 };

		ModelVertex* outVtx = s_vertexData.data() + curVtxSize;
		u32* outIdx = s_indexData.data() + curIdxSize;
		for (s32 v = 0; v < model->vertexCount; v++, outVtx += 4, vidx += 4, outIdx += 6)
		{
			Vec3f pos = { fixed16ToFloat(srcVtx[v].x), fixed16ToFloat(srcVtx[v].y), fixed16ToFloat(srcVtx[v].z) };
			
			// Build a quad.
			outVtx[0].pos = pos;
			outVtx[0].nrm = nrm;
			outVtx[0].uv = { 0.0f, 1.0f };
			outVtx[0].color = color;

			outVtx[1].pos = pos;
			outVtx[1].nrm = nrm;
			outVtx[1].uv = { 1.0f, 1.0f };
			outVtx[1].color = color;

			outVtx[2].pos = pos;
			outVtx[2].nrm = nrm;
			outVtx[2].uv = { 1.0f, 0.0f };
			outVtx[2].color = color;

			outVtx[3].pos = pos;
			outVtx[3].nrm = nrm;
			outVtx[3].uv = { 0.0f, 0.0f };
			outVtx[3].color = color;

			// Indices.
			outIdx[0] = 0 + vidx;
			outIdx[1] = 1 + vidx;
			outIdx[2] = 2 + vidx;

			outIdx[3] = 0 + vidx;
			outIdx[4] = 2 + vidx;
			outIdx[5] = 3 + vidx;
		}

		ModelGPU* mgpu = newModelGPU();
		if (!mgpu) { return false; }

		mgpu->indexStart = (s32)curIdxSize;
		mgpu->polyCount = model->vertexCount * 2;
		mgpu->shader = MGPU_SHADER_HOLOGRAM;
		model->drawId = (void *)mgpu;

		return true;
	}
		
	static void startModel(struct ModelBuildCtx *ctx, JediModel* model, s32* indexStart, s32* vertexStart)
	{
		ctx->model = model;
		ctx->curIndexStart = indexStart;
		ctx->curVertexStart = vertexStart;
		ctx->modelTrans = false;
		ctx->modelVertexMap.clear();
		ctx->modelVertexList.clear();
	}

	static void endModel(struct ModelBuildCtx *ctx)
	{
		// Create the entry.
		ModelGPU* mgpu = newModelGPU();
		if (!mgpu) { return; } 

		mgpu->indexStart = *(ctx->curIndexStart);
		mgpu->polyCount = ((s32)s_indexData.size() - (*(ctx->curIndexStart))) / 3;
		mgpu->shader = ctx->modelTrans ? MGPU_SHADER_TRANS : MGPU_SHADER_SOLID;
		ctx->model->drawId = (void *)mgpu;

		// Add vertices.
		const u32 vtxCount = (u32)ctx->modelVertexList.size();
		const CompositeVertex* srcVtx = ctx->modelVertexList.data();

		s_vertexData.resize((*(ctx->curVertexStart)) + vtxCount);
		ModelVertex* outVtx = s_vertexData.data() + (*(ctx->curVertexStart));
		for (u32 v = 0; v < vtxCount; v++, outVtx++, srcVtx++)
		{
			outVtx->pos.x = fixed16ToFloat(srcVtx->pos.x);
			outVtx->pos.y = fixed16ToFloat(srcVtx->pos.y);
			outVtx->pos.z = fixed16ToFloat(srcVtx->pos.z);

			outVtx->nrm.x = fixed16ToFloat(srcVtx->nrml.x);
			outVtx->nrm.y = fixed16ToFloat(srcVtx->nrml.y);
			outVtx->nrm.z = fixed16ToFloat(srcVtx->nrml.z);

			outVtx->uv.x = fixed16ToFloat(srcVtx->uv.x);
			outVtx->uv.z = fixed16ToFloat(srcVtx->uv.y);

			u8* outColor = (u8*)&outVtx->color;
			outColor[0] = srcVtx->color;
			outColor[1] = srcVtx->textureId >= 0 ? srcVtx->textureId & 0xff : 0xff;
			outColor[2] = srcVtx->textureId >= 0 ? (srcVtx->textureId >> 8) & 0xff : 0xff;
			outColor[3] = srcVtx->planeMode ? 0xff : 0x00;
		}

		*(ctx->curIndexStart)  = (s32)s_indexData.size();
		*(ctx->curVertexStart) = (s32)s_vertexData.size();
	}

	static u32 getVertexKey(vec3* pos)
	{
		return u32(floor16(pos->x) + floor16(pos->y)*256 + floor16(pos->z)*65536);
	}

	static bool isCompositeVtxEqual(const CompositeVertex* srcVtx, vec3* pos, vec2* uv, vec3* nrml, u8 color, u8 planeMode, s32 textureId)
	{
		if (srcVtx->pos.x  != pos->x  || srcVtx->pos.y  != pos->y  || srcVtx->pos.z  != pos->z)  { return false; }
		if (srcVtx->nrml.x != nrml->x || srcVtx->nrml.y != nrml->y || srcVtx->nrml.z != nrml->z) { return false; }
		if (srcVtx->uv.x   != uv->x   || srcVtx->uv.y   != uv->y   || srcVtx->textureId != textureId) { return false; }
		return srcVtx->color == color && srcVtx->planeMode == planeMode;
	}

	static u32 getVertex(struct ModelBuildCtx *ctx, vec3* pos, vec2* uv, vec3* nrml, u8 color, u8 planeMode, s32 textureId)
	{
		// If the vertex already exists, then return it.
		const u32 key = getVertexKey(pos);
		bool keyFound = false;
		ModelVertexMap::iterator iBucket = ctx->modelVertexMap.find(key);
		if (iBucket != ctx->modelVertexMap.end())
		{
			keyFound = true;

			std::vector<u32>& vtxList = iBucket->second;
			const size_t count = vtxList.size();
			const u32* listIndices = vtxList.data();
			const CompositeVertex* listVtx = ctx->modelVertexList.data();
			for (size_t i = 0; i < count; i++)
			{
				const CompositeVertex* vtx = &listVtx[listIndices[i]];
				if (isCompositeVtxEqual(vtx, pos, uv, nrml, color, planeMode, textureId))
				{
					return vtx->index;
				}
			}
		}

		// Otherwise we need to add it.
		const u32 newId = (u32)ctx->modelVertexList.size();

		CompositeVertex newVtx;
		newVtx.pos   = *pos;
		newVtx.uv    = *uv;
		newVtx.nrml  = *nrml;
		newVtx.color = color;
		newVtx.planeMode = planeMode;
		newVtx.textureId = textureId;
		newVtx.index = newId;

		if (!keyFound)
		{
			std::vector<u32> newList;
			newList.push_back(newId);
			ctx->modelVertexMap[key] = newList;
		}
		else
		{
			ctx->modelVertexMap[key].push_back(newId);
		}
		ctx->modelVertexList.push_back(newVtx);

		return newId;
	}

	static void addFlatTriangle(ModelBuildCtx *ctx, s32* indices, u8 color, vec2* uv, vec3* nrml, s32 textureId)
	{
		vec3* v0 = &ctx->model->vertices[indices[0]];
		vec3* v1 = &ctx->model->vertices[indices[1]];
		vec3* v2 = &ctx->model->vertices[indices[2]];

		vec2 zero[3] = { 0 };
		vec2* srcUV = (uv && textureId >= 0) ? uv : zero;
		vec3 nrmDir = { nrml->x - v1->x, nrml->y - v1->y, nrml->z - v1->z };

		const u8 planeMode = 0;
		const s32 vertexStart = *(ctx->curVertexStart);
		s_indexData.push_back(getVertex(ctx, v0, &srcUV[0], &nrmDir, color, planeMode, textureId) + vertexStart);
		s_indexData.push_back(getVertex(ctx, v1, &srcUV[1], &nrmDir, color, planeMode, textureId) + vertexStart);
		s_indexData.push_back(getVertex(ctx, v2, &srcUV[2], &nrmDir, color, planeMode, textureId) + vertexStart);
	}

	static void addFlatQuad(ModelBuildCtx *ctx, s32* indices, u8 color, vec2* uv, vec3* nrml, s32 textureId)
	{
		vec3* v0 = &ctx->model->vertices[indices[0]];
		vec3* v1 = &ctx->model->vertices[indices[1]];
		vec3* v2 = &ctx->model->vertices[indices[2]];
		vec3* v3 = &ctx->model->vertices[indices[3]];
		vec3 nrmDir = { nrml->x - v1->x, nrml->y - v1->y, nrml->z - v1->z };

		vec2 zero[4] = { 0 };
		vec2* srcUV = (uv && textureId >= 0) ? uv : zero;
		const u8 planeMode = 0;

		u32 outIndices[4];
		outIndices[0] = getVertex(ctx, v0, &srcUV[0], &nrmDir, color, planeMode, textureId);
		outIndices[1] = getVertex(ctx, v1, &srcUV[1], &nrmDir, color, planeMode, textureId);
		outIndices[2] = getVertex(ctx, v2, &srcUV[2], &nrmDir, color, planeMode, textureId);
		outIndices[3] = getVertex(ctx, v3, &srcUV[3], &nrmDir, color, planeMode, textureId);

		const s32 vertexStart = *(ctx->curVertexStart);
		s_indexData.push_back(outIndices[0] + vertexStart);
		s_indexData.push_back(outIndices[1] + vertexStart);
		s_indexData.push_back(outIndices[2] + vertexStart);

		s_indexData.push_back(outIndices[0] + vertexStart);
		s_indexData.push_back(outIndices[2] + vertexStart);
		s_indexData.push_back(outIndices[3] + vertexStart);
	}

	static void addSmoothTriangle(ModelBuildCtx *ctx, s32* indices, u8 color, vec2* uv, s32 textureId)
	{
		vec3* v0 = &ctx->model->vertices[indices[0]];
		vec3* v1 = &ctx->model->vertices[indices[1]];
		vec3* v2 = &ctx->model->vertices[indices[2]];

		vec3* n0 = &ctx->model->vertexNormals[indices[0]];
		vec3* n1 = &ctx->model->vertexNormals[indices[1]];
		vec3* n2 = &ctx->model->vertexNormals[indices[2]];

		vec3 nDir0 = { n0->x - v0->x, n0->y - v0->y, n0->z - v0->z };
		vec3 nDir1 = { n1->x - v1->x, n1->y - v1->y, n1->z - v1->z };
		vec3 nDir2 = { n2->x - v2->x, n2->y - v2->y, n2->z - v2->z };

		vec2 zero[3] = { 0 };
		vec2* srcUV = (uv && textureId >= 0) ? uv : zero;
		const u8 planeMode = 0;

		const s32 vertexStart = *(ctx->curVertexStart);
		s_indexData.push_back(getVertex(ctx, v0, &srcUV[0], &nDir0, color, planeMode, textureId) + vertexStart);
		s_indexData.push_back(getVertex(ctx, v1, &srcUV[1], &nDir1, color, planeMode, textureId) + vertexStart);
		s_indexData.push_back(getVertex(ctx, v2, &srcUV[2], &nDir2, color, planeMode, textureId) + vertexStart);
	}

	static void addSmoothQuad(ModelBuildCtx *ctx, s32* indices, u8 color, vec2* uv, s32 textureId)
	{
		vec3* v0 = &ctx->model->vertices[indices[0]];
		vec3* v1 = &ctx->model->vertices[indices[1]];
		vec3* v2 = &ctx->model->vertices[indices[2]];
		vec3* v3 = &ctx->model->vertices[indices[3]];

		vec3* n0 = &ctx->model->vertexNormals[indices[0]];
		vec3* n1 = &ctx->model->vertexNormals[indices[1]];
		vec3* n2 = &ctx->model->vertexNormals[indices[2]];
		vec3* n3 = &ctx->model->vertexNormals[indices[3]];

		vec3 nDir0 = { n0->x - v0->x, n0->y - v0->y, n0->z - v0->z };
		vec3 nDir1 = { n1->x - v1->x, n1->y - v1->y, n1->z - v1->z };
		vec3 nDir2 = { n2->x - v2->x, n2->y - v2->y, n2->z - v2->z };
		vec3 nDir3 = { n3->x - v3->x, n3->y - v3->y, n3->z - v3->z };

		vec2 zero[4] = { 0 };
		vec2* srcUV = (uv && textureId >= 0) ? uv : zero;
		const u8 planeMode = 0;

		u32 outIndices[4];
		outIndices[0] = getVertex(ctx, v0, &srcUV[0], &nDir0, color, planeMode, textureId);
		outIndices[1] = getVertex(ctx, v1, &srcUV[1], &nDir1, color, planeMode, textureId);
		outIndices[2] = getVertex(ctx, v2, &srcUV[2], &nDir2, color, planeMode, textureId);
		outIndices[3] = getVertex(ctx, v3, &srcUV[3], &nDir3, color, planeMode, textureId);

		const s32 vertexStart = *(ctx->curVertexStart);
		s_indexData.push_back(outIndices[0] + vertexStart);
		s_indexData.push_back(outIndices[1] + vertexStart);
		s_indexData.push_back(outIndices[2] + vertexStart);

		s_indexData.push_back(outIndices[0] + vertexStart);
		s_indexData.push_back(outIndices[2] + vertexStart);
		s_indexData.push_back(outIndices[3] + vertexStart);
	}

	static void addPlaneTriangle(ModelBuildCtx *ctx, s32* indices, vec3* nrml, s32 textureId)
	{
		vec3* v0 = &ctx->model->vertices[indices[0]];
		vec3* v1 = &ctx->model->vertices[indices[1]];
		vec3* v2 = &ctx->model->vertices[indices[2]];
		vec2 uv = { v0->y, 0 };	// Store vertex 0 y to determine the plane Y in the shader.

		const vec3 up = { 0,  ONE_16, 0 };
		const vec3 dn = { 0, -ONE_16, 0 };
		vec3 planeNrm = nrml->y - v1->y > 0 ? up : dn;
		const u8 planeMode = 1;
		const u8 color = 255;

		const s32 vertexStart = *(ctx->curVertexStart);
		s_indexData.push_back(getVertex(ctx, v0, &uv, &planeNrm, color, planeMode, textureId) + vertexStart);
		s_indexData.push_back(getVertex(ctx, v1, &uv, &planeNrm, color, planeMode, textureId) + vertexStart);
		s_indexData.push_back(getVertex(ctx, v2, &uv, &planeNrm, color, planeMode, textureId) + vertexStart);
	}

	static void addPlaneQuad(ModelBuildCtx *ctx, s32* indices, vec3* nrml, s32 textureId)
	{
		vec3* v0 = &(ctx->model)->vertices[indices[0]];
		vec3* v1 = &(ctx->model)->vertices[indices[1]];
		vec3* v2 = &(ctx->model)->vertices[indices[2]];
		vec3* v3 = &(ctx->model)->vertices[indices[3]];
		vec2 uv = { v0->y, 0 };	// Store vertex 0 y to determine the plane Y in the shader.

		const vec3 up = { 0,  ONE_16, 0 };
		const vec3 dn = { 0, -ONE_16, 0 };
		vec3 planeNrm = nrml->y - v1->y > 0 ? up : dn;
		const u8 planeMode = 1;
		const u8 color = 255;

		u32 outIndices[4];
		outIndices[0] = getVertex(ctx, v0, &uv, &planeNrm, color, planeMode, textureId);
		outIndices[1] = getVertex(ctx, v1, &uv, &planeNrm, color, planeMode, textureId);
		outIndices[2] = getVertex(ctx, v2, &uv, &planeNrm, color, planeMode, textureId);
		outIndices[3] = getVertex(ctx, v3, &uv, &planeNrm, color, planeMode, textureId);

		const s32 vertexStart = *(ctx->curVertexStart);
		s_indexData.push_back(outIndices[0] + vertexStart);
		s_indexData.push_back(outIndices[1] + vertexStart);
		s_indexData.push_back(outIndices[2] + vertexStart);

		s_indexData.push_back(outIndices[0] + vertexStart);
		s_indexData.push_back(outIndices[2] + vertexStart);
		s_indexData.push_back(outIndices[3] + vertexStart);
	}

	void model_loadGpuModels()
	{
		ModelBuildCtx ctx;
		s32 indexStart  = 0;
		s32 vertexStart = 0;
		s_vertexData.clear();
		s_indexData.clear();

		s_modelVertexBuffer.destroy();
		s_modelIndexBuffer.destroy();

		// For now handle both pools here.
		for (s32 pool = 0; pool < POOL_COUNT; pool++)
		{
			const std::vector<JediModel*>& modelList = TFE_Model_Jedi::getModelList(AssetPool(pool));
			const size_t modelCount = modelList.size();
			JediModel*const* model = modelList.data();
			for (size_t i = 0; i < modelCount; i++)
			{
				if (model[i]->flags & MFLAG_DRAW_VERTICES)
				{
					if (!buildModelDrawVertices(model[i], &indexStart, &vertexStart))
					{
						// Too many unique models!
						break;
					}
					continue;
				}

				// This model has solid polygons.
				startModel(&ctx, model[i], &indexStart, &vertexStart);
				for (s32 p = 0; p < model[i]->polygonCount; p++)
				{
					JmPolygon* poly = &model[i]->polygons[p];
					if (poly->texture && (poly->texture->flags & OPACITY_TRANS) && poly->shading == PSHADE_PLANE)
					{
						ctx.modelTrans = true;
					}

					// Vanilla DF does not support transparency on TEXTURE shading, but TFE will support it using a flag set in the 3DO file
					if (poly->texture && (poly->texture->flags & OPACITY_TRANS) && (poly->textureFlags & TEXFLAG_TRANSPARENT) && (poly->shading & PSHADE_TEXTURE))
					{
						ctx.modelTrans = true;
					}

					switch (poly->shading)
					{
						case PSHADE_FLAT:
						{
							// Flat shaded polygon
							if (poly->vertexCount == 3)
							{
								addFlatTriangle(&ctx, poly->indices, poly->color, poly->uv, &model[i]->polygonNormals[p], -1);
							}
							else
							{
								addFlatQuad(&ctx, poly->indices, poly->color, poly->uv, &model[i]->polygonNormals[p], -1);
							}
						} break;
						case PSHADE_GOURAUD:
						{
							// Smooth shaded polygon
							if (poly->vertexCount == 3)
							{
								addSmoothTriangle(&ctx, poly->indices, poly->color, poly->uv, -1);
							}
							else
							{
								addSmoothQuad(&ctx, poly->indices, poly->color, poly->uv, -1);
							}
						} break;
						case PSHADE_TEXTURE:
						{
							// Flat shaded textured polygon
							if (poly->vertexCount == 3)
							{
								addFlatTriangle(&ctx, poly->indices, poly->color, poly->uv, &model[i]->polygonNormals[p], poly->texture->textureId);
							}
							else
							{
								addFlatQuad(&ctx, poly->indices, poly->color, poly->uv, &model[i]->polygonNormals[p], poly->texture->textureId);
							}
						} break;
						case PSHADE_GOURAUD_TEXTURE:
						{
							// Smooth shaded textured polygon
							if (poly->vertexCount == 3)
							{
								addSmoothTriangle(&ctx, poly->indices, poly->color, poly->uv, poly->texture->textureId);
							}
							else
							{
								addSmoothQuad(&ctx, poly->indices, poly->color, poly->uv, poly->texture->textureId);
							}
						} break;
						case PSHADE_PLANE:
						{
							// "Plane" shaded textured polygon
							if (poly->vertexCount == 3)
							{
								addPlaneTriangle(&ctx, poly->indices, &model[i]->polygonNormals[p], poly->texture->textureId);
							}
							else
							{
								addPlaneQuad(&ctx, poly->indices, &model[i]->polygonNormals[p], poly->texture->textureId);
							}
						} break;
					};
				}
				endModel(&ctx);
			}
		}

		s_modelVertexBuffer.create((u32)s_vertexData.size(), sizeof(ModelVertex), c_modelAttrCount, c_modelAttrMapping, false, s_vertexData.data());
		s_modelIndexBuffer.create((u32)s_indexData.size(), sizeof(u32), false, s_indexData.data());
	}

	void model_drawListClear()
	{
		for (s32 i = 0; i < MGPU_SHADER_COUNT; i++)
		{
			s_modelDrawList[i].clear();
		}
		s_3doRendered = 0;
		s_3doPolygons = 0;
		model_updateShaders(false);
	}

	void model_drawListFinish()
	{
	}

	// This will only reallocate the array if size > capacity.
	// At some point the system will settle on a maximum capacity and no more allocations will be needed.
	ModelDraw* getDrawItem(ModelShader shader)
	{
		// This will allocate in chunks and only when size > capacity.
		s_modelDrawList[shader].resize(s_modelDrawList[shader].size() + 1);
		return &s_modelDrawList[shader].back();
	}

	void model_add(void* obj, JediModel* model, Vec3f posWS, fixed16_16* transform, f32 ambient, Vec2f floorOffset, Vec2f ceilOffset, u32 portalInfo)
	{
		// Make sure the model has been assigned a GPU ID.
		if (!model || !model->drawId)
		{
			return;
		}

		ModelGPU* modelGPU = (ModelGPU *)model->drawId;
		ModelDraw* drawItem = getDrawItem(modelGPU->shader);
		
		drawItem->modelId = model->drawId;
		drawItem->posWS = posWS;
		drawItem->portalInfo = portalInfo;
		drawItem->obj = obj;
		for (s32 i = 0; i < 9; i++)
		{
			drawItem->transform[i] = fixed16ToFloat(transform[i]);
		}

		drawItem->lightData =
		{
			f32(s_worldAmbient) + (s_showWireframe ? 128.0f : 0.0f), min(ambient, 31.0f) + (s_cameraLightSource ? 64.0f : 0.0f)
		};
		assert(drawItem->lightData.x < 64.0f || s_showWireframe);
		drawItem->textureOffsets =
		{
			floorOffset.x, floorOffset.z,
			ceilOffset.x, ceilOffset.z,
		};

		s_3doRendered++;
		s_3doPolygons += modelGPU->polyCount;
	}

	void model_drawList()
	{
		const TFE_Settings_Graphics* settings = TFE_Settings::getGraphicsSettings();

		// Bind the uber-vertex and index buffers. This holds geometry for *all* 3D models currently loaded.
		s_modelVertexBuffer.bind();
		s_modelIndexBuffer.bind();

		Shader* shader = s_modelShaders;
		for (s32 s = 0; s < MGPU_SHADER_COUNT; s++, shader++)
		{
			const size_t listCount = s_modelDrawList[s].size();
			const ModelDraw* drawList = s_modelDrawList[s].data();
			if (!listCount) { continue; }
			
			// Bind the shader and set per-frame shader variables.
			shader->bind();

			shader->setVariable(s_shaderInputs[s].cameraPosId,   SVT_VEC3,   s_cameraPos.m);
			shader->setVariable(s_shaderInputs[s].cameraViewId,  SVT_MAT3x3, s_cameraMtx.data);
			shader->setVariable(s_shaderInputs[s].cameraProjId,  SVT_MAT4x4, s_cameraProj.data);
			shader->setVariable(s_shaderInputs[s].cameraDirId,   SVT_VEC3,   s_cameraDir.m);
			shader->setVariable(s_shaderInputs[s].cameraRightId, SVT_VEC3,   s_cameraRight.m);
			if (s_shaderInputs[s].texSamplingParamId > 0)
			{
				const f32 texSamplingParam[] = { settings->useBilinear ? settings->bilinearSharpness : 0.0f, 0.0f, 0.0f, 0.0f };
				shader->setVariable(s_shaderInputs[s].texSamplingParamId, SVT_VEC4, texSamplingParam);
			}
			if (s_shaderInputs[s].palFxLumMask >= 0 && s_shaderInputs[s].palFxFlash >= 0)
			{
				Vec3f lumMask, palFx;
				renderer_getPalFx(&lumMask, &palFx);

				shader->setVariable(s_shaderInputs[s].palFxLumMask, SVT_VEC3, lumMask.m);
				shader->setVariable(s_shaderInputs[s].palFxFlash, SVT_VEC3, palFx.m);
			}
			if (s_shaderInputs[s].textureSettings >= 0)
			{
				shader->setVariable(s_shaderInputs[s].textureSettings, SVT_USCALAR, &s_textureSettings);
			}
			
			// Draw items in the current draw list (draw lists are bucketed by shader).
			for (size_t i = 0; i < listCount; i++)
			{
				const ModelDraw* drawItem = &drawList[i];
				const ModelGPU* model = (ModelGPU *)drawItem->modelId;
				const u32 portalInfo[] = { drawItem->portalInfo, drawItem->portalInfo };

				// Per-draw shader variables.
				shader->setVariable(s_shaderInputs[s].modelPosId,  SVT_VEC3,   drawItem->posWS.m);
				shader->setVariable(s_shaderInputs[s].modelMtxId,  SVT_MAT3x3, drawItem->transform);
				shader->setVariable(s_shaderInputs[s].lightDataId, SVT_VEC2,   drawItem->lightData.m);
				shader->setVariable(s_shaderInputs[s].textureOffsetId, SVT_VEC4, drawItem->textureOffsets.m);
				shader->setVariable(s_shaderInputs[s].portalInfo,  SVT_UVEC2,  portalInfo);

				// Draw the geometry (note a single vertex/index buffer is used, so this is just a count and start offset).
				TFE_RenderBackend::drawIndexedTriangles(model->polyCount, sizeof(u32), model->indexStart);

				if (s_drawnObjCount < MAX_DRAWN_OBJ_STORE)
				{
					s_drawnObj[s_drawnObjCount++] = (SecObject*)drawItem->obj;
				}
			}
		}

		// Cleanup
		s_modelVertexBuffer.unbind();
		s_modelIndexBuffer.unbind();
	}
}
