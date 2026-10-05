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
#include <TFE_Jedi/Renderer/rcommon.h>

#include <TFE_RenderBackend/renderBackend.h>
#include <TFE_RenderBackend/vertexBuffer.h>
#include <TFE_RenderBackend/indexBuffer.h>
#include <TFE_RenderBackend/shader.h>
#include <TFE_RenderBackend/shaderBuffer.h>
#include <TFE_RenderShared/texturePacker.h>

#include <TFE_Settings/settings.h>

#include <TFE_Asset/imageAsset.h>
#include <TFE_Memory/chunkedArray.h>

#include <map>

#define DEBUG_TEXTURE_ATLAS 0

namespace TFE_DarkForces
{
	// TODO: Make this accessible like the palette.
	extern u8* s_levelColorMap;
}

namespace TFE_Jedi
{
	struct TextureNode
	{
		TextureNode* child[2] = { 0 };	// Binary tree.
		Vec4ui rect = { 0 };
		void* tex = nullptr;
		s32 pageId = 0;
	};

	enum
	{
		TIGHT_FIT_PIXELS = 2,
		MAX_TEXTURE_COUNT = 16384,
		MAX_TEXTURE_PAGES = 16,
		FILTER_PADDING = 2,
		PALETTE_COUNT = 2,
		PALETTE_SIZE = 256,
		PALETTE_DEFAULT_IDX = 1,
		COLOR_INDEX_COUNT = 8,
	};
	static const f32 c_satLimit = 0.2f;

	static std::vector<TextureNode*> s_nodes;
	static TextureNode* s_root;
	static TexturePacker* s_texturePacker;
	static std::map<TextureData*, s32> s_textureDataMap;
	static std::map<WaxCell*, s32> s_waxDataMap;
	static std::vector<TextureInfo> s_texInfoPool;
	static std::vector<TextureInfo*> s_unpackedTextures[2];
	static ChunkedArray* s_nodePool = nullptr;
	static MemoryRegion* s_texturePackerRegion = nullptr;

	static s32 s_usedTexels = 0;
	static s32 s_totalTexels = 0;
	static s32 s_nodePoolIndex = 0;
	static s32 s_unpackedBuffer = 0;
	static s32 s_currentPage = 0;
	static AssetPool s_assetPool;

	static u32 s_conversionPal[PALETTE_COUNT][PALETTE_SIZE];

	// Global Packer
	static const char* c_globalTexturePackerName = "GameTextures";
	static const s32   c_globalPageWidth = 4096;
	static const s32   c_globalPageReserveCount = 1;
	static TexturePacker* s_globalTexturePacker = nullptr;

	static s32 s_colorIndexStart = -1;
		
	TextureNode* allocateNode();
	u8* getWritePointer(s32 page, s32 x, s32 y, u32 mipLevel = 0);

#if DEBUG_TEXTURE_ATLAS
	void debug_writeOutAtlas();
#endif

#define CONV_6bitTo8bit_Tex(x) (((x)<<2) | ((x)>>4))
	void texturepacker_setConversionPalette(s32 index, s32 bpp, const u8* input)
	{
		if (index < 0 || index >= PALETTE_COUNT)
		{
			TFE_System::logWrite(LOG_ERROR, "TexturePacker", "Invalid palette index %d.", index);
			return;
		}

		u32* output = s_conversionPal[index];
		const u8* srcColor = input;
		if (bpp == 6)
		{
			for (s32 i = 0; i < PALETTE_SIZE; i++, output++, srcColor += 3)
			{
				*output = CONV_6bitTo8bit_Tex(srcColor[0]) | (CONV_6bitTo8bit_Tex(srcColor[1]) << 8u) | (CONV_6bitTo8bit_Tex(srcColor[2]) << 16u) | (0xffu << 24u);
			}
		}
		else if (bpp == 8)
		{
			for (s32 i = 0; i < PALETTE_SIZE; i++, output++, srcColor += 3)
			{
				*output = srcColor[0] | (srcColor[1] << 8u) | (srcColor[2] << 16u) | (0xffu << 24u);
			}
		}
	}

	const u32* getPalette(s32 index)
	{
		if (index < 0 || index >= PALETTE_COUNT)
		{
			TFE_System::logWrite(LOG_ERROR, "TexturePacker", "Invalid palette index %d.", index);
			return nullptr;
		}
		return s_conversionPal[index];
	}

	TexturePage* allocateTexturePage(u32 pageSize)
	{
		TexturePage* page = (TexturePage*)malloc(sizeof(TexturePage));
		memset(page, 0, sizeof(TexturePage));
		page->backingMemory = (u8*)malloc(pageSize);
		memset(page->backingMemory, 0, pageSize);
		page->root = nullptr;
		page->textureCount = 0;
		return page;
	}
				
	// Initialize the texture packer once, it is persistent across levels.
	TexturePacker* texturepacker_init(const char* name, s32 width, s32 height)
	{
		TexturePacker* texturePacker = (TexturePacker*)malloc(sizeof(TexturePacker));
		if (!texturePacker) { return nullptr; }

		if (!s_nodePool)
		{
			s_texturePackerRegion = TFE_Memory::region_create("game", 8 * 1024 * 1024);
			s_nodePool = TFE_Memory::createChunkedArray(sizeof(TextureNode), 256, 1, s_texturePackerRegion);
		}
				
		// Initialize with one page.
		texturePacker->pageCount = 1;
		texturePacker->reservedPages = 0;
		texturePacker->reservedTexturesPacked = 0;
		texturePacker->trueColor = (TFE_Settings::getGraphicsSettings()->colorMode == COLORMODE_TRUE_COLOR);
		texturePacker->bytesPerTexel = texturePacker->trueColor ? 4 : 1;
		texturePacker->pages = (TexturePage**)malloc(sizeof(TexturePage*) * MAX_TEXTURE_PAGES);
		if (!texturePacker->pages)
		{
			texturepacker_destroy(texturePacker);
			return nullptr;
		}
		
		// Compute the desired mip count.
		u32 mipCount = 1;
		if (TFE_Settings::getGraphicsSettings()->useMipmapping && texturePacker->trueColor)
		{
			// start from 2x2 (mip 5) give a 64x64 texture.
			mipCount = 6;
		}

		texturePacker->width = width;
		texturePacker->height = height;
		texturePacker->mipCount = mipCount;
		texturePacker->mipPadding = 1 << mipCount;
		texturePacker->texture = nullptr;

		memset(texturePacker->mipOffset, 0, sizeof(u32) * 8);
		u32 offset = 0;
		u32 w = width, h = height;
		texturePacker->pageSize = 0;
		for (u32 m = 0; m < texturePacker->mipCount; m++)
		{
			texturePacker->mipOffset[m] = offset;
			texturePacker->pageSize += (w * h);
			offset += w * h;
			w >>= 1;
			h >>= 1;
		}
		texturePacker->pageSize *= texturePacker->bytesPerTexel;
		texturePacker->pages[0] = allocateTexturePage(texturePacker->pageSize);

		texturePacker->textureTable = (Vec4i*)malloc(sizeof(Vec4i) * MAX_TEXTURE_COUNT);	// 256Kb (count can be up to 64K).
		if (!texturePacker->textureTable)
		{
			texturepacker_destroy(texturePacker);
			return nullptr;
		}

		ShaderBufferDef textureTableDef =
		{
			4,				// 1, 2, 4 channels (R, RG, RGBA)
			sizeof(s32),	// 1, 2, 4 bytes (u8; s16,u16; s32,u32,f32)
			BUF_CHANNEL_INT
		};
		texturePacker->textureTableGPU.create(MAX_TEXTURE_COUNT, textureTableDef, true, nullptr);

		strncpy(texturePacker->name, name, 64);
		return texturePacker;
	}

