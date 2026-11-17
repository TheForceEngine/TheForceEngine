#include <cstring>

#include "modelAsset_jedi.h"
#include <TFE_System/system.h>
#include <TFE_Settings/settings.h>
#include <TFE_Asset/assetSystem.h>
#include <TFE_FileSystem/filestream.h>
#include <TFE_FileSystem/paths.h>
#include <TFE_System/parser.h>

#include <TFE_Jedi/Math/core_math.h>
#include <TFE_Jedi/Level/rtexture.h>
#include <TFE_Jedi/Serialization/serialization.h>
// TODO: dependency on JediRenderer, this should be refactored...
#include <TFE_Jedi/Renderer/rlimits.h>

#include <assert.h>
#include <map>
#include <algorithm>

using namespace TFE_Jedi;
extern MemoryRegion* s_gameRegion;
extern MemoryRegion* s_levelRegion;
static MemoryRegion* s_memRegion;

#define model_alloc(size) TFE_Memory::region_alloc(s_memRegion, size)
#define model_free(ptr) TFE_Memory::region_free(s_memRegion, ptr)

// Jedi code for processing models.
// TODO: Move model processing to its own file.
// TODO: Consider processed model binary caching - once models get large for mods, parsing and processiong
//       will drastically slow down load times.
namespace TFE_Jedi_Object3d
{
	void vec3_computeNormalOffset(const vec3* vIn, const vec3* v0, vec3* vOut)
	{
		const fixed16_16 dx = vIn->x - v0->x;
		const fixed16_16 dy = vIn->y - v0->y;
		const fixed16_16 dz = vIn->z - v0->z;
		fixed16_16 len;

		if (TFE_Settings::normalFix3do())
		{
			// Convert to floating point before calculating the length to avoid
			// fixed-point overflow.
			const f32 xf = fixed16ToFloat(dx);
			const f32 yf = fixed16ToFloat(dy);
			const f32 zf = fixed16ToFloat(dz);
			const f32 lenf = sqrtf(xf*xf + yf*yf + zf*zf);
			// Then convert back so the rest of the code stays the same.
			len = floatToFixed16(lenf);
		}
		else
		{
			const fixed16_16 xSq = mul16(dx, dx);
			const fixed16_16 ySq = mul16(dy, dy);
			const fixed16_16 zSq = mul16(dz, dz);
			len = fixedSqrt(xSq + ySq + zSq);
		}

		// The Jedi 3D object renderer expect normals in the form of v0 + dir rather than just dir as used today.
		vOut->x = v0->x;
		vOut->y = v0->y;
		vOut->z = v0->z;
		if (len > 0)
		{
			vOut->x += div16(dx, len);
			vOut->y += div16(dy, len);
			vOut->z += div16(dz, len);
		}
	}

	void object3d_computePolygonNormal(const vec3* v0, const vec3* v1, const vec3* v2, vec3* out)
	{
		const fixed16_16 dx10 = v1->x - v0->x;
		const fixed16_16 dy10 = v1->y - v0->y;
		const fixed16_16 dz10 = v1->z - v0->z;
		const fixed16_16 dx20 = v2->x - v0->x;
		const fixed16_16 dy20 = v2->y - v0->y;
		const fixed16_16 dz20 = v2->z - v0->z;

		out->x = v0->x + mul16(dz10, dy20) - mul16(dy10, dz20);
		out->y = v0->y + mul16(dx10, dz20) - mul16(dz10, dx20);
		out->z = v0->z + mul16(dy10, dx20) - mul16(dx10, dy20);
		vec3_computeNormalOffset(out, v0, out);
	}

