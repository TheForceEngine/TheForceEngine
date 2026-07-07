#include <TFE_Jedi/Math/fixedPoint.h>
#include <TFE_Jedi/Level/rtexture.h>
#include "rsectorFloat.h"
#include "rflatFloat.h"
#include "rlightingFloat.h"
#include "redgePairFloat.h"
#include "rclassicFloat.h"
#include "rclassicFloatSharedState.h"
#include "fixedPoint20.h"
#include "../rscanline.h"
#include "../rsectorRender.h"
#include "../redgePair.h"
#include "../rcommon.h"
#include <assert.h>

namespace TFE_Jedi
{

namespace RClassic_Float
{
	static s32 s_scanlineX0;

	static fixed44_20 s_scanlineU0;
	static fixed44_20 s_scanlineV0;
	static fixed44_20 s_scanline_dUdX;
	static fixed44_20 s_scanline_dVdX;

	static s32 s_scanlineWidth;
	static const u8* s_scanlineLight;
	static u8* s_scanlineOut;

	static u8* s_ftexImage;
	static s32 s_ftexDataEnd;
	static s32 s_ftexHeight;
	static s32 s_ftexWidthMask;
	static s32 s_ftexHeightMask;
	static s32 s_ftexHeightLog2;
		
	void flat_addEdges(s32 length, s32 x0, f32 dyFloor_dx, f32 yFloor, f32 dyCeil_dx, f32 yCeil)
	{
		if (s_flatCount < s_maxSegCount && length > 0)
		{
			const f32 lengthFlt = f32(length - 1);

			f32 yCeil1 = yCeil;
			if (dyCeil_dx != 0)
			{
				yCeil1 += dyCeil_dx * lengthFlt;
			}
			f32 yFloor1 = yFloor;
			if (dyFloor_dx != 0)
			{
				yFloor1 += dyFloor_dx * lengthFlt;
			}

			edgePair_setup(length, x0, dyFloor_dx, yFloor1, yFloor, dyCeil_dx, yCeil, yCeil1, s_rcfltState.flatEdge);

			if (s_rcfltState.flatEdge->yPixel_C1 - 1 > s_wallMaxCeilY)
			{
				s_wallMaxCeilY = s_rcfltState.flatEdge->yPixel_C1 - 1;
			}
			if (s_rcfltState.flatEdge->yPixel_F1 + 1 < s_wallMinFloorY)
			{
				s_wallMinFloorY = s_rcfltState.flatEdge->yPixel_F1 + 1;
			}
			if (s_wallMaxCeilY < s_windowMinY_Pixels)
			{
				s_wallMaxCeilY = s_windowMinY_Pixels;
			}
			if (s_wallMinFloorY > s_windowMaxY_Pixels)
			{
				s_wallMinFloorY = s_windowMaxY_Pixels;
			}

			s_rcfltState.flatEdge++;
			s_flatCount++;
		}
	}
				
	// This produces functionally identical results to the original but splits apart the U/V and dUdx/dVdx into seperate variables
	// to account for C vs ASM differences.
	void drawScanline()
	{
		const fixed44_20 dVdX = s_scanline_dVdX;
		const fixed44_20 dUdX = s_scanline_dUdX;
		fixed44_20 V = s_scanlineV0;
		fixed44_20 U = s_scanlineU0;

		// Supports all texture dimensions. The original fixed point renderer only supported 64x64.
		for (s32 i = s_scanlineWidth - 1; i >= 0; i--, U += dUdX, V += dVdX)
		{
			const u32 texel = ((floor20(U) & s_ftexWidthMask) * s_ftexHeight + (floor20(V) & s_ftexHeightMask)) & s_ftexDataEnd;
			s_scanlineOut[i] = s_scanlineLight[s_ftexImage[texel]];
		}
	}

	void drawScanline_Fullbright()
	{
		const fixed44_20 dVdX = s_scanline_dVdX;
		const fixed44_20 dUdX = s_scanline_dUdX;
		fixed44_20 V = s_scanlineV0;
		fixed44_20 U = s_scanlineU0;

		// Supports all texture dimensions. The original fixed point renderer only supported 64x64.
		for (s32 i = s_scanlineWidth - 1; i >= 0; i--, U += dUdX, V += dVdX)
		{
			const u32 texel = ((floor20(U) & s_ftexWidthMask) * s_ftexHeight + (floor20(V) & s_ftexHeightMask)) & s_ftexDataEnd;
			s_scanlineOut[i] = s_ftexImage[texel];
		}
	}