	// Free memory and GPU buffers. Note: GPU textures need to be persistent, so the level allocator will not be used.
	void texturepacker_destroy(TexturePacker* texturePacker)
	{
		if (!texturePacker) { return; }

		TFE_RenderBackend::freeTexture(texturePacker->texture);
		texturePacker->textureTableGPU.destroy();
		free(texturePacker->textureTable);
		for (s32 p = 0; p < texturePacker->pageCount; p++)
		{
			free(texturePacker->pages[p]->backingMemory);
		}
		free(texturePacker->pages);
		free(texturePacker);

		TFE_Memory::region_destroy(s_texturePackerRegion);
		s_texturePackerRegion = nullptr;
	}
		
	void texturepacker_reserveCommitedPages(TexturePacker* texturePacker)
	{
		texturePacker->reservedPages = texturePacker->pageCount;
		texturePacker->reservedTexturesPacked = texturePacker->texturesPacked;
	}

	bool texturepacker_hasReservedPages(TexturePacker* texturePacker)
	{
		return texturePacker->reservedPages > 0;
	}

	void texturepacker_discardUnreservedPages(TexturePacker* texturePacker)
	{
		if (!texturepacker_hasReservedPages(texturePacker)) { return; }
		// Clear pages.
		for (s32 p = 0; p < s_texturePacker->reservedPages; p++)
		{
			s_texturePacker->pages[p]->root = nullptr;
		}
		for (s32 p = s_texturePacker->reservedPages; p < s_texturePacker->pageCount; p++)
		{
			s_texturePacker->pages[p]->root = nullptr;
			s_texturePacker->pages[p]->textureCount = 0;
		}

		TFE_Memory::chunkedArrayClear(s_nodePool);
		s_textureDataMap.clear();
		s_waxDataMap.clear();
		s_texInfoPool.clear();

		// Insert the parent that covers all of the available space.
		s_texturePacker->pageCount = s_texturePacker->reservedPages;
		s_texturePacker->texturesPacked = s_texturePacker->reservedTexturesPacked;
	}
		
	bool textureFitsInNode(TextureNode* cur, u32 width, u32 height)
	{
		return width <= cur->rect.z && height <= cur->rect.w;
	}

	bool textureTightFit(TextureNode* cur, u32 width, u32 height)
	{
		return (cur->rect.z - width <= TIGHT_FIT_PIXELS) && (cur->rect.w - height <= TIGHT_FIT_PIXELS);
	}

	TextureNode* insertNode(TextureNode* cur, void* tex, u32 width, u32 height)
	{
		TextureNode* newNode = nullptr;
		if (!s_root)
		{
			newNode = allocateNode();
			newNode->child[0] = nullptr;
			newNode->child[1] = nullptr;
			newNode->rect = {0u, 0u, (u32)s_texturePacker->width, (u32)s_texturePacker->height};
			newNode->tex  = tex;
			s_root = newNode;

			s_nodes.push_back(newNode);
			return newNode;
		}

		// We are not a leaf...
		if (cur->child[0])
		{
			// Try inserting left.
			newNode = insertNode(cur->child[0], tex, width, height);
			if (newNode) { return newNode; }

			// Then right.
			return insertNode(cur->child[1], tex, width, height);
		}
		else // Leaf node.
		{
			// If the node is already full...
			if (cur->tex) { return nullptr; }

			// Check to see if the texture fits.
			if (!textureFitsInNode(cur, width, height)) { return nullptr; }

			// Check to see if it is a tight fit.
			if (textureTightFit(cur, width, height))
			{
				cur->tex = tex;
				return cur;
			}

			// Split the node.
			cur->child[0] = allocateNode();
			cur->child[1] = allocateNode();

			// Push nodes so they can be deleted later.
			s_nodes.push_back(cur->child[0]);
			s_nodes.push_back(cur->child[1]);
			
			// Binary split along the longest axis.
			const s32 dw = cur->rect.z - width;
			const s32 dh = cur->rect.w - height;
			if (dw > dh)
			{
				cur->child[0]->rect = { cur->rect.x, cur->rect.y, width, cur->rect.w };
				cur->child[1]->rect = { cur->rect.x + width, cur->rect.y, cur->rect.z - width, cur->rect.w };
			}
			else
			{
				cur->child[0]->rect = { cur->rect.x, cur->rect.y, cur->rect.z, height };
				cur->child[1]->rect = { cur->rect.x, cur->rect.y + height, cur->rect.z, cur->rect.w - height };
			}

			// Then insert into the first child.
			return insertNode(cur->child[0], tex, width, height);
		}
	}

	// For now just hack it...
	// TODO: Override texture...
	void handleAlpha8Bit(u8 palIndex, u32* output)
	{
		u8 alpha = 0x7f;
		if (palIndex == 0)
		{
			alpha = 0x00;
		}
		else if (palIndex < 32)
		{
			alpha = 0xff;
		}

		(*output) &= 0x00ffffff;
		(*output) |= (alpha << 24u);
	}

	u8* getWritePointer(s32 page, s32 x, s32 y, u32 mipLevel)
	{
		const u32 mipClamped = min(mipLevel, u32(s_texturePacker->mipCount - 1));
		const u32 w = s_texturePacker->width >> mipClamped;
		x >>= mipClamped;
		y >>= mipClamped;

		const u32 addr = s_texturePacker->mipOffset[mipClamped] + y*w + x;
		return &s_texturePacker->pages[page]->backingMemory[addr * s_texturePacker->bytesPerTexel];
	}

	void generateMipmap(const u32* source, u32* output, s32 w, s32 h, s32 stride)
	{
		s32 wDst = w >> 1;
		s32 hDst = h >> 1;
		s32 strideDst = stride >> 1;

		for (s32 y = 0; y < hDst; y++)
		{
			const s32 srcY = y * 2;
			const s32 dstY = y;

			const u32* src = &source[srcY * stride];
			u32* dst = &output[dstY * strideDst];
			for (s32 x = 0; x < wDst; x++)
			{
				const s32 srcX = x * 2;
				const s32 dstX = x;

				u32 c0 = src[srcX];
				u32 c1 = src[srcX + 1];
				u32 c2 = src[srcX + stride];
				u32 c3 = src[srcX + 1 + stride];

				u32 r[4] = { c0 & 0xff, c1 & 0xff, c2 & 0xff, c3 & 0xff };
				u32 g[4] = { (c0>>8) & 0xff, (c1>>8) & 0xff, (c2>>8) & 0xff, (c3>>8) & 0xff };
				u32 b[4] = { (c0>>16) & 0xff, (c1>>16) & 0xff, (c2>>16) & 0xff, (c3>>16) & 0xff };
				u32 a[4] = { (c0>>24) & 0xff, (c1>>24) & 0xff, (c2>>24) & 0xff, (c3>>24) & 0xff };

				u32 dr = (r[0] + r[1] + r[2] + r[3]) >> 2;
				u32 dg = (g[0] + g[1] + g[2] + g[3]) >> 2;
				u32 db = (b[0] + b[1] + b[2] + b[3]) >> 2;
				u32 da = (a[0] + a[1] + a[2] + a[3]) >> 2;

				dst[dstX] = dr | (dg << 8) | (db << 16) | (da << 24);
			}
		}
	}