	void object3d_computeVertexNormals(JediModel* model)
	{
		const s32 vertexCount = model->vertexCount;
		const vec3* vertex = model->vertices;

		model->vertexNormals = (vec3*)model_alloc(vertexCount * sizeof(vec3));
		vec3* outNormal = model->vertexNormals;
		if (!outNormal)
		{
			TFE_System::logWrite(LOG_ERROR, "ComputeVertexNormals", "Output normal pointer is NULL.");
			return;
		}

		// For each vertex, go through each polyon and see if it belongs.
		// Add polygon normals for each polygon the vertex belongs to.
		// Note: this is pretty brute force and will not scale well to high polygon models and is part of the reason why loading
		// Dark Forces levels back in the day took so long...
		for (s32 v = 0; v < vertexCount; v++, vertex++, outNormal++)
		{
			vec3 normal = { 0 };
			s32 ncount = 0;
			vec3* cnrm = model->polygonNormals;

			const JmPolygon* polygon = model->polygons;
			for (s32 p = 0; p < model->polygonCount; p++, polygon++, cnrm++)
			{
				const s32 polyVtxCount = polygon->vertexCount;
				const s32* indices = polygon->indices;
				for (s32 i = 0; i < polyVtxCount; i++)
				{
					if (v == indices[i])
					{
						const vec3* v1 = &model->vertices[indices[1]];
						normal.x += cnrm->x - v1->x;
						normal.y += cnrm->y - v1->y;
						normal.z += cnrm->z - v1->z;
						ncount++;
						break;
					}
				}
			}

			// The Jedi 3D object renderer expect normals in the form of v0 + dir rather than just dir as used today.
			outNormal->x = vertex->x;
			outNormal->y = vertex->y;
			outNormal->z = vertex->z;
			if (ncount > 0)
			{
				ncount = intToFixed16(ncount);
				normal.x = div16(normal.x, ncount);
				normal.y = div16(normal.y, ncount);
				normal.z = div16(normal.z, ncount);

				const fixed16_16 lenSq = mul16(normal.x, normal.x) + mul16(normal.y, normal.y) + mul16(normal.z, normal.z);
				const fixed16_16 len = fixedSqrt(lenSq);
				if (len > 0)
				{
					outNormal->x += div16(normal.x, len);
					outNormal->y += div16(normal.y, len);
					outNormal->z += div16(normal.z, len);
				}
			}
		}
	}
}

using namespace TFE_Jedi_Object3d;

namespace TFE_Model_Jedi
{
	typedef std::map<std::string, JediModel*> ModelMap;
	typedef std::vector<JediModel*> ModelList;
	typedef std::map<std::string, TextureData*> TextureMap;
	typedef std::vector<std::string> NameList;
	static ModelMap s_models[POOL_COUNT];
	static ModelList s_modelList[POOL_COUNT];
	static NameList s_modelNames[POOL_COUNT];
	static std::vector<char> s_buffer;

	// Remove 3DO limits.
	static std::vector<vec2> s_tmpVtx;

	bool parseModel(JediModel* model, const char* name, AssetPool pool);

