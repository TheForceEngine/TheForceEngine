#include "editorObj3D.h"
#include "editorColormap.h"
#include "editorAsset.h"
#include <TFE_Editor/editor.h>
#include <TFE_Editor/AssetBrowser/assetBrowser.h>
#include <TFE_DarkForces/mission.h>
#include <TFE_System/parser.h>
#include <TFE_System/system.h>
#include <TFE_Archive/archive.h>
#include <TFE_Jedi/Level/rtexture.h>
#include <TFE_Jedi/Renderer/rcommon.h>
#include <algorithm>
#include <cstring>
#include <vector>
#include <map>

namespace TFE_Editor
{
	static const AttributeMapping c_editorModelAttrMapping[] =
	{
		{ATTR_POS,   ATYPE_FLOAT, 3, 0, false},
		{ATTR_UV,    ATYPE_FLOAT, 2, 0, false},
		{ATTR_COLOR, ATYPE_UINT8, 4, 0, true},
	};
	static const u32 c_editorModelAttrCount = TFE_ARRAYSIZE(c_editorModelAttrMapping);

	typedef std::vector<EditorObj3D*> Obj3DList;
	static Obj3DList s_obj3DList;
	static std::map<u32, std::vector<s32>> s_vertexMap;
	
	void freeObj3D(const char* name)
	{
		const size_t count = s_obj3DList.size();
		EditorObj3D** obj3D = s_obj3DList.data();
		for (size_t i = 0; i < count; i++)
		{
			if (strcasecmp(obj3D[i]->name, name) == 0)
			{
				TFE_RenderBackend::freeTexture(obj3D[i]->thumbnail);
				*obj3D[i] = EditorObj3D{};
				break;
			}
		}
	}

	void freeCachedObj3D()
	{
		const size_t count = s_obj3DList.size();
		EditorObj3D** obj3D = s_obj3DList.data();
		for (size_t i = 0; i < count; i++)
		{
			TFE_RenderBackend::freeTexture(obj3D[i]->thumbnail);
			*obj3D[i] = EditorObj3D{};
		}
		s_obj3DList.clear();
	}

	s32 allocateObj3D(const char* name)
	{
		s32 index = (s32)s_obj3DList.size();
		s_obj3DList.push_back(new EditorObj3D);
		return index;
	}

	s32 addMaterial(u32 shading, TextureGpu* tex, bool drawVertices, EditorObj3D* out3d)
	{
		s32 idx = 0;
		// If drawVertices == true, use a single material.
		if (!drawVertices)
		{
			// Otherwise try to find a matching material.
			// Force tex to null if texture based shading is not enabled.
			if (!(shading&PSHADE_TEXTURE) && !(shading&PSHADE_PLANE))
			{
				tex = nullptr;
			}
			const bool flatProj = (shading & PSHADE_PLANE) != 0;
			const s32 mtlCount = (s32)out3d->mtl.size();
			const EditorMaterial* mtl = out3d->mtl.data();
			idx = -1;
			for (s32 m = 0; m < mtlCount; m++, mtl++)
			{
				if (mtl->texture == tex && flatProj == mtl->flatProj)
				{
					idx = m;
					break;
				}
			}
			if (idx < 0)
			{
				EditorMaterial newMtl = {};
				newMtl.texture = tex;
				newMtl.flatProj = flatProj;
				idx = mtlCount;
				out3d->mtl.push_back(newMtl);
			}
		}
		else if (out3d->mtl.empty())
		{
			EditorMaterial newMtl = {};
			newMtl.texture = nullptr;
			newMtl.flatProj = false;
			out3d->mtl.push_back(newMtl);
		}

		return idx;
	}
	