	f32 getSaturation(u32 color)
	{
		if ((color & 0x00ffffff) == 0) { return 0.0f; }

		const f32 scale = 1.0f / 255.0f;
		const f32 r = f32(color & 0xff) * scale;
		const f32 g = f32((color >> 8) & 0xff) * scale;
		const f32 b = f32((color >> 16) & 0xff) * scale;

		const f32 maxc = max(r, max(g, b));
		const f32 minc = min(r, min(g, b));
		const f32 lum = maxc - minc;

		return (lum < 0.5f) ? lum / (maxc + minc) : lum / (2.0f - maxc - minc);
	}

	f64 addMultiplier(u32 cFull, u32 cHalf, f64* accum)
	{
		if ((cFull & 0x00ffffff) == 0) { return 0.0; }

		u32 r[2] = { cFull & 0xff, cHalf & 0xff };
		u32 g[2] = { (cFull >> 8) & 0xff, (cHalf >> 8) & 0xff };
		u32 b[2] = { (cFull >> 16) & 0xff, (cHalf >> 16) & 0xff };

		f64 scale = 1.0 / 255.0;
		f64 rFull = f64(r[0]) * scale;
		f64 gFull = f64(g[0]) * scale;
		f64 bFull = f64(b[0]) * scale;
		f64 rHalf = f64(r[1]) * scale;
		f64 gHalf = f64(g[1]) * scale;
		f64 bHalf = f64(b[1]) * scale;

		f64 mR = rFull > FLT_EPSILON ? rHalf / rFull : -1.0;
		f64 mG = gFull > FLT_EPSILON ? gHalf / gFull : -1.0;
		f64 mB = bFull > FLT_EPSILON ? bHalf / bFull : -1.0;

		accum[0] += mR > FLT_EPSILON ? mR : 0.0;
		accum[1] += mG > FLT_EPSILON ? mG : 0.0;
		accum[2] += mB > FLT_EPSILON ? mB : 0.0;
		return 1.0;
	}

	f64 addMultiplierHd(u32 cFull, const f64* cHdAveColor, f64* accum)
	{
		if ((cFull & 0x00ffffff) == 0) { return 0.0; }

		u32 r = cFull & 0xff;
		u32 g = (cFull >> 8) & 0xff;
		u32 b = (cFull >> 16) & 0xff;

		f64 scale = 1.0 / 255.0;
		f64 rFull = f64(r) * scale;
		f64 gFull = f64(g) * scale;
		f64 bFull = f64(b) * scale;
		f64 rHd = cHdAveColor[0];
		f64 gHd = cHdAveColor[1];
		f64 bHd = cHdAveColor[2];

		f64 mR = rFull > FLT_EPSILON ? rHd / rFull : -1.0;
		f64 mG = gFull > FLT_EPSILON ? gHd / gFull : -1.0;
		f64 mB = bFull > FLT_EPSILON ? bHd / bFull : -1.0;

		accum[0] += mR > FLT_EPSILON ? mR : 0.0;
		accum[1] += mG > FLT_EPSILON ? mG : 0.0;
		accum[2] += mB > FLT_EPSILON ? mB : 0.0;
		return 1.0;
	}

	void getAverageColorHd(const u32* srcImageHd, s32 x, s32 y, s32 stride, s32 scaleFactor, f64* aveColor)
	{
		s32 pixelCount = scaleFactor * scaleFactor;
		s32 startIndex = y * stride + x;
		aveColor[0] = 0;
		aveColor[1] = 0;
		aveColor[2] = 0;

		const f64 scale = 1.0 / 255.0;
		for (s32 i = 0; i < pixelCount; i++)
		{
			const s32 index = (i / scaleFactor) * stride + (i % scaleFactor) + startIndex;
			aveColor[0] += f64(srcImageHd[index] & 0xff) * scale;
			aveColor[1] += f64((srcImageHd[index] >> 8) & 0xff) * scale;
			aveColor[2] += f64((srcImageHd[index] >> 16) & 0xff) * scale;
		}
		const f64 rcpCount = 1.0 / f64(pixelCount);
		aveColor[0] *= rcpCount;
		aveColor[1] *= rcpCount;
		aveColor[2] *= rcpCount;
	}

	// TODO: This isn't working, it has been shelved until the rest of the support is in for HD assets.
	Vec3f colorMatch(const TextureData* texData, const TextureData* hdSrc, const u32* srcImageHd)
	{
		Vec3f colorScale = { 1.0f, 1.0f, 1.0f };

		const u8* srcImage = texData->image;
		const s32 scaleFactor = max(1, hdSrc->scaleFactor);
		if (!srcImage || !srcImageHd || (texData->flags & INDEXED) || (texData->flags & ALWAYS_FULLBRIGHT)) { return colorScale; }
		
		const u32* pal = getPalette(texData->palIndex);
		// TODO: For some levels this should be 30.
		const u8* remap = &TFE_DarkForces::s_levelColorMap[31 << 8];
		const u8* remapHalf = &TFE_DarkForces::s_levelColorMap[16 << 8];
		const s32 wHd = texData->width  * scaleFactor;
		const s32 hHd = texData->height * scaleFactor;
						
		f64 accum[3] = { 0.0 };
		f64 accumCount = 0.0;
		s32 grayScaleCount = 0;
		s32 grayScaleCountHalf = 0;
		s32 totalCount = 0;
		for (s32 y = 0; y < texData->height; y++)
		{
			for (s32 x = 0; x < texData->width; x++)
			{
				f64 aveColor[3];
				getAverageColorHd(srcImageHd, x, y, wHd, scaleFactor, aveColor);

				u8 palIndex = srcImage[x*texData->height + y];
				u32 color = palIndex == 0 ? 0u : pal[remap[palIndex]];
				f32 sat = getSaturation(color);
				if (sat < c_satLimit)
				{
					grayScaleCount++;
				}
				totalCount++;

				// Also figure out the average multiplier.
				u32 halfValue = palIndex == 0 ? 0u : pal[remapHalf[palIndex]];
				accumCount += addMultiplierHd(color, aveColor, accum);

				sat = getSaturation(halfValue);
				if (sat < c_satLimit)
				{
					grayScaleCountHalf++;
				}
			}
		}

		if (accumCount > 0.0)
		{
			// Try to guess at which textures have areas that should not be tinted.
			//if (grayScaleCount <= totalCount / 4 || grayScaleCountHalf <= 3 * totalCount / 8)
			{
				const f64 scale = 1.0 / max(accum[0], max(accum[1], accum[2]));
				accum[0] *= scale;
				accum[1] *= scale;
				accum[2] *= scale;

				colorScale = { f32(accum[0]), f32(accum[1]), f32(accum[2]) };
			}
		}
		return colorScale;
	}