	void drawScanline_Trans()
	{
		const fixed44_20 dVdX = s_scanline_dVdX;
		const fixed44_20 dUdX = s_scanline_dUdX;
		fixed44_20 V = s_scanlineV0;
		fixed44_20 U = s_scanlineU0;

		// Supports all texture dimensions. The original fixed point renderer only supported 64x64.
		for (s32 i = s_scanlineWidth - 1; i >= 0; i--, U += dUdX, V += dVdX)
		{
			const u32 texel = ((floor20(U) & s_ftexWidthMask) * s_ftexHeight + (floor20(V) & s_ftexHeightMask)) & s_ftexDataEnd;
			const u8 baseColor = s_ftexImage[texel];

			if (baseColor) { s_scanlineOut[i] = s_scanlineLight[baseColor]; }
		}
	}

	void drawScanline_Fullbright_Trans()
	{
		const fixed44_20 dVdX = s_scanline_dVdX;
		const fixed44_20 dUdX = s_scanline_dUdX;
		fixed44_20 V = s_scanlineV0;
		fixed44_20 U = s_scanlineU0;

		// Supports all texture dimensions. The original fixed point renderer only supported 64x64.
		for (s32 i = s_scanlineWidth - 1; i >= 0; i--, U += dUdX, V += dVdX)
		{
			const u32 texel = ((floor20(U) & s_ftexWidthMask) * s_ftexHeight + (floor20(V) & s_ftexHeightMask)) & s_ftexDataEnd;
			const u8 baseColor = s_ftexImage[texel];

			if (baseColor) { s_scanlineOut[i] = baseColor; }
		}
	}
			   
	bool flat_setTexture(TextureData* tex)
	{
		if (!tex) { return false; }

		s_ftexHeight = tex->height;
		s_ftexWidthMask = tex->width - 1;
		s_ftexHeightMask = tex->height - 1;
		s_ftexHeightLog2 = tex->logSizeY;
		s_ftexImage = tex->image;
		s_ftexDataEnd = tex->width * tex->height - 1;

		return true;
	}
	
	void flat_drawCeiling(SectorCached* sectorCached, EdgePairFloat* edges, s32 count)
	{
		f32 textureOffsetU = s_rcfltState.cameraPos.x - sectorCached->ceilOffset.x;
		f32 textureOffsetV = sectorCached->ceilOffset.z - s_rcfltState.cameraPos.z;

		f32 relCeil          = sectorCached->ceilingHeight - s_rcfltState.eyeHeight;
		f32 scaledRelCeil    =  relCeil * s_rcfltState.focalLenAspect;
		f32 cosScaledRelCeil =  scaledRelCeil * s_rcfltState.cosYaw;
		f32 negSinRelCeil    = -relCeil * s_rcfltState.sinYaw;
		f32 sinScaledRelCeil =  scaledRelCeil * s_rcfltState.sinYaw;
		f32 negCosRelCeil    = -relCeil * s_rcfltState.cosYaw;

		if (!flat_setTexture(*sectorCached->sector->ceilTex)) { return; }

		for (s32 y = s_windowMinY_Pixels; y <= s_wallMaxCeilY && y < s_windowMaxY_Pixels; y++)
		{
			const s32 yOffset = y * s_width;
			const f32 yShear = f32(y - s_screenYMidFlt);
			const f32 yRcp = (yShear != 0.0f) ? 1.0f/yShear : 1.0f;
			const f32 z = scaledRelCeil * yRcp;

			s32 x = s_windowMinX_Pixels;
			s32 left  = 0;
			s32 right = 0;
			for (s32 i = 0; i < count;)
			{
				if (!flat_buildScanlineCeiling(i, count, x, y, left, right, s_scanlineWidth, (EdgePairFixed*)edges))
				{
					break;
				}

				if (s_scanlineWidth > 0)
				{
					assert(left >= 0 && left + s_scanlineWidth <= s_width);
					assert(y >= 0 && y < s_height);
					s_scanlineX0  = left;
					s_scanlineOut = &s_display[left + yOffset];

					const f32 worldToTexelScale = 8.0f;
					f32 rightClip = f32(right - s_screenXMid) * s_rcfltState.aspectScaleX;
					f32 v0 = (cosScaledRelCeil - (negSinRelCeil*rightClip)) * yRcp;
					f32 u0 = (sinScaledRelCeil + (negCosRelCeil*rightClip)) * yRcp;

					s_scanlineV0 = floatToFixed20((v0 - textureOffsetV) * worldToTexelScale);
					s_scanlineU0 = floatToFixed20((u0 - textureOffsetU) * worldToTexelScale);

					const f32 worldTexelScaleAspect = yRcp * worldToTexelScale * s_rcfltState.aspectScaleY;
					s_scanline_dVdX =  floatToFixed20(negSinRelCeil * worldTexelScaleAspect);
					s_scanline_dUdX = -floatToFixed20(negCosRelCeil * worldTexelScaleAspect);
					s_scanlineLight =  computeLighting(z, 0);
					
					if (s_scanlineLight)
					{
						drawScanline();
					}
					else
					{
						drawScanline_Fullbright();
					}
				}
			} // while (i < count)
		}
	}
		