	s32 addVertex(const Vec3f pos, const Vec2f uv, u32 color, EditorObj3D* out3d)
	{
		const f32 eps = 0.00001f;
		EditorVertex* vtxList = out3d->vtx.data();

		const u32 key = (s32(pos.x + 128.0f) & 255) | ((s32(pos.y + 128.0f) & 255)<<8) | ((s32(pos.z + 128.0f) & 255)<<16);
		std::map<u32, std::vector<s32>>::iterator iCell = s_vertexMap.find(key);
		if (iCell != s_vertexMap.end())
		{
			std::vector<s32>& cell = iCell->second;
			const s32 count = (s32)cell.size();
			const s32* idx = cell.data();

			for (s32 i = 0; i < count; i++)
			{
				EditorVertex* vtx = &vtxList[idx[i]];
				if (fabsf(vtx->pos.x - pos.x) > eps || fabsf(vtx->pos.y - pos.y) > eps || fabsf(vtx->pos.z - pos.z) > eps || vtx->color != color)
				{
					continue;
				}
				if (fabsf(vtx->uv.x - uv.x) > eps || fabsf(vtx->uv.z - uv.z) > eps)
				{
					continue;
				}
				return idx[i];
			}
		}

		const s32 newIdx = (s32)out3d->vtx.size();
		out3d->vtx.push_back({ pos, uv, color });
		if (iCell != s_vertexMap.end())
		{
			iCell->second.push_back(newIdx);
		}
		else
		{
			s_vertexMap[key] = {};
			s_vertexMap[key].push_back(newIdx);
		}
		return newIdx;
	}