	u32 scaleColor(u32 inColor, Vec3f colorScale)
	{
		const f32 r = f32(inColor & 0xff) * colorScale.x;
		const f32 g = f32((inColor >> 8) & 0xff) * colorScale.y;
		const f32 b = f32((inColor >> 16) & 0xff) * colorScale.z;

		const u32 ir = min(u32(r), 255u);
		const u32 ig = min(u32(g), 255u);
		const u32 ib = min(u32(b), 255u);
		return (inColor & 0xff000000) | (ir) | (ig << 8) | (ib << 16);
	}

	// Fixup the alpha/emissive encoding to match TFE expectations.
	// TODO: Add an option to enable variable emissive instead of all or nothing.
	u32 fixupAlphaForHdTexture(u32 color)
	{
		u32 alpha = color >> 24u;
		if (alpha == 0xffu)
		{
			alpha = 0x7fu;
		}
		else if (alpha > 0u)
		{
			alpha = 0xffu;
		}
		color &= 0x00ffffffu;
		color |= (alpha << 24u);
		return color;
	}

	void copyHdTrueColorTexture(const TextureData* texData, s32 scaleFactor, const u32* srcImageHd, s32 frameIndex, s32 paddingX, s32 paddingY, s32 offsetX, s32 offsetY, u32* output)
	{
		// TODO: Optional color matching between the HD texture and original to help
		// make levels like Gromas Mines look correct.
		const s32 w = texData->width  * scaleFactor;
		const s32 h = texData->height * scaleFactor;
		const s32 srcPixels = w * h;
		const s32 dstStrideInTexels = s_texturePacker->width;
		if (srcImageHd && w > 0 && h > 0)
		{
			srcImageHd += frameIndex * srcPixels;
			// TODO:
			//Vec3f colorScale = colorMatch(texData, hdSrc, (u32*)srcImage);

			for (s32 y = 0; y < h + paddingY; y++, output += dstStrideInTexels)
			{
				const s32 ySrc = (y - offsetY + h) % h;
				for (s32 x = 0; x < w + paddingX; x++)
				{
					const s32 xSrc = (x - offsetX + w) % w;
					output[x] = srcImageHd[ySrc*w + xSrc];
					// TODO:
					//output[x] = scaleColor(output[x], colorScale);
					output[x] = fixupAlphaForHdTexture(output[x]);
				}
			}
		}
		else
		{
			const s32 srcStride = w * 4;
			for (s32 y = 0; y < h + paddingY; y++, output += dstStrideInTexels)
			{
				memset(output, 0, srcStride);
			}
		}
	}

	void copy8BitToTrueColorTexture(const TextureData* texData, const u8* srcImage, s32 paddingX, s32 paddingY, s32 offsetX, s32 offsetY, u32* output, Vec3f& halfTint)
	{
		if (!TFE_DarkForces::s_levelColorMap) { return; }

		const s32 w = texData->width;
		const s32 h = texData->height;
		const s32 dstStrideInTexels = s_texturePacker->width;
		const u32* pal = getPalette(texData->palIndex);
		// TODO: For some levels this should be 30.
		const u8* remap = &TFE_DarkForces::s_levelColorMap[31 << 8];
		const u8* remapHalf = &TFE_DarkForces::s_levelColorMap[16 << 8];

		f64 accum[3] = { 0.0 };
		f64 accumCount = 0.0;
		s32 grayScaleCount = 0;
		s32 grayScaleCountHalf = 0;
		s32 totalCount = 0;

		if (srcImage && w > 0 && h > 0)
		{
			for (s32 y = 0; y < texData->height + paddingY; y++, output += dstStrideInTexels)
			{
				const s32 ySrc = (y - offsetY + h) % h;
				for (s32 x = 0; x < texData->width + paddingX; x++)
				{
					const s32 xSrc = (x - offsetX + w) % w;

					u8 palIndex = srcImage ? srcImage[xSrc*texData->height + ySrc] : 0;
					if (texData->flags & INDEXED)
					{
						output[x] = palIndex == 0 ? 0u : pal[palIndex];
						if (palIndex >= s_colorIndexStart && palIndex < s_colorIndexStart + COLOR_INDEX_COUNT)
						{
							s32 index = palIndex - s_colorIndexStart;
							// set it half way inside the buckets.
							s32 alpha = 128 + index * 16 + 8;
							output[x] &= 0x00ffffff;
							output[x] |= (alpha << 24);
						}
					}
					else if (texData->flags & ALWAYS_FULLBRIGHT)
					{
						output[x] = palIndex == 0 ? 0u : pal[palIndex];
					}
					else
					{
						output[x] = palIndex == 0 ? 0u : pal[remap[palIndex]];
						f32 sat = getSaturation(output[x]);
						if (sat < c_satLimit)
						{
							grayScaleCount++;
						}
						totalCount++;

						// Also figure out the average multiplier.
						u32 halfValue = palIndex == 0 ? 0u : pal[remapHalf[palIndex]];
						accumCount += addMultiplier(output[x], halfValue, accum);

						sat = getSaturation(halfValue);
						if (sat < c_satLimit)
						{
							grayScaleCountHalf++;
						}
					}

					if (!(texData->flags & INDEXED))
					{
						handleAlpha8Bit(palIndex, &output[x]);
					}
				}
			}
		}
		else
		{
			const s32 srcStride = w * 4;
			for (s32 y = 0; y < h + paddingY; y++, output += dstStrideInTexels)
			{
				memset(output, 0, srcStride);
			}
		}

		if (accumCount > 0.0)
		{
			// Try to guess at which textures have areas that should not be tinted.
			if (grayScaleCount > totalCount / 4 && grayScaleCountHalf > 3 * totalCount / 8)
			{
				halfTint = { 1.0f, 1.0f, 1.0f };
			}
			else
			{
				const f64 scale = 1.0 / max(accum[0], max(accum[1], accum[2]));
				accum[0] *= scale;
				accum[1] *= scale;
				accum[2] *= scale;

				halfTint = { f32(accum[0]), f32(accum[1]), f32(accum[2]) };
			}
		}
	}

	void generateTrueColorMips(const TextureNode* node, const TextureData* texData, s32 scaleFactor, s32 paddingX, s32 paddingY, s32 mipCount, u32* output)
	{
		const u32* source = (u32*)getWritePointer(s_currentPage, node->rect.x, node->rect.y, 0);
		u32 w = texData->width  * scaleFactor + paddingX;
		u32 h = texData->height * scaleFactor + paddingY;
		u32 stride = s_texturePacker->width;
		for (s32 m = 1; m < mipCount; m++)
		{
			output = (u32*)getWritePointer(s_currentPage, node->rect.x, node->rect.y, m);
			generateMipmap(source, output, w, h, stride);

			stride >>= 1;
			w >>= 1;
			h >>= 1;
			source = output;
		}
	}

	void copy8BitTo8BitTexture(const TextureData* texData, const u8* srcImage, u8* output)
	{
		const s32 outStride = s_texturePacker->width;
		const s32 w = texData->width;
		const s32 h = texData->height;
		for (s32 y = 0; y < h; y++, output += outStride)
		{
			for (s32 x = 0; x < w; x++)
			{
				output[x] = srcImage[x*h + y];
			}
		}
	}

