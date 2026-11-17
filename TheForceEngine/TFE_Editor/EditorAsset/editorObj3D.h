#pragma once
//////////////////////////////////////////////////////////////////////
// The Force Engine Editor
// A system built to view and edit Dark Forces data files.
// The viewing aspect needs to be put in place at the beginning
// in order to properly test elements in isolation without having
// to "play" the game as intended.
//////////////////////////////////////////////////////////////////////
#include <TFE_System/types.h>
#include <TFE_Archive/archive.h>
#include <TFE_Asset/modelAsset_jedi.h>
#include <TFE_RenderBackend/renderBackend.h>
#include <TFE_RenderBackend/vertexBuffer.h>
#include <TFE_RenderBackend/indexBuffer.h>

namespace TFE_Editor
{
	struct EditorVertex
	{
		Vec3f pos = { 0 };
		Vec2f uv = { 0 };
		u32 color = 0xffffffff;
	};

	struct EditorMaterial
	{
		TextureGpu* texture = nullptr;	// null = solid color.
		bool flatProj = false;
		u32 idxStart = 0;
		u32 idxCount = 0;
	};

	struct EditorObj3D
	{
		TextureGpu* thumbnail = nullptr;
		char name[64] = "";

		std::vector<EditorVertex> vtx;
		std::vector<u32> idx;
		std::vector<EditorMaterial> mtl;

		Vec3f bounds[2] = { 0 };

		VertexBuffer vtxGpu = {};
		IndexBuffer idxGpu = {};
	};

	enum Obj3DSourceType
	{
		OBJ3D_3DO,
		OBJ3D_COUNT
	};
		
	void freeCachedObj3D();
	void freeObj3D(const char* name);

	EditorObj3D* getObj3DData(u32 index);
	s32 loadEditorObj3D(Obj3DSourceType type, Archive* archive, const char* filename, const char* filePath, const u32* palette, s32 palIndex, s32 id = -1);
	void compute3x3Rotation(Mat3* transform, f32 yaw, f32 pitch, f32 roll);
}