	JediModel* get(const char* name, AssetPool pool)
	{
		ModelMap::iterator iModel = s_models[pool].find(name);
		if (iModel != s_models[pool].end())
		{
			return iModel->second;
		}

		// It doesn't exist yet, try to load the model.
		FilePath filePath;
		if (!TFE_Paths::getFilePath(name, &filePath))
		{
			return nullptr;
		}
		FileStream file;
		if (!file.open(&filePath, Stream::MODE_READ))
		{
			return nullptr;
		}
		size_t len = file.getSize();
		s_buffer.resize(len);
		file.readBuffer(s_buffer.data(), u32(len));
		file.close();
			
		s_memRegion = (pool == POOL_GAME) ? s_gameRegion : s_levelRegion;
		JediModel* model = (JediModel*)model_alloc(sizeof(JediModel));
		memset(model, 0, sizeof(JediModel));

		////////////////////////////////////////////////////////////////
		// Load and parse the model.
		////////////////////////////////////////////////////////////////
		if (!parseModel(model, name, pool))
		{
			return nullptr;
		}

		////////////////////////////////////////////////////////////////
		// Post process the model.
		////////////////////////////////////////////////////////////////
		// Compute culling normals.
		model->polygonNormals = (vec3*)model_alloc(model->polygonCount * sizeof(vec3));

		vec3* polygonNormal = model->polygonNormals;
		JmPolygon* polygon = model->polygons;
		for (s32 i = 0; i < model->polygonCount; i++, polygon++, polygonNormal++)
		{
			vec3* v0 = &model->vertices[polygon->indices[0]];
			vec3* v1 = &model->vertices[polygon->indices[1]];
			vec3* v2 = &model->vertices[polygon->indices[2]];
			object3d_computePolygonNormal(v1, v2, v0, polygonNormal);
		}

		// Compute vertex normals if vertex lighting is required.
		if (model->flags & MFLAG_VERTEX_LIT)
		{
			object3d_computeVertexNormals(model);
		}

		// Compute the radius of the model (from <0,0,0>).
		vec3* vertex = model->vertices;
		fixed16_16 maxDist = 0;
		for (s32 v = 0; v < model->vertexCount; v++, vertex++)
		{
			const fixed16_16 distSq = mul16(vertex->x,vertex->x) + mul16(vertex->y,vertex->y) + mul16(vertex->z,vertex->z);
			const fixed16_16 dist = fixedSqrt(distSq);
			if (dist > maxDist)
			{
				maxDist = dist;
			}
		}
		model->radius = maxDist;

		// TODO (maybe): Cache binary models to disk so they can be
		// directly loaded, which will reduce load time.
		s_models[pool][name] = model;
		s_modelList[pool].push_back(model);
		s_modelNames[pool].push_back(name);
		return model;
	}

	bool getModelIndex(JediModel* model, s32* index, AssetPool* pool)
	{
		assert(index && pool);
		if (!index || !pool) { return false; }

		for (s32 p = 0; p < POOL_COUNT; p++)
		{
			const size_t count = s_modelList[p].size();
			JediModel** list = s_modelList[p].data();
			for (size_t m = 0; m < count; m++)
			{
				if (list[m] == model)
				{
					*index = s32(m);
					*pool = AssetPool(p);
					return true;
				}
			}
		}
		return false;
	}

	JediModel* getModelByIndex(s32 index, AssetPool pool)
	{
		if (pool >= POOL_COUNT || index >= (s32)s_modelList[pool].size())
		{
			return nullptr;
		}
		return s_modelList[pool][index];
	}

	const std::vector<JediModel*>& getModelList(AssetPool pool)
	{
		return s_modelList[pool];
	}

	void freePool(AssetPool pool)
	{
		// Memory will get freed with the memory region automatically.
		s_models[pool].clear();
		
		// free the memory of each models' drawId object
		const size_t count = s_modelList[pool].size();
		JediModel** models = s_modelList[pool].data();
		for (size_t i = 0; i < count; i++)
		{
			free(models[i]->drawId);
		}

		s_modelList[pool].clear();
		s_modelNames[pool].clear();
	}

	void serializeModels(Stream* stream)
	{
		bool modeWrite = serialization_getMode() == SMODE_WRITE;
		if (!modeWrite)
		{
			freePool(POOL_LEVEL);
		}

		s32 count = (s32)s_modelNames[POOL_LEVEL].size();
		SERIALIZE(SaveVersionInit, count, 0);

		std::string* names = s_modelNames[POOL_LEVEL].data();
		std::string name;
		for (s32 i = 0; i < count; i++)
		{
			u8 size;
			if (modeWrite)
			{
				size = (u8)names[i].length();
			}
			SERIALIZE(SaveVersionInit, size, 0);

			if (!modeWrite)
			{
				name.resize(size);
			}
			SERIALIZE_BUF(SaveVersionInit, modeWrite ? &names[i][0] : &name[0], size);

			if (!modeWrite)
			{
				get(name.c_str(), POOL_LEVEL);
			}
		}
	}

	void freeAll()
	{
		for (s32 p = 0; p < POOL_COUNT; p++)
		{
			freePool(AssetPool(p));
		}
	}
	