	void flat_drawFloor(SectorCached* sectorCached, EdgePairFloat* edges, s32 count)
	{
		f32 textureOffsetU = s_rcfltState.cameraPos.x - sectorCached->floorOffset.x;
		f32 textureOffsetV = sectorCached->floorOffset.z - s_rcfltState.cameraPos.z;

		f32 relFloor       = sectorCached->floorHeight - s_rcfltState.eyeHeight;
		f32 scaledRelFloor = relFloor * s_rcfltState.focalLenAspect;

		f32 cosScaledRelFloor = scaledRelFloor * s_rcfltState.cosYaw;
		f32 negSinRelFloor    =-relFloor * s_rcfltState.sinYaw;
		f32 sinScaledRelFloor = scaledRelFloor * s_rcfltState.sinYaw;
		f32 negCosRelFloor    =-relFloor * s_rcfltState.cosYaw;

		if (!flat_setTexture(*sectorCached->sector->floorTex)) { return; }

		for (s32 y = max(s_wallMinFloorY, s_windowMinY_Pixels); y <= s_windowMaxY_Pixels; y++)
		{
			const s32 yOffset = y * s_width;
			const f32 yShear = f32(y - s_screenYMidFlt);
			const f32 yRcp = (yShear != 0.0f) ? 1.0f/yShear : 1.0f;
			const f32 z = scaledRelFloor * yRcp;

			s32 x = s_windowMinX_Pixels;
			s32 left = 0;
			s32 right = 0;
			for (s32 i = 0; i < count;)
			{
				s32 winMaxX = s_windowMaxX_Pixels;

				// Search for the left edge of the scanline.
				if (!flat_buildScanlineFloor(i, count, x, y, left, right, s_scanlineWidth, (EdgePairFixed*)edges))
				{
					break;
				}

				if (s_scanlineWidth > 0)
				{
					assert(left >= 0 && left + s_scanlineWidth <= s_width);
					assert(y >= 0 && y < s_height);
					s_scanlineX0 = left;
					s_scanlineOut = &s_display[left + yOffset];

					const f32 worldToTexelScale = 8.0f;
					f32 rightClip = f32(right - s_screenXMid) * s_rcfltState.aspectScaleX;
					f32 v0 = (cosScaledRelFloor - (negSinRelFloor * rightClip)) * yRcp;
					f32 u0 = (sinScaledRelFloor + (negCosRelFloor * rightClip)) * yRcp;
					s_scanlineV0 = floatToFixed20((v0 - textureOffsetV) * worldToTexelScale);
					s_scanlineU0 = floatToFixed20((u0 - textureOffsetU) * worldToTexelScale);

					const f32 worldTexelScaleAspect = yRcp * worldToTexelScale * s_rcfltState.aspectScaleY;
					s_scanline_dVdX =  floatToFixed20(negSinRelFloor * worldTexelScaleAspect);
					s_scanline_dUdX = -floatToFixed20(negCosRelFloor * worldTexelScaleAspect);
					s_scanlineLight = computeLighting(z, 0);

					if (s_scanlineLight)
					{
						drawScanline();
					}
					else
					{
						drawScanline_Fullbright();
					}
				}
			} // while (i < count)
		}
	}