	void packNode(const TextureNode* node, const TextureData* texData, Vec4i* tableEntry, s32 paddingX, s32 paddingY, s32 mipCount, const TextureData* hdSrc, s32 frameIndex)
	{
		// Copy the texture into place.
		const s32 offsetX = paddingX / 2;
		const s32 offsetY = paddingY / 2;
		const bool isHdTex = hdSrc && hdSrc->hdAssetData;
		const u8* srcImage = texData->image;
		const s32 scaleFactor = isHdTex ? hdSrc->scaleFactor : 1;

		Vec3f halfTint = { 1.0f, 1.0f, 1.0f };
		if (s_texturePacker->trueColor)
		{
			u32* output = (u32*)getWritePointer(s_currentPage, node->rect.x, node->rect.y, 0);
			if (isHdTex)
			{
				const u32* srcImageHd = (u32*)hdSrc->hdAssetData;
				copyHdTrueColorTexture(texData, scaleFactor, srcImageHd, frameIndex, paddingX, paddingY, offsetX, offsetY, output);
			}
			else
			{
				copy8BitToTrueColorTexture(texData, srcImage, paddingX, paddingY, offsetX, offsetY, output, halfTint);
			}
			generateTrueColorMips(node, texData, scaleFactor, paddingX, paddingY, mipCount, output);
		}
		else
		{
			u8* output = getWritePointer(s_currentPage, node->rect.x, node->rect.y, 0);
			copy8BitTo8BitTexture(texData, srcImage, output);
		}
		s_usedTexels += texData->width * texData->height;

		// Copy the mapping into the texture table.
		tableEntry->x = (s32)node->rect.x + offsetX;
		tableEntry->y = (s32)node->rect.y + offsetY;
		tableEntry->z = (s32)texData->width  * scaleFactor;
		tableEntry->w = (s32)texData->height * scaleFactor;

		// Page the page index into the x offset.
		tableEntry->x |= (s_currentPage << 12);
		tableEntry->y |= (scaleFactor << 12);

		// Half color tint packed.
		const s32 r = s32(halfTint.x * 255.0);
		const s32 g = s32(halfTint.y * 255.0);
		const s32 b = s32(halfTint.z * 255.0);
		tableEntry->z |= ((r << 15) | (g << 23));
		tableEntry->w |= (b << 15);
	}

	void packNodeDeltaTex(const TextureNode* node, const TextureData* texData, Vec4i* tableEntry, s32 paddingX, s32 paddingY)
	{
		// Copy the texture into place.
		s32 offsetX = paddingX / 2;
		s32 offsetY = paddingY / 2;
		const u8* srcImage = texData->image;
		if (s_texturePacker->trueColor)
		{
			const u32* pal = getPalette(texData->palIndex);

			u32* output = (u32*)getWritePointer(s_currentPage, node->rect.x, node->rect.y, 0);
			for (s32 y = 0; y < texData->height + paddingY; y++, output += s_texturePacker->width)
			{
				const s32 ySrc = y - offsetY;
				for (s32 x = 0; x < texData->width + paddingX; x++)
				{
					const s32 xSrc = x - offsetX;
					const bool outside = ySrc < 0 || xSrc < 0 || ySrc >= texData->height || xSrc >= texData->width;
					u8 palIndex = outside ? 0 : srcImage[(texData->height - ySrc - 1)*texData->width + xSrc];
					output[x] = palIndex == 0 ? 0u : pal[palIndex];

					handleAlpha8Bit(palIndex, &output[x]);
				}
			}
		}
		else
		{
			u8* output = getWritePointer(s_currentPage, node->rect.x, node->rect.y, 0);
			for (s32 y = 0; y < texData->height; y++, output += s_texturePacker->width)
			{
				for (s32 x = 0; x < texData->width; x++)
				{
					output[x] = srcImage[(texData->height - y - 1)*texData->width + x];
				}
			}
		}
		s_usedTexels += texData->width * texData->height;

		// Copy the mapping into the texture table.
		tableEntry->x = (s32)node->rect.x + offsetX;
		tableEntry->y = (s32)node->rect.y + offsetY;
		tableEntry->z = (s32)texData->width;
		tableEntry->w = (s32)texData->height;

		// Page the page index into the x offset.
		s32 scaleFactor = 1;
		tableEntry->x |= (s_currentPage << 12);
		tableEntry->y |= (scaleFactor << 12);
	}
		
	void packNodeCell(const TextureNode* node, const void* basePtr, const WaxCell* cell, const HdWax* hdWax, Vec4i* tableEntry, s32 paddingX, s32 paddingY)
	{
		// Copy the texture into place.
		s32 offsetX = paddingX / 2;
		s32 offsetY = paddingY / 2;

		const s32 compressed = hdWax ? 0 : cell->compressed;
		u8* imageData = hdWax ? (u8*)hdWax->cells[cell->id].data : (u8*)cell + sizeof(WaxCell);
		u8* image = (compressed == 1) ? imageData + (cell->sizeX * sizeof(u32)) : imageData;

		s32 scaleFactor = hdWax ? 2 : 1;	// TODO: Hardcoded.
		s32 w = cell->sizeX * scaleFactor;
		s32 h = cell->sizeY * scaleFactor;
		
		u8 columnWorkBuffer[WAX_DECOMPRESS_SIZE];
		const u32* columnOffset = (u32*)((u8*)basePtr + cell->columnOffset);
		if (s_texturePacker->trueColor)
		{
			const u32* pal = getPalette(PALETTE_DEFAULT_IDX);
			const u8* remap = &TFE_DarkForces::s_levelColorMap[31 << 8];

			u32* output = (u32*)getWritePointer(s_currentPage, node->rect.x, node->rect.y, 0);

			for (s32 x = 0; x < w + paddingX; x++)
			{
				const s32 xSrc = x - offsetX;
				if (xSrc < 0 || xSrc >= w)
				{
					for (s32 y = 0; y < h + paddingY; y++)
					{
						output[y*s_texturePacker->width + x] = 0;
					}
				}
				else if (hdWax)
				{
					u32* imageDataHd = (u32*)imageData;
					for (s32 y = 0; y < h + paddingY; y++)
					{
						const s32 ySrc = y - offsetY;
						const bool outside = ySrc < 0 || ySrc >= h;
						const u32 outAddr = y * s_texturePacker->width + x;
						output[outAddr] = outside ? 0 : fixupAlphaForHdTexture(imageDataHd[(h-ySrc-1)*w + w-xSrc-1]);
					}
				}
				else
				{
					u8* column = (u8*)image + columnOffset[xSrc];
					if (compressed)
					{
						const u8* colPtr = (u8*)cell + columnOffset[xSrc];
						sprite_decompressColumn(colPtr, columnWorkBuffer, cell->sizeY);
						column = columnWorkBuffer;
					}

					for (s32 y = 0; y < h + paddingY; y++)
					{
						const s32 ySrc = y - offsetY;
						bool outside = ySrc < 0 || ySrc >= h;
						const u8 palIndex = (outside || column[ySrc] == 0) ? 0u : column[ySrc];
						const u32 addr = y * s_texturePacker->width + x;
						if (s_assetPool == POOL_LEVEL)
						{
							output[addr] = palIndex == 0 ? 0u : pal[remap[palIndex]];
						}
						else
						{
							output[addr] = palIndex == 0 ? 0u : pal[palIndex];
						}

						handleAlpha8Bit(palIndex, &output[addr]);
					}
				}
			}
		}
		else
		{
			u8* output = getWritePointer(s_currentPage, node->rect.x, node->rect.y, 0);
			for (s32 x = 0; x < w; x++)
			{
				u8* column = (u8*)image + columnOffset[x];
				if (compressed)
				{
					const u8* colPtr = (u8*)cell + columnOffset[x];
					sprite_decompressColumn(colPtr, columnWorkBuffer, h);
					column = columnWorkBuffer;
				}

				for (s32 y = 0; y < h; y++)
				{
					output[y*s_texturePacker->width + x] = column[y];
				}
			}
		}
		s_usedTexels += w * h;

		// Copy the mapping into the texture table.
		tableEntry->x = (s32)node->rect.x + offsetX;
		tableEntry->y = (s32)node->rect.y + offsetY;
		tableEntry->z = (s32)w;
		tableEntry->w = (s32)h;

		// Page the page index into the x offset.
		tableEntry->x |= (s_currentPage << 12);
		tableEntry->y |= (scaleFactor << 12);
	}