	s32 loadEditorObj3D(Obj3DSourceType type, Archive* archive, const char* filename, const char* filePath, const u32* palette, s32 palIndex, s32 id)
	{
		s_vertexMap.clear();
		if (!archive && !filename) { return -1; }

		WorkBuffer buffer;
		size_t len;

		if (archive)
		{
			if (!archive->openFile(filename))
			{
				return -1;
			}
			len = archive->getFileLength();
			if (!len)
			{
				archive->closeFile();
				return -1;
			}
			buffer.resize(len + 1);
			memset(buffer.data(), 0, len + 1);
			archive->readFile(buffer.data(), len);
			archive->closeFile();
		}
		else
		{
			FileStream file;
			if (!filePath || !file.open(filePath, Stream::MODE_READ))
			{
				return -1;
			}

			len = file.getSize();
			buffer.resize(len + 1);
			memset(buffer.data(), 0, len + 1);
			file.readBuffer(buffer.data(), len);
			file.close();
		}

		if (!len) { return false; }
		if (id < 0) { id = allocateObj3D(filename); }
		EditorObj3D* out3d = s_obj3DList[id];
		strcpy(out3d->name, filename);

		TFE_Parser parser;
		size_t bufferPos = 0;
		parser.init((char*)buffer.data(), len);
		const char* fileBuffer = (char*)buffer.data();
		parser.addCommentString("#");

		// For now just do what the original code does.
		// TODO: Make this more robust.
		const char* line = parser.readLine(bufferPos, true);
		if (!line) { return false; }

		f32 version;
		if (sscanf(line, "3DO %f", &version) != 1)
		{
			TFE_System::logWrite(LOG_ERROR, "Object3D_Load", "'%s' has no valid name.", filename);
			assert(0);
			return false;
		}
		if (version < 1.2f)
		{
			TFE_System::logWrite(LOG_ERROR, "Object3D_Load", "'%s' has inavlid version", filename);
			assert(0);
			return false;
		}

		line = parser.readLine(bufferPos, true);
		if (!line) { return false; }
		char name3do[256];
		if (sscanf(line, "3DONAME %s", name3do) != 1)
		{
			TFE_System::logWrite(LOG_ERROR, "Object3D_Load", "'%s' has no valid 3DONAME.", line);
			assert(0);
			return false;
		}
		strcpy(out3d->name, name3do);

		line = parser.readLine(bufferPos, true);
		if (!line) { return false; }
		s32 objectCount;
		if (sscanf(line, "OBJECTS %d", &objectCount) != 1)
		{
			TFE_System::logWrite(LOG_ERROR, "Object3D_Load", "'%s' has no objects.", filename);
			assert(0);
			return false;
		}

		line = parser.readLine(bufferPos, true);
		if (!line) { return false; }
		s32 vertexCount;
		if (sscanf(line, "VERTICES %d", &vertexCount) != 1)
		{
			TFE_System::logWrite(LOG_ERROR, "Object3D_Load", "'%s' has no vertices.", filename);
			assert(0);
			return false;
		}

		line = parser.readLine(bufferPos, true);
		if (!line) { return false; }
		s32 polygonCount;
		if (sscanf(line, "POLYGONS %d", &polygonCount) != 1)
		{
			TFE_System::logWrite(LOG_ERROR, "Object3D_Load", "'%s' has no polygons.", filename);
			assert(0);
			return false;
		}

		line = parser.readLine(bufferPos, true);
		if (!line) { return false; }
		char palette3do[32];
		if (sscanf(line, "PALETTE %s", palette3do) != 1)
		{
			TFE_System::logWrite(LOG_ERROR, "Object3D_Load", "'%s' has no PALETTE.", filename);
			assert(0);
			return false;
		}

		line = parser.readLine(bufferPos, true);
		if (!line) { return false; }
		s32 textureCount;
		if (sscanf(line, "TEXTURES %d", &textureCount) != 1)
		{
			TFE_System::logWrite(LOG_ERROR, "Object3D_Load", "'%s' has no valid TEXTURES entry.", filename);
			assert(0);
			return false;
		}

		// Load textures.
		std::vector<TextureGpu*> textures;
		if (textureCount)
		{
			textures.reserve(textureCount);

			for (s32 i = 0; i < textureCount; i++)
			{
				line = parser.readLine(bufferPos, true);
				if (!line) { return false; }

				char textureName[256];
				if (sscanf(line, " TEXTURE: %s ", textureName) != 1)
				{
					TFE_System::logWrite(LOG_WARNING, "Object3D_Load", "'%s' unable to parse TEXTURE: entry.", filename);
					strcpy(textureName, "default.bm");
				}

				TextureGpu* texGpu = nullptr;
				if (strcasecmp(textureName, "<NoTexture>"))
				{
					Asset* texAsset = AssetBrowser::findAsset(textureName, TYPE_TEXTURE);
					if (texAsset)
					{
						AssetHandle handle = AssetBrowser::loadAssetData(texAsset);
						EditorTexture* tex = (EditorTexture*)getAssetData(handle);
						if (tex)
						{
							texGpu = tex->frames[0];
						}
					}
				}
				textures.push_back(texGpu);
			}
		}

		bool nextLine = true;
		
		struct E3dPolygon
		{
			s32 count = 3;
			Vec3f pos[4] = { 0 };
			Vec3f nrm[4] = { 0 };
			Vec2f uv[4] = { 0 };
			u32 color = 0xffffffff;
			TextureGpu* tex = nullptr;
			u32 shading = PSHADE_FLAT;
		};

		std::vector<Vec3f> vtxPos;
		std::vector<Vec2f> vtxUv;
		std::vector<E3dPolygon> polyList;
		bool drawVertices = false;

		for (s32 i = 0; i < objectCount; i++)
		{
			vtxPos.clear();

			if (nextLine)
			{
				line = parser.readLine(bufferPos, true);
			}
			if (!line) { break; }
			nextLine = true;

			char objName[32];
			if (sscanf(line, " OBJECT %s", objName) != 1)
			{
				// Some of the models have fewer object entries than objectCount (bad data, "fixed" by the code).
				TFE_System::logWrite(LOG_WARNING, "Object3D_Load", "'%s' has too few objects, continuing anyway.", filename);
				break;
			}

			line = parser.readLine(bufferPos, true);
			if (!line) { return false; }
			s32 textureId;
			if (sscanf(line, " TEXTURE %d", &textureId) != 1)
			{
				TFE_System::logWrite(LOG_ERROR, "Object3D_Load", "'%s' unable to parse TEXTURE ID.", filename);
				assert(0);
				return false;
			}
			TextureGpu* texture = nullptr;
			if (textureId != -1 && textureCount > 0)
			{
				texture = textures[textureId];
			}

			line = parser.readLine(bufferPos, true);
			if (!line) { return false; }
			if (sscanf(line, " VERTICES %d", &vertexCount) != 1)
			{
				TFE_System::logWrite(LOG_ERROR, "Object3D_Load", "'%s' unable to parse object VERTICES entry.", filename);
				assert(0);
				return false;
			}

			while (1)
			{
				line = parser.readLine(bufferPos, true);
				if (!line)
				{
					nextLine = false;
					break;
				}

				s32 index;
				f32 x, y, z;
				if (sscanf(line, " %d: %f %f %f", &index, &x, &y, &z) != 4)
				{
					nextLine = false;
					break;
				}
				vtxPos.push_back({x, -y, z});
			}

			if (nextLine)
			{
				line = parser.readLine(bufferPos, true);
				if (!line) { return false; }
			}
			nextLine = true;

			s32 triangleCount = 0, quadCount = 0;
			s32 curPolyIndex = (s32)polyList.size();
			if (sscanf(line, " TRIANGLES %d", &triangleCount) == 1)
			{
				while (1)
				{
					line = parser.readLine(bufferPos, true);
					if (!line)
					{
						nextLine = false;
						break;
					}

					s32 num, a, b, c, color;
					char shading[32];
					if (sscanf(line, " %d: %d %d %d %d %s ", &num, &a, &b, &c, &color, shading) != 6)
					{
						nextLine = false;
						break;
					}

					E3dPolygon poly = {};
					poly.count = 3;
					poly.pos[0] = vtxPos[a];
					poly.pos[1] = vtxPos[b];
					poly.pos[2] = vtxPos[c];
					poly.color = color;
					poly.tex = texture;
					
					if (strcasecmp(shading, "FLAT") == 0)
					{
						poly.shading = PSHADE_FLAT;
					}
					else if (strcasecmp(shading, "VERTEX") == 0)
					{
						poly.shading = PSHADE_FLAT;
						drawVertices = true;
					}
					else if (strcasecmp(shading, "TEXTURE") == 0)
					{
						poly.shading = PSHADE_TEXTURE;
					}
					else if (strcasecmp(shading, "GOURAUD") == 0)
					{
						poly.shading = PSHADE_GOURAUD;
					}
					else if (strcasecmp(shading, "GOURTEX") == 0)
					{
						poly.shading = PSHADE_GOURAUD_TEXTURE;
					}
					else if (strcasecmp(shading, "PLANE") == 0)
					{
						poly.shading = PSHADE_PLANE;
					}
					else
					{
						poly.shading = texture ? PSHADE_TEXTURE : PSHADE_FLAT;
					}
					polyList.push_back(poly);
				}
			}
			else if (sscanf(line, " QUADS %d", &quadCount) == 1)
			{
				while (1)
				{
					line = parser.readLine(bufferPos, true);
					if (!line)
					{
						nextLine = false;
						break;
					}

					s32 num, a, b, c, d, color;
					char shading[32];
					if (sscanf(line, " %d: %d %d %d %d %d %s", &num, &a, &b, &c, &d, &color, shading) != 7)
					{
						nextLine = false;
						break;
					}

					E3dPolygon poly = {};
					poly.count = 4;
					poly.pos[0] = vtxPos[a];
					poly.pos[1] = vtxPos[b];
					poly.pos[2] = vtxPos[c];
					poly.pos[3] = vtxPos[d];
					poly.color = color;
					poly.tex = texture;

					if (strcasecmp(shading, "FLAT") == 0)
					{
						poly.shading = PSHADE_FLAT;
					}
					else if (strcasecmp(shading, "VERTEX") == 0)
					{
						poly.shading = PSHADE_FLAT;
						drawVertices = true;
					}
					else if (strcasecmp(shading, "TEXTURE") == 0)
					{
						poly.shading = PSHADE_TEXTURE;
					}
					else if (strcasecmp(shading, "GOURAUD") == 0)
					{
						poly.shading = PSHADE_GOURAUD;
					}
					else if (strcasecmp(shading, "GOURTEX") == 0)
					{
						poly.shading = PSHADE_GOURAUD_TEXTURE;
					}
					else if (strcasecmp(shading, "PLANE") == 0)
					{
						poly.shading = PSHADE_PLANE;
					}
					else
					{
						poly.shading = texture ? PSHADE_TEXTURE : PSHADE_FLAT;
					}
					polyList.push_back(poly);
				}
			}
			else  // No polygons
			{
				TFE_System::logWrite(LOG_ERROR, "Object3D_Load", "'%s' object %s has no polygons.", filename, objName);
				return false;
			}

			if (texture)
			{
				if (nextLine)
				{
					line = parser.readLine(bufferPos, true);
					if (!line) { return false; }
				}
				nextLine = true;

				s32 texVertexCount;
				if (sscanf(line, " TEXTURE VERTICES %d", &texVertexCount) != 1)
				{
					TFE_System::logWrite(LOG_ERROR, "Object3D_Load", "'%s' unable to parse TEXTURE VERTICES.", filename);
					assert(0);
					return false;
				}
				vtxUv.clear();

				nextLine = true;
				// Keep reading until the format no longer matches...
				for (s32 v = 0; ; v++)
				{
					line = parser.readLine(bufferPos, true);
					if (!line) { return false; }

					s32 num;
					f32 x, y;
					if (sscanf(line, " %d: %f %f", &num, &x, &y) != 3)
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

					vtxUv.push_back({ x, y });
				}

				if (nextLine)
				{
					line = parser.readLine(bufferPos, true);
					if (!line) { return false; }
				}
				nextLine = true;

				s32 texTriangleCount;
				s32 texQuadCount;
				const s32 uvCount = (s32)vtxUv.size();
				if (sscanf(line, " TEXTURE TRIANGLES %d", &texTriangleCount) == 1)
				{
					while (1)
					{
						line = parser.readLine(bufferPos, true);
						if (!line) { break; }

						s32 num, a, b, c;
						if (sscanf(line, " %d: %d %d %d", &num, &a, &b, &c) != 4)
						{
							nextLine = false;
							break;
						}
						// Handle bad data.
						a = std::min(a, uvCount-1);
						b = std::min(b, uvCount-1);
						c = std::min(c, uvCount-1);

						polyList[curPolyIndex].uv[0] = vtxUv[a];
						polyList[curPolyIndex].uv[1] = vtxUv[b];
						polyList[curPolyIndex].uv[2] = vtxUv[c];
						polyList[curPolyIndex].tex = texture;
						curPolyIndex++;
					}
				}
				else if (sscanf(line, " TEXTURE QUADS %d", &texQuadCount) == 1)
				{
					while (1)
					{
						line = parser.readLine(bufferPos, true);
						if (!line) { break; }

						s32 num, a, b, c, d;
						if (sscanf(line, " %d: %d %d %d %d", &num, &a, &b, &c, &d) != 5)
						{
							nextLine = false;
							break;
						}
						// Handle bad data.
						a = std::min(a, uvCount-1);
						b = std::min(b, uvCount-1);
						c = std::min(c, uvCount-1);
						d = std::min(d, uvCount-1);

						polyList[curPolyIndex].uv[0] = vtxUv[a];
						polyList[curPolyIndex].uv[1] = vtxUv[b];
						polyList[curPolyIndex].uv[2] = vtxUv[c];
						polyList[curPolyIndex].uv[3] = vtxUv[d];
						polyList[curPolyIndex].tex = texture;
						curPolyIndex++;
					}
				}
			}
		}

		// Process now that the polygons are complete.
		// Loop through polygons and create materials.
		const s32 polyCount = (s32)polyList.size();
		E3dPolygon* poly = polyList.data();

		// Sort polygons by material while creating materials.
		std::vector<std::vector<E3dPolygon*>> mtlPoly;
		for (s32 p = 0; p < polyCount; p++, poly++)
		{
			const s32 mtl = addMaterial(poly->shading, poly->tex, drawVertices, out3d);
			if (mtl >= mtlPoly.size())
			{
				mtlPoly.resize(mtl + 1);
			}
			mtlPoly[mtl].push_back(poly);
		}

		s32 indexOffset = 0;
		const s32 mtlCount = (s32)out3d->mtl.size();
		EditorMaterial* mtl = out3d->mtl.data();
		for (s32 m = 0; m < mtlCount; m++, mtl++)
		{
			s32 indexCount = 0;

			bool hasTex = mtl->texture != nullptr;
			mtl->idxStart = indexOffset;
			const s32 polyCount = (s32)mtlPoly[m].size();
			E3dPolygon** mtlPolyList = mtlPoly[m].data();
			for (s32 p = 0; p < polyCount; p++)
			{
				E3dPolygon* poly = mtlPolyList[p];
				s32 i0 = addVertex(poly->pos[0], poly->uv[0], hasTex ? 0xffffffff : palette[poly->color], out3d);
				s32 i1 = addVertex(poly->pos[1], poly->uv[1], hasTex ? 0xffffffff : palette[poly->color], out3d);
				s32 i2 = addVertex(poly->pos[2], poly->uv[2], hasTex ? 0xffffffff : palette[poly->color], out3d);
				if (poly->count == 3)
				{
					indexCount += 3;

					out3d->idx.push_back(i0);
					out3d->idx.push_back(i1);
					out3d->idx.push_back(i2);
				}
				else if (poly->count == 4)
				{
					s32 i3 = addVertex(poly->pos[3], poly->uv[3], hasTex ? 0xffffffff : palette[poly->color], out3d);
					indexCount += 6;

					out3d->idx.push_back(i0);
					out3d->idx.push_back(i1);
					out3d->idx.push_back(i2);

					out3d->idx.push_back(i0);
					out3d->idx.push_back(i2);
					out3d->idx.push_back(i3);
				}
			}
			mtl->idxCount = indexCount;
			indexOffset += indexCount;
		}

		// Compute the final bounds.
		const EditorVertex* vtx = out3d->vtx.data();
		vertexCount = (s32)out3d->vtx.size();
		out3d->bounds[0] = { FLT_MAX, FLT_MAX, FLT_MAX };
		out3d->bounds[1] = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
		for (s32 v = 1; v < vertexCount; v++)
		{
			out3d->bounds[0].x = std::min(out3d->bounds[0].x, vtx[v].pos.x);
			out3d->bounds[0].y = std::min(out3d->bounds[0].y, vtx[v].pos.y);
			out3d->bounds[0].z = std::min(out3d->bounds[0].z, vtx[v].pos.z);

			out3d->bounds[1].x = std::max(out3d->bounds[1].x, vtx[v].pos.x);
			out3d->bounds[1].y = std::max(out3d->bounds[1].y, vtx[v].pos.y);
			out3d->bounds[1].z = std::max(out3d->bounds[1].z, vtx[v].pos.z);
		}

		// Create GPU buffers.
		out3d->vtxGpu.create((u32)out3d->vtx.size(), sizeof(EditorVertex), c_editorModelAttrCount, c_editorModelAttrMapping, false, out3d->vtx.data());
		out3d->idxGpu.create((u32)out3d->idx.size(), sizeof(s32), false, out3d->idx.data());
		
		return id;
	}