	//////////////////////////////////////////////////////////////////////
	// Polygon Scanline rendering using the same algorithms as flats.
	//////////////////////////////////////////////////////////////////////
	typedef void(*ScanlineFunction)();
	static const ScanlineFunction c_scanlineDrawFunc[] =
	{
		drawScanline,
		drawScanline_Fullbright,
		drawScanline_Trans,
		drawScanline_Fullbright_Trans
	};

	static f32 s_poly_offsetX;
	static f32 s_poly_offsetZ;

	static f32 s_poly_scaledHOffset;
	static f32 s_poly_sinYawHOffset;
	static f32 s_poly_cosYawHOffset;

	static f32 s_poly_cosYawScaledHOffset;
	static f32 s_poly_sinYawScaledHOffset;
		
	void flat_preparePolygon(f32 heightOffset, f32 offsetX, f32 offsetZ, TextureData* texture)
	{
		s_poly_offsetX = s_rcfltState.cameraPos.x - offsetX;
		s_poly_offsetZ = offsetZ - s_rcfltState.cameraPos.z;

		s_poly_scaledHOffset = heightOffset * s_rcfltState.focalLenAspect;
		s_poly_sinYawHOffset = s_rcfltState.sinYaw * heightOffset;
		s_poly_cosYawHOffset = s_rcfltState.cosYaw * heightOffset;

		s_poly_cosYawScaledHOffset = s_rcfltState.cosYaw * s_poly_scaledHOffset;
		s_poly_sinYawScaledHOffset = s_rcfltState.sinYaw * s_poly_scaledHOffset;

		s_ftexWidthMask  = texture->width - 1;
		s_ftexHeightMask = texture->height - 1;
		s_ftexHeightLog2 = texture->logSizeY;
		s_ftexImage      = texture->image;
		s_ftexDataEnd    = texture->width * texture->height - 1;
	}

	void flat_drawPolygonScanline(s32 x0, s32 x1, s32 y, bool trans)
	{
		x0 = max(x0, s_windowMinX_Pixels);
		x1 = min(x1, s_windowMaxX_Pixels);
		clipScanline(&x0, &x1, y);

		s_scanlineWidth = x1 - x0 + 1;
		if (s_scanlineWidth <= 0) { return; }

		s_scanlineX0  = x0;
		s_scanlineOut = &s_display[y * s_width + x0];

		const f32 yShear = f32(y - s_screenYMidFlt);
		const f32 yRcp = (yShear != 0.0f) ? 1.0f/yShear : 1.0f;
		const f32 z = s_poly_scaledHOffset * yRcp;
		const f32 right = f32(x1 - 1 - s_screenXMid) * s_rcfltState.aspectScaleX;

		const f32 u0 = s_poly_sinYawScaledHOffset - (s_poly_cosYawHOffset*right);
		const f32 v0 = s_poly_cosYawScaledHOffset + (s_poly_sinYawHOffset*right);
		s_scanlineU0 = floatToFixed20((u0*yRcp - s_poly_offsetX) * 8.0f);
		s_scanlineV0 = floatToFixed20((v0*yRcp - s_poly_offsetZ) * 8.0f);

		const f32 worldTexelScaleAspect = yRcp * 8.0f * s_rcfltState.aspectScaleY;
		s_scanline_dVdX = -floatToFixed20(s_poly_sinYawHOffset*worldTexelScaleAspect);
		s_scanline_dUdX =  floatToFixed20(s_poly_cosYawHOffset*worldTexelScaleAspect);

		s_scanlineLight = computeLighting(z, 0);
		const s32 index = (!s_scanlineLight) + trans*2;
		c_scanlineDrawFunc[index]();
	}

}  // RFlatFixed

}  // TFE_Jedi