	bool isTextureInMap(TextureData* tex)
	{
		return (s_textureDataMap.find(tex) != s_textureDataMap.end());
	}

	void insertTextureIntoMap(TextureData* tex, s32 id)
	{
		s_textureDataMap[tex] = id;
	}

	bool isWaxCellInMap(WaxCell* cell)
	{
		return (s_waxDataMap.find(cell) != s_waxDataMap.end());
	}

	void insertWaxCellIntoMap(WaxCell* cell, s32 id)
	{
		s_waxDataMap[cell] = id;
	}

#define SMALL_TEX 16

	bool isNotDivBy4(s32 w)
	{
		return ((w >> 2) & 1) == 1;
	}

	void getTexturePadding(s32& paddingX, s32& paddingY, s32 scale, TextureData* tex)
	{
		paddingX = (s_texturePacker->trueColor) ? FILTER_PADDING : 0;
		paddingY = paddingX;

		const s32 w = tex->width  * scale;
		const s32 h = tex->height * scale;

		if (s_texturePacker->mipCount > 1 && (tex->flags & ENABLE_MIP_MAPS))
		{
			paddingX = s_texturePacker->mipPadding;
			paddingY = paddingX;

			// No point in all the extra padding if the texture is smaller than the
			// padding itself.
			if (w < paddingX)
			{
				paddingX = w;
			}
			if (h < paddingY)
			{
				paddingY = h;
			}
		}
		// Handle the case where the texture takes up an entire axis, padding is not possible and not required in that case.
		if (w >= s_texturePacker->width)
		{
			paddingX = 0;
		}
		if (h >= s_texturePacker->height)
		{
			paddingY = 0;
		}

		// Make sure textures are large enough with padding, so mipmapping can work correctly.
		s32 tw = w + paddingX;
		s32 th = h + paddingY;
		if (s_texturePacker->mipCount > 1 && (tex->flags & ENABLE_MIP_MAPS) && (tw < SMALL_TEX || th < SMALL_TEX))
		{
			if (tw < SMALL_TEX) { paddingX = SMALL_TEX - w; }
			if (th < SMALL_TEX) { paddingY = SMALL_TEX - h; }
		}
	}

	bool insertTexture(TextureData* tex, bool packHdTextures, TextureData* baseFrame, s32 frameIndex)
	{
		if (!tex || isTextureInMap(tex)) { return true; }

		const s32 scale = packHdTextures ? baseFrame->scaleFactor : 1;
		const s32 w = tex->width  * scale;
		const s32 h = tex->height * scale;

		s32 paddingX, paddingY;
		getTexturePadding(paddingX, paddingY, scale, tex);

		TextureNode* node = insertNode(s_root, tex, w + paddingX, h + paddingY);
		if (!node)
		{
			return false;
		}

		s_totalTexels += w * h;
		insertTextureIntoMap(tex, s_texturePacker->texturesPacked);

		assert(node->tex == tex && s_texturePacker->texturesPacked < MAX_TEXTURE_COUNT);
		tex->textureId = s_texturePacker->texturesPacked;
		packNode(node, tex, &s_texturePacker->textureTable[s_texturePacker->texturesPacked], paddingX, paddingY, (tex->flags & ENABLE_MIP_MAPS) ? s_texturePacker->mipCount : 1, packHdTextures ? baseFrame : nullptr, frameIndex);
		s_texturePacker->texturesPacked++;
		return true;
	}

	bool insertDeltTexture(TextureData* tex)
	{
		if (!tex || isTextureInMap(tex)) { return true; }
		s32 padding = (s_texturePacker->trueColor) ? FILTER_PADDING : 0;
		TextureNode* node = insertNode(s_root, tex, tex->width + padding, tex->height + padding);
		if (!node)
		{
			return false;
		}

		s_totalTexels += tex->width * tex->height;
		insertTextureIntoMap(tex, s_texturePacker->texturesPacked);

		assert(node->tex == tex && s_texturePacker->texturesPacked < MAX_TEXTURE_COUNT);
		tex->textureId = s_texturePacker->texturesPacked;
		packNodeDeltaTex(node, tex, &s_texturePacker->textureTable[s_texturePacker->texturesPacked], padding, padding);
		s_texturePacker->texturesPacked++;
		return true;
	}

	bool insertWaxFrame(void* basePtr, WaxFrame* frame, bool packHdSprites)
	{
		if (!basePtr || !frame) { return true; }
		WaxCell* cell = WAX_CellPtr(basePtr, frame);
		if (!cell || isWaxCellInMap(cell)) { return true; }

		const HdWax* hdWax = packHdSprites ? TFE_Sprite_Jedi::getHdWaxData(basePtr) : nullptr;
		const s32 scale = hdWax ? 2 : 1;	// TODO: still hardcoded.
		const s32 w = cell->sizeX * scale;
		const s32 h = cell->sizeY * scale;
		
		s32 padding = (s_texturePacker->trueColor) ? FILTER_PADDING : 0;
		TextureNode* node = insertNode(s_root, cell, w + padding, h + padding);
		if (!node)
		{
			return false;
		}

		s_totalTexels += w * h;
		insertWaxCellIntoMap(cell, s_texturePacker->texturesPacked);

		assert(node->tex == cell && s_texturePacker->texturesPacked < MAX_TEXTURE_COUNT);
		cell->textureId = s_texturePacker->texturesPacked;
		packNodeCell(node, basePtr, cell, hdWax, &s_texturePacker->textureTable[s_texturePacker->texturesPacked], padding, padding);
		s_texturePacker->texturesPacked++;
		return true;
	}

	bool insertAnimatedTextureFrames(AnimatedTexture* animTex, bool packHdTextures)
	{
		if (!animTex) { return true; }
		for (s32 f = 0; f < animTex->count; f++)
		{
			if (!insertTexture(animTex->frameList[f], packHdTextures, animTex->baseFrame, f))
			{
				return false;
			}
		}
		return true;
	}
		
	s32 textureSort(const void* a, const void* b)
	{
		const TextureInfo* texA = (TextureInfo*)a;
		const TextureInfo* texB = (TextureInfo*)b;
		if (texA->sortKey > texB->sortKey) { return -1; }
		if (texA->sortKey < texB->sortKey) { return  1; }
		return 0;
	}

	TextureNode* allocateNode()
	{
		TextureNode* node = (TextureNode*)TFE_Memory::allocFromChunkedArray(s_nodePool);
		memset(node, 0, sizeof(TextureNode));
		return node;
	}