	void freeLevelData()
	{
		freePool(POOL_LEVEL);
	}

	void allocatePolygon(JmPolygon* polygon, s32 vertexCount)
	{
		polygon->texture = 0;
		polygon->shading = PSHADE_FLAT;
		polygon->index = 0;
		polygon->p08 = 0;
		polygon->vertexCount = vertexCount;
		polygon->uv = nullptr;
		polygon->indices = (s32*)model_alloc(vertexCount * sizeof(s32));
		polygon->textureFlags = 0;
	}
	
	bool parseModel(JediModel* model, const char* name, AssetPool pool)
	{
		if (s_buffer.empty()) { return false; }
		const size_t len = s_buffer.size();

		model->isBridge = 0;
		model->vertexCount = 0;
		model->vertices = nullptr;
		model->polygonCount = 0;
		model->polygons = nullptr;
		model->vertexNormals = nullptr;
		model->polygonNormals = nullptr;
		model->flags = 0;
		model->textureCount = 0;
		model->textures = nullptr;
		model->radius = 0;
		model->drawId = nullptr;	// invalid ID initially.

		// Check to see if the name has an underscore.
		// If so, set the "isBridge" field.
		const size_t nameLen = strlen(name);
		for (size_t i = 0; i < nameLen; i++)
		{
			if (name[i] == '_')
			{
				model->isBridge = 1;
				break;
			}
		}

		TFE_Parser parser;
		size_t bufferPos = 0;
		parser.init(s_buffer.data(), len);
		const char* fileBuffer = s_buffer.data();
		parser.addCommentString("#");

		// For now just do what the original code does.
		// TODO: Make this more robust.
		const char* buffer = parser.readLine(bufferPos, true);
		if (!buffer) { return false; }

		f32 version;
		if (sscanf(buffer, "3DO %f", &version) != 1)
		{
			TFE_System::logWrite(LOG_ERROR, "Object3D_Load", "'%s' has no valid name.", name);
			assert(0);
			return false;
		}
		if (version < 1.2f)
		{
			TFE_System::logWrite(LOG_ERROR, "Object3D_Load", "'%s' has inavlid version", name);
			assert(0);
			return false;
		}

		buffer = parser.readLine(bufferPos, true);
		if (!buffer) { return false; }
		char name3do[32];
		if (sscanf(buffer, "3DONAME %s", name3do) != 1)
		{
			TFE_System::logWrite(LOG_ERROR, "Object3D_Load", "'%s' has no valid 3DONAME.", name);
			assert(0);
			return false;
		}

		buffer = parser.readLine(bufferPos, true);
		if (!buffer) { return false; }
		s32 objectCount;
		if (sscanf(buffer, "OBJECTS %d", &objectCount) != 1)
		{
			TFE_System::logWrite(LOG_ERROR, "Object3D_Load", "'%s' has no objects.", name);
			assert(0);
			return false;
		}

		buffer = parser.readLine(bufferPos, true);
		if (!buffer) { return false; }
		s32 vertexCount;
		if (sscanf(buffer, "VERTICES %d", &vertexCount) != 1)
		{
			TFE_System::logWrite(LOG_ERROR, "Object3D_Load", "'%s' has no vertices.", name);
			assert(0);
			return false;
		}
		if (!TFE_Settings::ignore3doLimits() && vertexCount > MAX_VERTEX_COUNT_3DO)
		{
			TFE_System::logWrite(LOG_ERROR, "Object3D_Load", "'%s' has too many vertices: %d / %d.", name, vertexCount, MAX_VERTEX_COUNT_3DO);
			assert(0);
			return false;
		}
		model->vertices = (vec3*)model_alloc(vertexCount * sizeof(vec3));

		buffer = parser.readLine(bufferPos, true);
		if (!buffer) { return false; }
		s32 polygonCount;
		if (sscanf(buffer, "POLYGONS %d", &polygonCount) != 1)
		{
			TFE_System::logWrite(LOG_ERROR, "Object3D_Load", "'%s' has no polygons.", name);
			assert(0);
			return false;
		}
		if (!TFE_Settings::ignore3doLimits() && polygonCount > MAX_POLYGON_COUNT_3DO)
		{
			TFE_System::logWrite(LOG_ERROR, "Object3D_Load", "'%s' has too many polygons: %d / %d.", name, polygonCount, MAX_POLYGON_COUNT_3DO);
			assert(0);
			return false;
		}
		model->polygons = (JmPolygon*)model_alloc(polygonCount * sizeof(JmPolygon));

		buffer = parser.readLine(bufferPos, true);
		if (!buffer) { return false; }
		char palette[32];
		if (sscanf(buffer, "PALETTE %s", palette) != 1)
		{
			TFE_System::logWrite(LOG_ERROR, "Object3D_Load", "'%s' has no PALETTE.", name);
			assert(0);
			return false;
		}

		buffer = parser.readLine(bufferPos, true);
		if (!buffer) { return false; }
		s32 textureCount;
		if (sscanf(buffer, "TEXTURES %d", &textureCount) != 1)
		{
			TFE_System::logWrite(LOG_ERROR, "Object3D_Load", "'%s' has no valid TEXTURES entry.", name);
			assert(0);
			return false;
		}

		// Load textures.
		MemoryRegion* prevMemRegion = TFE_Jedi::bitmap_getAllocator();
		TFE_Jedi::bitmap_setAllocator(s_memRegion);
		model->textures = nullptr;
		if (textureCount)
		{
			model->textures = (TextureData**)model_alloc(textureCount * sizeof(TextureData*));
			model->textureCount = textureCount;
			TextureData** texture = model->textures;
			for (s32 i = 0; i < textureCount; i++, texture++)
			{
				buffer = parser.readLine(bufferPos, true);
				if (!buffer) { return false; }

				char textureName[256];
				if (sscanf(buffer, " TEXTURE: %s ", textureName) != 1)
				{
					TFE_System::logWrite(LOG_WARNING, "Object3D_Load", "'%s' unable to parse TEXTURE: entry.", name);
					*texture = TFE_Jedi::bitmap_load("default.bm", 1, pool);
					if ((*texture)) { (*texture)->flags |= ENABLE_MIP_MAPS; }
					continue;
				}

				*texture = nullptr;
				if (strcasecmp(textureName, "<NoTexture>"))
				{
					*texture = TFE_Jedi::bitmap_load(textureName, 1, pool);
					if (!(*texture))
					{
						*texture = TFE_Jedi::bitmap_load("default.bm", 1, pool);
					}
				}
				if ((*texture)) { (*texture)->flags |= ENABLE_MIP_MAPS; }
			}
		}
		TFE_Jedi::bitmap_setAllocator(prevMemRegion);

		bool nextLine = true;
		s32 vertexOffset = 0;
		for (s32 i = 0; i < objectCount; i++)
		{
			if (nextLine)
			{
				buffer = parser.readLine(bufferPos, true);
			}
			if (!buffer) { break; }
			nextLine = true;

			char objName[32];
			if (sscanf(buffer, " OBJECT %s", objName) != 1)
			{
				// Some of the models have fewer object entries than objectCount (bad data, "fixed" by the code).
				TFE_System::logWrite(LOG_WARNING, "Object3D_Load", "'%s' has too few objects, continuing anyway.", name);
				break;
			}

			buffer = parser.readLine(bufferPos, true);
			if (!buffer) { return false; }
			s32 textureId;
			char transparency[32];
			if (sscanf(buffer, " TEXTURE %d %s ", &textureId, transparency) < 1)
			{
				TFE_System::logWrite(LOG_ERROR, "Object3D_Load", "'%s' unable to parse TEXTURE ID.", name);
				assert(0);
				return false;
			}
			TextureData* texture = nullptr;
			if (textureId != -1 && model->textures)
			{
				texture = model->textures[textureId];
			}

			// TFE - texture transparency information
			s32 texFlags = 0;
			if (strcasecmp(transparency, "TRANSPARENT") == 0)
			{
				texFlags |= TEXFLAG_TRANSPARENT;
			}

			buffer = parser.readLine(bufferPos, true);
			if (!buffer) { return false; }
			if (sscanf(buffer, " VERTICES %d", &vertexCount) != 1)
			{
				TFE_System::logWrite(LOG_ERROR, "Object3D_Load", "'%s' unable to parse object VERTICES entry.", name);
				assert(0);
				return false;
			}
			// Apparently zero vertex objects are allowed, these simply refer back to the existing vertex offset.
			if (vertexCount)
			{
				vertexOffset = model->vertexCount;
			}

			while (1)
			{
				buffer = parser.readLine(bufferPos, true);
				if (!buffer)
				{ 
					nextLine = false;
					break;
				}

				s32 index;
				f32 x, y, z;
				if (sscanf(buffer, " %d: %f %f %f", &index, &x, &y, &z) != 4)
				{
					nextLine = false;
					break;
				}
				model->vertices[model->vertexCount].x = floatToFixed16(x);
				model->vertices[model->vertexCount].y = floatToFixed16(y);
				model->vertices[model->vertexCount].z = floatToFixed16(z);
				model->vertexCount++;
			}

			if (nextLine)
			{
				buffer = parser.readLine(bufferPos, true);
				if (!buffer) { return false; }
			}
			nextLine = true;

			s32 triangleCount = 0, quadCount = 0;
			s32 basePolygonCount = model->polygonCount;
			if (sscanf(buffer, " TRIANGLES %d", &triangleCount) == 1)
			{
				s32 polygonCount = model->polygonCount;
				while (1)
				{
					buffer = parser.readLine(bufferPos, true);
					if (!buffer)
					{
						nextLine = false;
						break;
					}

					s32 num, a, b, c, color;
					char shading[32];
					if (sscanf(buffer, " %d: %d %d %d %d %s ", &num, &a, &b, &c, &color, shading) != 6)
					{
						nextLine = false;
						break;
					}

					a += vertexOffset;
					b += vertexOffset;
					c += vertexOffset;

					JmPolygon* polygon = &model->polygons[model->polygonCount];
					allocatePolygon(polygon, 3);

					polygon->index = model->polygonCount;
					polygon->texture = texture;
					polygon->indices[0] = a;
					polygon->indices[1] = b;
					polygon->indices[2] = c;
					polygon->color = color;
					polygon->textureFlags = texFlags;

					model->polygonCount++;

					if (strcasecmp(shading, "FLAT") == 0)
					{
						if (TFE_Settings::getGraphicsSettings()->forceGouraudShading)
						{
							polygon->shading = PSHADE_GOURAUD;
							model->flags |= MFLAG_VERTEX_LIT;
						}
						else
						{
							polygon->shading = PSHADE_FLAT;
						}
					}
					else if (strcasecmp(shading, "VERTEX") == 0)
					{
						polygon->shading = PSHADE_FLAT;
						model->flags = MFLAG_DRAW_VERTICES;
					}
					else if (strcasecmp(shading, "TEXTURE") == 0)
					{
						if (TFE_Settings::getGraphicsSettings()->forceGouraudShading)
						{
							polygon->shading = PSHADE_GOURAUD_TEXTURE;
							model->flags |= MFLAG_VERTEX_LIT;
						}
						else
						{
							polygon->shading = PSHADE_TEXTURE;
						}
					}
					else if (strcasecmp(shading, "GOURAUD") == 0)
					{
						polygon->shading = PSHADE_GOURAUD;
						model->flags |= MFLAG_VERTEX_LIT;
					}
					else if (strcasecmp(shading, "GOURTEX") == 0)
					{
						polygon->shading = PSHADE_GOURAUD_TEXTURE;
						model->flags |= MFLAG_VERTEX_LIT;
					}
					else if (strcasecmp(shading, "PLANE") == 0)
					{
						polygon->shading = PSHADE_PLANE;
					}
					else
					{
						// ERROR!
						TFE_System::logWrite(LOG_ERROR, "Object3D_Load", "Invalid polygon shading: %s", shading);
						assert(0);
					}
				}
			}
			else if (sscanf(buffer, " QUADS %d", &quadCount) == 1)
			{
				s32 polygonCount = model->polygonCount;
				while (1)
				{
					buffer = parser.readLine(bufferPos, true);
					if (!buffer)
					{ 
						nextLine = false;
						break;
					}

					s32 num, a, b, c, d, color;
					char shading[32];
					if (sscanf(buffer, " %d: %d %d %d %d %d %s", &num, &a, &b, &c, &d, &color, shading) != 7)
					{
						nextLine = false;
						break;
					}
					a += vertexOffset;
					b += vertexOffset;
					c += vertexOffset;
					d += vertexOffset;

					JmPolygon* polygon = &model->polygons[model->polygonCount];
					allocatePolygon(polygon, 4);

					polygon->index = model->polygonCount;
					polygon->texture = texture;
					polygon->indices[0] = a;
					polygon->indices[1] = b;
					polygon->indices[2] = c;
					polygon->indices[3] = d;
					polygon->color = color;
					polygon->textureFlags = texFlags;

					model->polygonCount++;

					if (strcasecmp(shading, "FLAT") == 0)
					{
						if (TFE_Settings::getGraphicsSettings()->forceGouraudShading)
						{
							polygon->shading = PSHADE_GOURAUD;
							model->flags |= MFLAG_VERTEX_LIT;
						}
						else
						{
							polygon->shading = PSHADE_FLAT;
						}
					}
					else if (strcasecmp(shading, "VERTEX") == 0)
					{
						polygon->shading = PSHADE_FLAT;
						model->flags = MFLAG_DRAW_VERTICES;
					}
					else if (strcasecmp(shading, "TEXTURE") == 0)
					{
						if (TFE_Settings::getGraphicsSettings()->forceGouraudShading)
						{
							polygon->shading = PSHADE_GOURAUD_TEXTURE;
							model->flags |= MFLAG_VERTEX_LIT;
						}
						else
						{
							polygon->shading = PSHADE_TEXTURE;
						}
					}
					else if (strcasecmp(shading, "GOURAUD") == 0)
					{
						polygon->shading = PSHADE_GOURAUD;
						model->flags |= MFLAG_VERTEX_LIT;
					}
					else if (strcasecmp(shading, "GOURTEX") == 0)
					{
						polygon->shading = PSHADE_GOURAUD_TEXTURE;
						model->flags |= MFLAG_VERTEX_LIT;
					}
					else if (strcasecmp(shading, "PLANE") == 0)
					{
						polygon->shading = PSHADE_PLANE;
					}
					else
					{
						// ERROR!
						TFE_System::logWrite(LOG_ERROR, "Object3D_Load", "Invalid polygon shading: %s", shading);
						assert(0);
					}
				}
			}
			else  // No polygons
			{
				TFE_System::logWrite(LOG_ERROR, "Object3D_Load", "'%s' object %s has no polygons.", name, objName);
				return false;
			}

			if (texture)
			{
				if (nextLine)
				{
					buffer = parser.readLine(bufferPos, true);
					if (!buffer) { return false; }
				}
				nextLine = true;

				s32 texVertexCount;
				if (sscanf(buffer, " TEXTURE VERTICES %d", &texVertexCount) != 1)
				{
					TFE_System::logWrite(LOG_ERROR, "Object3D_Load", "'%s' unable to parse TEXTURE VERTICES.", name);
					assert(0);
					return false;
				}

				// pre-allocate; make sure it handles at least one primitive...
				s_tmpVtx.resize(max(texVertexCount, 4) * 2);

				nextLine = true;
				// Keep reading until the format no longer matches...
				for (s32 v = 0; ; v++)
				{
					buffer = parser.readLine(bufferPos, true);
					if (!buffer) { return false; }

					// Just in case the reported vertex count is too small.
					// Unfortunately data errors like this are common.
					if (v >= s_tmpVtx.size())
					{
						s_tmpVtx.resize(v * 2);
					}

					s32 num;
					f32 x, y;
					if (sscanf(buffer, " %d: %f %f", &num, &x, &y) != 3)
					{
						// Handle buggy input data.
						nextLine = false;
						break;
					}
					// clamp texture coordinate range.
					if (x < 0.0f) { x = 0.0f; }
					if (y < 0.0f) { y = 0.0f; }
					if (x > 1.0f) { x = 1.0f; }
					if (y > 1.0f) { y = 1.0f; }

					s_tmpVtx[v].x = floatToFixed16(x);
					s_tmpVtx[v].y = floatToFixed16(y);
				}

				if (nextLine)
				{
					buffer = parser.readLine(bufferPos, true);
					if (!buffer) { return false; }
				}
				nextLine = true;

				s32 texTriangleCount;
				s32 texQuadCount;
				if (sscanf(buffer, " TEXTURE TRIANGLES %d", &texTriangleCount) == 1)
				{
					JmPolygon* polygon = &model->polygons[basePolygonCount];
					while (1)
					{
						buffer = parser.readLine(bufferPos, true);
						if (!buffer) { break; }

						s32 num, a, b, c;
						if (sscanf(buffer, " %d: %d %d %d", &num, &a, &b, &c) != 4)
						{
							nextLine = false;
							break;
						}

						polygon->uv   = (vec2*)model_alloc(sizeof(vec2) * 3);
						fixed16_16 texWidth  = intToFixed16(texture->uvWidth);
						fixed16_16 texHeight = intToFixed16(texture->uvHeight);

						polygon->uv[0].x = mul16(s_tmpVtx[a].x, texWidth);
						polygon->uv[0].y = mul16(s_tmpVtx[a].y, texHeight);
						polygon->uv[1].x = mul16(s_tmpVtx[b].x, texWidth);
						polygon->uv[1].y = mul16(s_tmpVtx[b].y, texHeight);
						polygon->uv[2].x = mul16(s_tmpVtx[c].x, texWidth);
						polygon->uv[2].y = mul16(s_tmpVtx[c].y, texHeight);

						polygon++;
					}
				}
				else if (sscanf(buffer, " TEXTURE QUADS %d", &texQuadCount) == 1)
				{
					JmPolygon* polygon = &model->polygons[basePolygonCount];
					while (1)
					{
						buffer = parser.readLine(bufferPos, true);
						if (!buffer) { break; }

						s32 num, a, b, c, d;
						if (sscanf(buffer, " %d: %d %d %d %d", &num, &a, &b, &c, &d) != 5)
						{
							nextLine = false;
							break;
						}
						polygon->uv = (vec2*)model_alloc(sizeof(vec2) * 4);
						fixed16_16 texWidth = intToFixed16(texture->uvWidth);
						fixed16_16 texHeight = intToFixed16(texture->uvHeight);

						polygon->uv[0].x = mul16(s_tmpVtx[a].x, texWidth);
						polygon->uv[0].y = mul16(s_tmpVtx[a].y, texHeight);
						polygon->uv[1].x = mul16(s_tmpVtx[b].x, texWidth);
						polygon->uv[1].y = mul16(s_tmpVtx[b].y, texHeight);
						polygon->uv[2].x = mul16(s_tmpVtx[c].x, texWidth);
						polygon->uv[2].y = mul16(s_tmpVtx[c].y, texHeight);
						polygon->uv[3].x = mul16(s_tmpVtx[d].x, texWidth);
						polygon->uv[3].y = mul16(s_tmpVtx[d].y, texHeight);

						polygon++;
					}
				}
			}
		}

		return true;
	}
};