	EditorObj3D* getObj3DData(u32 index)
	{
		if (index >= s_obj3DList.size()) { return nullptr; }
		return s_obj3DList[index];
	}

	// Matches the DF calculation.
	void compute3x3Rotation(Mat3* transform, f32 yaw, f32 pitch, f32 roll)
	{
		const f32 sinYaw = sinf(yaw);
		const f32 cosYaw = cosf(yaw);
		const f32 sinPch = sinf(pitch);
		const f32 cosPch = cosf(pitch);
		const f32 sinRol = sinf(roll);
		const f32 cosRol = cosf(roll);

		const f32 sRol_sPitch = sinRol * sinPch;
		const f32 cRol_sPitch = cosRol * sinPch;

		transform->data[0] = cosRol * cosYaw + sRol_sPitch * sinYaw;
		transform->data[1] = -sinRol * cosYaw + cRol_sPitch * sinYaw;
		transform->data[2] = cosPch * sinYaw;

		transform->data[3] = cosPch * sinRol;
		transform->data[4] = cosRol * cosPch;
		transform->data[5] = -sinPch;

		transform->data[6] = -cosRol * sinYaw + sRol_sPitch * cosYaw;
		transform->data[7] = sinRol * sinYaw + cRol_sPitch * cosYaw;
		transform->data[8] = cosPch * cosYaw;
	}
}