	// Begin the packing process, this clears out the texture packer.
	bool texturepacker_begin(TexturePacker* texturePacker)
	{
		if (!texturePacker) { return false; }
		if (s_texInfoPool.capacity() == 0)
		{
			s_texInfoPool.reserve(MAX_TEXTURE_COUNT);
		}

		bool trueColor = (TFE_Settings::getGraphicsSettings()->colorMode == COLORMODE_TRUE_COLOR);
		bool useMips = (TFE_Settings::getGraphicsSettings()->useMipmapping && trueColor);
		bool packerHasMips = texturePacker->mipCount > 1;
		u32 bytesPerTexel = trueColor ? 4 : 1;
		if (trueColor != texturePacker->trueColor || bytesPerTexel != texturePacker->bytesPerTexel || useMips != packerHasMips)
		{
			// Compute the desired mip count.
			u32 mipCount = useMips ? 6 : 1;
			texturePacker->mipCount = mipCount;
			texturePacker->mipPadding = 1 << mipCount;
			if (!trueColor)
			{
				texturePacker->mipPadding = 0;
			}
			
			memset(texturePacker->mipOffset, 0, sizeof(u32) * 8);
			u32 offset = 0;
			u32 w = texturePacker->width, h = texturePacker->height;
			texturePacker->pageSize = 0;
			for (u32 m = 0; m < texturePacker->mipCount; m++)
			{
				texturePacker->mipOffset[m] = offset;
				texturePacker->pageSize += (w * h);
				offset += w * h;
				w >>= 1;
				h >>= 1;
			}
			texturePacker->pageSize *= bytesPerTexel;

			// Realloc the pages.
			for (s32 p = 0; p < s_texturePacker->pageCount; p++)
			{
				TexturePage* page = s_texturePacker->pages[p];
				if (!page) { continue; }
				page->backingMemory = (u8*)realloc(page->backingMemory, texturePacker->pageSize);
				memset(page->backingMemory, 0, texturePacker->pageSize);
			}

			// Free the existing texture.
			TFE_RenderBackend::freeTexture(texturePacker->texture);
			texturePacker->texture = nullptr;
		}

		s_texturePacker = texturePacker;
		s_texturePacker->texturesPacked = 0;
		s_texturePacker->trueColor = trueColor;
		s_texturePacker->bytesPerTexel = bytesPerTexel;
		// Clear pages.
		for (s32 p = 0; p < s_texturePacker->pageCount; p++)
		{
			s_texturePacker->pages[p]->root = nullptr;
			s_texturePacker->pages[p]->textureCount = 0;
		}

		TFE_Memory::chunkedArrayClear(s_nodePool);
		s_textureDataMap.clear();
		s_waxDataMap.clear();
		s_texInfoPool.clear();

		// Insert the parent that covers all of the available space.
		s_root = nullptr;
		insertNode(nullptr, nullptr, 0, 0);
		s_texturePacker->pages[0]->root = s_root;
		return true;
	}

	// Commit the final packing to GPU memory.
	void texturepacker_commit()
	{
		// Update the texture table.
		s_texturePacker->textureTableGPU.update(s_texturePacker->textureTable, sizeof(Vec4i) * s_texturePacker->texturesPacked);

		// Create the texture if one doesn't exist or we need more layers.
		if (!s_texturePacker->texture || s_texturePacker->pageCount > (s32)s_texturePacker->texture->getLayers())
		{
			if (s_texturePacker->texture)
			{
				TFE_RenderBackend::freeTexture(s_texturePacker->texture);
			}
			// Allocate at least 2 layers.
			s_texturePacker->texture = TFE_RenderBackend::createTextureArray(s_texturePacker->width, s_texturePacker->height,
				max(2, s_texturePacker->pageCount), s_texturePacker->bytesPerTexel, s_texturePacker->mipCount);
		}
				
		// Then update each page.
		u32 width  = s_texturePacker->width;
		u32 height = s_texturePacker->height;
		u32 bytesPerTexel = s_texturePacker->bytesPerTexel;
		for (u32 mip = 0; mip < s_texturePacker->mipCount; mip++)
		{
			const size_t size = width * height * bytesPerTexel;
			for (s32 page = 0; page < s_texturePacker->pageCount; page++)
			{
				const u8* image = getWritePointer(page, 0, 0, mip);
				s_texturePacker->texture->update(image, size, page, mip);
			}
			width  >>= 1;
			height >>= 1;
		}

		// Write out the debug atlas if enabled.
		#if DEBUG_TEXTURE_ATLAS
			debug_writeOutAtlas();
		#endif
	}
		
	s32 texturepacker_pack(TextureListCallback getList, AssetPool pool)
	{
		if (!getList) { return 0; }
		s_assetPool = pool;

		const bool packHdTextures = TFE_Settings::getEnhancementsSettings()->enableHdTextures && s_texturePacker->trueColor;
		const bool packHdSprites  = TFE_Settings::getEnhancementsSettings()->enableHdSprites && s_texturePacker->trueColor;

		// Get textures.
		s_texInfoPool.clear();
		if (getList(s_texInfoPool, pool))
		{
			s32 count = (s32)s_texInfoPool.size();
			TextureInfo* list = s_texInfoPool.data();
			// 1. Calculate the sort key (uses the area metric).
			for (s32 i = 0; i < count; i++)
			{
				switch (list[i].type)
				{
					case TEXINFO_DF_TEXTURE_DATA:
					case TEXINFO_DF_DELT_TEX:
					{
						if (list[i].texData->uvWidth == BM_ANIMATED_TEXTURE)
						{
							AnimatedTexture* animTex = (AnimatedTexture*)list[i].texData->image;
							list[i].sortKey = animTex->frameList[0]->width + animTex->frameList[0]->height;
							if (packHdTextures && animTex->baseFrame->hdAssetData)
							{
								list[i].sortKey *= (animTex->baseFrame->scaleFactor * animTex->baseFrame->scaleFactor);
							}
						}
						else
						{
							list[i].sortKey = list[i].texData->width * list[i].texData->height;
							if (list[i].type == TEXINFO_DF_TEXTURE_DATA && packHdTextures && list[i].texData->hdAssetData)
							{
								list[i].sortKey *= (list[i].texData->scaleFactor * list[i].texData->scaleFactor);
							}
						}
					} break;
					case TEXINFO_DF_ANIM_TEX:
					{
						list[i].sortKey = list[i].animTex->frameList[0]->width * list[i].animTex->frameList[0]->height;
						// Account for texture scaling.
						if (packHdTextures && list[i].animTex->baseFrame->hdAssetData)
						{
							list[i].sortKey *= (list[i].animTex->baseFrame->scaleFactor * list[i].animTex->baseFrame->scaleFactor);
						}
					} break;
					case TEXINFO_DF_WAX_CELL:
					{
						WaxCell* cell = list[i].frame ? WAX_CellPtr(list[i].basePtr, list[i].frame) : nullptr;
						list[i].sortKey = cell ? cell->sizeX * cell->sizeY : 0;
						if (packHdSprites && cell)
						{
							const HdWax* hdWax = TFE_Sprite_Jedi::getHdWaxData(list[i].basePtr);
							if (hdWax)
							{
								// TODO: Hardcoded.
								list[i].sortKey *= 4;
							}
						}
					} break;
				}
			}

			// 2. Sort textures by perimeter from largest to smallest - simplified to w+h
			std::qsort(list, size_t(count), sizeof(TextureInfo), textureSort);

			// 3. Put all textures into the unpacked list.
			s_unpackedTextures[0].resize(count);
			TextureInfo** unpackedList = s_unpackedTextures[0].data();
			for (s32 i = 0; i < count; i++)
			{
				unpackedList[i] = &list[i];
			}

			// 4. Insert each texture into the tree, adding pages as needed.
			s_currentPage = s_texturePacker->reservedPages;
			if (s_currentPage >= s_texturePacker->pageCount)
			{
				s_texturePacker->pages[s_texturePacker->pageCount] = allocateTexturePage(s_texturePacker->pageSize);
				s_texturePacker->pageCount++;

				s_root = nullptr;
				insertNode(nullptr, nullptr, 0, 0);
				s_texturePacker->pages[s_currentPage]->root = s_root;
			}
			else
			{
				s_root = s_texturePacker->pages[s_currentPage]->root;
				if (!s_root)
				{
					insertNode(nullptr, nullptr, 0, 0);
					s_texturePacker->pages[s_currentPage]->root = s_root;
				}
			}

			s_unpackedBuffer = 0;
			while (!s_unpackedTextures[s_unpackedBuffer].empty())
			{
				count = (s32)s_unpackedTextures[s_unpackedBuffer].size();
				TextureInfo** unpackedList = s_unpackedTextures[s_unpackedBuffer].data();

				s_unpackedBuffer = (s_unpackedBuffer + 1)&1;
				s_unpackedTextures[s_unpackedBuffer].clear();

				for (s32 i = 0; i < count; i++)
				{
					switch (unpackedList[i]->type)
					{
						case TEXINFO_DF_TEXTURE_DATA:
						{
							if (unpackedList[i]->texData->uvWidth == BM_ANIMATED_TEXTURE)
							{
								AnimatedTexture* animTex = (AnimatedTexture*)unpackedList[i]->texData->image;
								if (!insertAnimatedTextureFrames(animTex, packHdTextures))
								{
									s_unpackedTextures[s_unpackedBuffer].push_back(unpackedList[i]);
								}
							}
							else
							{
								if (!insertTexture(unpackedList[i]->texData, packHdTextures, unpackedList[i]->texData, 0))
								{
									s_unpackedTextures[s_unpackedBuffer].push_back(unpackedList[i]);
								}
							}
						} break;
						case TEXINFO_DF_DELT_TEX:
						{
							if (!insertDeltTexture(unpackedList[i]->texData))
							{
								s_unpackedTextures[s_unpackedBuffer].push_back(unpackedList[i]);
							}
						} break;
						case TEXINFO_DF_ANIM_TEX:
						{
							if (!insertAnimatedTextureFrames(unpackedList[i]->animTex, packHdTextures))
							{
								s_unpackedTextures[s_unpackedBuffer].push_back(unpackedList[i]);
							}
						} break;
						case TEXINFO_DF_WAX_CELL:
						{
							if (!insertWaxFrame(unpackedList[i]->basePtr, unpackedList[i]->frame, packHdSprites))
							{
								s_unpackedTextures[s_unpackedBuffer].push_back(unpackedList[i]);
							}
						} break;
					}
				}

				// Allocate another page...
				if (!s_unpackedTextures[s_unpackedBuffer].empty())
				{
					s_currentPage++;
					if (s_currentPage >= s_texturePacker->pageCount)
					{
						s_texturePacker->pages[s_texturePacker->pageCount] = allocateTexturePage(s_texturePacker->pageSize);
						s_texturePacker->pageCount++;

						s_root = nullptr;
						insertNode(nullptr, nullptr, 0, 0);
						s_texturePacker->pages[s_currentPage]->root = s_root;
					}
					else
					{
						s_root = s_texturePacker->pages[s_currentPage]->root;
						if (!s_root)
						{
							insertNode(nullptr, nullptr, 0, 0);
							s_texturePacker->pages[s_currentPage]->root = s_root;
						}
					}
				}
			}
		}
		return s_texturePacker->texturesPacked;
	}

	void texturepacker_setIndexStart(s32 colorIndexStart)
	{
		s_colorIndexStart = colorIndexStart;
	}

	void texturepacker_reset()
	{
		TexturePacker* texturePacker = s_globalTexturePacker;
		if (!texturePacker) { return; }

		// Free the existing pages...
		for (s32 i = 1; i < texturePacker->pageCount; i++)
		{
			free(texturePacker->pages[i]->backingMemory);
			texturePacker->pages[i]->backingMemory = nullptr;
		}

		texturePacker->pageCount = 1;
		texturePacker->reservedPages = 0;
		texturePacker->reservedTexturesPacked = 0;

		s_usedTexels = 0;
		s_totalTexels = 0;
		s_nodePoolIndex = 0;
		s_unpackedBuffer = 0;
		s_currentPage = 0;

		TFE_Memory::chunkedArrayClear(s_nodePool);
		s_textureDataMap.clear();
		s_waxDataMap.clear();
		s_texInfoPool.clear();

		texturepacker_begin(s_globalTexturePacker);
	}

#if DEBUG_TEXTURE_ATLAS
	void debug_writeOutAtlas()
	{
		u32* image = (u32*)malloc(s_texturePacker->width * s_texturePacker->height * sizeof(u32));
		const u32* pal = getPalette(1);
		if (s_texturePacker->bytesPerTexel == 1)
		{
			const u32 count = s_texturePacker->width * s_texturePacker->height;
			for (s32 p = 0; p < s_texturePacker->pageCount; p++)
			{
				const u8* src = s_texturePacker->pages[p]->backingMemory;
				for (u32 i = 0; i < count; i++)
				{
					image[i] = pal[src[i]];
				}

				char atlasName[TFE_MAX_PATH];
				sprintf(atlasName, "Atlas_%s_%d.png", s_texturePacker->name, p);
				TFE_Image::writeImage(atlasName, s_texturePacker->width, s_texturePacker->height, image);
			}
		}
		else
		{
			u32 w = (u32)s_texturePacker->width;
			u32 h = (u32)s_texturePacker->height;
			for (u32 m = 0; m < s_texturePacker->mipCount; m++)
			{
				const u32 count = w * h;
				for (s32 p = 0; p < s_texturePacker->pageCount; p++)
				{
					const u32* src = (u32*)getWritePointer(p, 0, 0, m);
					for (u32 i = 0; i < count; i++)
					{
						image[i] = src[i];
					}

					char atlasName[TFE_MAX_PATH];
					sprintf(atlasName, "Atlas_%s_%d_mip%d.png", s_texturePacker->name, p, m);
					TFE_Image::writeImage(atlasName, w, h, image);
				}
				w >>= 1;
				h >>= 1;
			}
		}
		free(image);
	}
#endif

	///////////////////////////////////////////////////
	// Global Texture Packer.
	///////////////////////////////////////////////////
	TexturePacker* texturepacker_getGlobal()
	{
		if (!s_globalTexturePacker)
		{
			s_globalTexturePacker = texturepacker_init(c_globalTexturePackerName, c_globalPageWidth, c_globalPageWidth);
			texturepacker_begin(s_globalTexturePacker);
		}
		return s_globalTexturePacker;
	}

	void texturepacker_freeGlobal()
	{
		texturepacker_destroy(s_globalTexturePacker);
		s_globalTexturePacker = nullptr;
	}
}