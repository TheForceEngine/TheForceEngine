#include <climits>
#include <cstring>

#include "level.h"
#include "levelBin.h"
#include "levelData.h"
#include "rwall.h"
#include "rtexture.h"
#include <TFE_Game/igame.h>
#include <TFE_Asset/assetSystem.h>
#include <TFE_Asset/dfKeywords.h>
#include <TFE_Asset/modelAsset_jedi.h>
#include <TFE_Asset/spriteAsset_Jedi.h>
#include <TFE_Asset/vocAsset.h>
#include <TFE_DarkForces/sound.h>
#include <TFE_FileSystem/filestream.h>
#include <TFE_FileSystem/paths.h>
#include <TFE_System/parser.h>
#include <TFE_System/system.h>
#include <TFE_Settings/settings.h>

#include <TFE_Jedi/InfSystem/infSystem.h>
#include <TFE_Jedi/InfSystem/infTypesInternal.h>
#include <TFE_Jedi/InfSystem/message.h>

// TODO: Fix game dependency?
#include <TFE_DarkForces/logic.h>

namespace TFE_DarkForces
{
	extern u8 s_levelPalette[];
	extern u8 s_basePalette[];
	extern JBool s_palModified;
}
using namespace TFE_DarkForces;

namespace TFE_Jedi
{
	enum
	{
		DF_LEVEL_VERSION_MAJOR = 2,
		DF_LEVEL_VERSION_MINOR = 1,
	};
			
	// Temp State.
	static s32 s_dataIndex;
	static char s_readBuffer[256];
	static std::vector<char> s_buffer;

	JBool level_loadGeometry(const char* levelName);
	JBool level_loadObjects(const char* levelName, u8 difficulty);
	JBool level_loadGoals(const char* levelName);

	JBool level_load(const char* levelName, u8 difficulty)
	{
		if (!levelName) { return JFALSE; }

		// Clear just in case.
		for (s32 i = 0; i < NUM_COMPLETE; i++)
		{
			s_levelState.completeNum[COMPL_TRIG][i] = -1;
			s_levelState.completeNum[COMPL_ITEM][i] = -1;

			s_levelState.complete[COMPL_TRIG][i] = JFALSE;
			s_levelState.complete[COMPL_ITEM][i] = JFALSE;
		}

		// Settings helper
		TFE_Settings::setLevelName(levelName);

		// TFE - Level Script, loading before INF
		loadLevelScript();

		// Load level data.
		if (!level_loadGeometry(levelName)) { return JFALSE; }
		level_loadObjects(levelName, difficulty);
		inf_load(levelName);
		level_loadGoals(levelName);

		// TFE - Level Script Level Start
		startLevelScript(levelName);

		return JTRUE;
	}

	void level_loadPalette()
	{
		// Palette *IS* loaded from the level file.
		FilePath filePath;
		if (TFE_Paths::getFilePath(s_levelState.levelPaletteName, &filePath))
		{
			FileStream::readContents(&filePath, s_levelPalette, 768);
		}
		else if (TFE_Paths::getFilePath("default.pal", &filePath))
		{
			FileStream::readContents(&filePath, s_levelPalette, 768);
		}
		// The "base palette" is adjusted by the hud colors, which is why it is a copy.
		memcpy(s_basePalette, s_levelPalette, 768);
		s_palModified = JTRUE;
	}

	void level_postProcessGeometry()
	{
		// Process sectors after load.
		RSector* sector = s_levelState.sectors;
		for (u32 i = 0; i < s_levelState.sectorCount; i++, sector++)
		{
			RWall* wall = sector->walls;
			for (s32 w = 0; w < sector->wallCount; w++, wall++)
			{
				RSector* nextSector = wall->nextSector;
				if (nextSector)
				{
					if (wall->mirror < 0 || wall->mirror >= nextSector->wallCount)
					{
						wall->nextSector = nullptr;
						wall->mirrorWall = nullptr;
						wall->mirror = -1;
						continue;
					}

					RWall* mirror = &nextSector->walls[wall->mirror];
					wall->mirrorWall = mirror;
					// Both sides of a mirror should have the same lower flags3 (such as walkability).
					wall->flags3 |= (mirror->flags3 & 0x0f);
					mirror->flags3 |= (wall->flags3 & 0x0f);
				}
			}
			sector_setupWallDrawFlags(sector);
			sector_adjustHeights(sector, 0, 0, 0);
			sector_computeBounds(sector);
			// TFE: Added to support non-fixed-point rendering.
			sector->dirtyFlags = SDF_ALL;

			// TFE: Try to figure out if a texture is for sky...
			if ((sector->flags1 & SEC_FLAGS1_EXTERIOR) && (*sector->ceilTex))
			{
				(*sector->ceilTex)->flags |= ALWAYS_FULLBRIGHT;
			}
			if ((sector->flags1 & SEC_FLAGS1_PIT) && (*sector->floorTex))
			{
				(*sector->floorTex)->flags |= ALWAYS_FULLBRIGHT;
			}
		}

		// Setup the control sector.
		s_levelState.controlSector->id = s_levelState.sectorCount;
		s_levelState.controlSector->index = s_levelState.controlSector->id;
	}

	JBool level_loadGeometry(const char* levelName)
	{
		s_levelState.secretCount = 0;
		s_dataIndex = 0;
		s_levelState.minLayer = INT_MAX;
		s_levelState.maxLayer = INT_MIN;
		message_free();

		// Try loading as an LVB
		if (level_loadGeometryBin(levelName, s_buffer))
		{
			return JTRUE;
		}

		// Otherwise load the LEV
		char levelPath[TFE_MAX_PATH];
		strcpy(levelPath, levelName);
		strcat(levelPath, ".LEV");

		FilePath filePath;
		if (!TFE_Paths::getFilePath(levelPath, &filePath))
		{
			TFE_System::logWrite(LOG_ERROR, "level_loadGeometry", "Cannot find level geometry '%s'.", levelName);
			return false;
		}
		FileStream file;
		if (!file.open(&filePath, Stream::MODE_READ))
		{
			TFE_System::logWrite(LOG_ERROR, "level_loadGeometry", "Cannot open level geometry '%s'.", levelName);
			return false;
		}
		size_t len = file.getSize();
		s_buffer.resize(len);
		file.readBuffer(s_buffer.data(), u32(len));
		file.close();

		TFE_Parser parser;
		size_t bufferPos = 0;
		parser.init(s_buffer.data(), s_buffer.size());
		parser.addCommentString("#");
		parser.convertToUpperCase(true);

		// Only use the parser "read line" functionality and otherwise read in the same was as the DOS code.
		const char* line;
		line = parser.readLine(bufferPos);
		s32 versionMajor, versionMinor;
		if (sscanf(line, " LEV %d.%d", &versionMajor, &versionMinor) != 2)
		{
			TFE_System::logWrite(LOG_ERROR, "level_loadGeometry", "Cannot read version.");
			return false;
		}
		if (versionMajor != DF_LEVEL_VERSION_MAJOR || versionMinor != DF_LEVEL_VERSION_MINOR)
		{
			TFE_System::logWrite(LOG_ERROR, "level_loadGeometry", "Invalid level version %d.%d.", versionMajor, versionMinor);
			return false;
		}
		
		line = parser.readLine(bufferPos);
		if (sscanf(line, " LEVELNAME %s", s_readBuffer) != 1)
		{
			TFE_System::logWrite(LOG_ERROR, "level_loadGeometry", "Cannot read level name.");
			return false;
		}

		// This gets read here just to be overwritten later... so just ignore for now.
		line = parser.readLine(bufferPos);
		if (sscanf(line, " PALETTE %s", s_levelState.levelPaletteName) != 1)
		{
			TFE_System::logWrite(LOG_ERROR, "level_loadGeometry", "Cannot read palette name.");
			return false;
		}

		level_loadPalette();
		
		// Another value that is ignored.
		line = parser.readLine(bufferPos);
		if (sscanf(line, " MUSIC %s", s_readBuffer) != 1)
		{
			TFE_System::logWrite(LOG_WARNING, "level_loadGeometry", "Cannot read music name.");
		}
		else
		{
			line = parser.readLine(bufferPos);
		}

		// Sky Parallax.
		f32 parallax0, parallax1;
		if (sscanf(line, " PARALLAX %f %f", &parallax0, &parallax1) != 2)
		{
			TFE_System::logWrite(LOG_ERROR, "level_loadGeometry", "Cannot read parallax values.");
			return false;
		}
		s_levelState.parallax0 = floatToFixed16(parallax0);
		s_levelState.parallax1 = floatToFixed16(parallax1);

		// Number of textures used by the level.
		line = parser.readLine(bufferPos);
		if (sscanf(line, " TEXTURES %d", &s_levelState.textureCount) != 1)
		{
			TFE_System::logWrite(LOG_ERROR, "level_loadGeometry", "Cannot read texture count.");
			return false;
		}
		s_levelState.textures = (TextureData**)level_alloc(2 * s_levelState.textureCount * sizeof(TextureData**));
		memset(s_levelState.textures, 0, 2 * s_levelState.textureCount * sizeof(TextureData**));

		// Load Textures.
		TextureData** texture = s_levelState.textures;
		TextureData** texBase = s_levelState.textures + s_levelState.textureCount;
		for (s32 i = 0; i < s_levelState.textureCount; i++, texture++, texBase++)
		{
			line = parser.readLine(bufferPos);
			char textureName[256];
			if (sscanf(line, " TEXTURE: %s ", textureName) != 1)
			{
				TFE_System::logWrite(LOG_ERROR, "level_loadGeometry", "Cannot read texture name.");
				*texture = bitmap_load("default.bm", 1);
				(*texture)->flags |= ENABLE_MIP_MAPS;
			}
			else if (strcasecmp(textureName, "<NoTexture>") == 0)
			{
				*texture = nullptr;
			}
			else
			{
				TextureData* tex = bitmap_load(textureName, 1);
				if (!tex)
				{
					TFE_System::logWrite(LOG_WARNING, "level_loadGeometry", "Could not open '%s', using 'default.bm' instead.", textureName);
					tex = bitmap_load("default.bm", 1);
					if (!tex)
					{
						TFE_System::logWrite(LOG_ERROR, "level_loadGeometry", "'default.bm' is not a valid BM file!");
						assert(0);
						return false;
					}
				}
				// TFE - so we know which textures to mip.
				tex->flags |= ENABLE_MIP_MAPS;
				*texture = tex;
				// This version never gets modified, so serialization is simpler.
				*texBase = tex;

				// Setup an animated texture.
				if (tex->uvWidth == BM_ANIMATED_TEXTURE && !tex->animSetup)
				{
					bitmap_setupAnimatedTexture(texture, i);
				}
			}
		}

		// Load Sectors.
		line = parser.readLine(bufferPos);
		if (sscanf(line, "NUMSECTORS %d", &s_levelState.sectorCount) != 1)
		{
			TFE_System::logWrite(LOG_ERROR, "level_loadGeometry", "Cannot read sector count.");
			return false;
		}

		s_levelState.sectors = (RSector*)level_alloc(sizeof(RSector) * s_levelState.sectorCount);
		memset(s_levelState.sectors, 0, sizeof(RSector) * s_levelState.sectorCount);
		for (u32 i = 0; i < s_levelState.sectorCount; i++)
		{
			RSector* sector = &s_levelState.sectors[i];
			sector_clear(sector);
			sector->index = i;

			// Sector ID and Name
			line = parser.readLine(bufferPos);
			if (sscanf(line, " SECTOR %d", &sector->id) != 1)
			{
				TFE_System::logWrite(LOG_ERROR, "level_loadGeometry", "Cannot read sector id.");
				return false;
			}

			// Allow names to have '#' in them.
			line = parser.readLine(bufferPos, false, true);
			// Sectors missing a name are valid but do not get "addresses" - and thus cannot be
			// used by the INF system (except in the case of doors and exploding walls, see the flags section below).
			char name[256];
			if (sscanf(line, " NAME %s", name) == 1)
			{
				// Add the sector "address" for later use by the INF system.
				message_addAddress(name, 0, 0, sector);

				// Track special elevators.
				if (!strcasecmp(name, "complete"))
				{
					s_levelState.completeSector = sector;
				}
				else if (!strcasecmp(name, "boss"))
				{
					s_levelState.bossSector = sector;
				}
				else if (!strcasecmp(name, "mohc"))
				{
					s_levelState.mohcSector = sector;
				}
			}

			// Lighting
			line = parser.readLine(bufferPos);
			s32 ambient;
			if (sscanf(line, " AMBIENT %d", &ambient) != 1)
			{
				TFE_System::logWrite(LOG_ERROR, "level_loadGeometry", "Cannot read sector ambient.");
				return false;
			}
			sector->ambient = intToFixed16(ambient);

			// Floor Texture & Offset
			line = parser.readLine(bufferPos);
			s32 index, tmp;
			f32 offsetX, offsetZ;
			if (sscanf(line, " FLOOR TEXTURE %d %f %f %d", &index, &offsetX, &offsetZ, &tmp) != 4)
			{
				TFE_System::logWrite(LOG_ERROR, "level_loadGeometry", "Cannot read floor texture.");
				return false;
			}

			sector->floorTex = nullptr;
			if (index != -1)
			{
				sector->floorTex = &s_levelState.textures[index];
			}
			sector->floorOffset.x = floatToFixed16(offsetX);
			sector->floorOffset.z = floatToFixed16(offsetZ);

			// Floor Altitude
			line = parser.readLine(bufferPos);
			f32 alt;
			if (sscanf(line, " FLOOR ALTITUDE %f", &alt) != 1)
			{
				TFE_System::logWrite(LOG_ERROR, "level_loadGeometry", "Cannot read floor altitude.");
				return false;
			}
			sector->floorHeight = floatToFixed16(alt);

			// Ceiling Texture & Offset
			line = parser.readLine(bufferPos);
			if (sscanf(line, " CEILING TEXTURE %d %f %f %d", &index, &offsetX, &offsetZ, &tmp) != 4)
			{
				TFE_System::logWrite(LOG_ERROR, "level_loadGeometry", "Cannot read ceiling texture.");
				return false;
			}
			sector->ceilTex = nullptr;
			if (index != -1)
			{
				sector->ceilTex = &s_levelState.textures[index];
			}
			sector->ceilOffset.x = floatToFixed16(offsetX);
			sector->ceilOffset.z = floatToFixed16(offsetZ);

			// Ceiling Altitude
			line = parser.readLine(bufferPos);
			if (sscanf(line, " CEILING ALTITUDE %f", &alt) != 1)
			{
				TFE_System::logWrite(LOG_ERROR, "level_loadGeometry", "Cannot read ceiling altitude.");
				return false;
			}
			sector->ceilingHeight = floatToFixed16(alt);

			// Second Altitude
			line = parser.readLine(bufferPos);
			if (sscanf(line, " SECOND ALTITUDE %f", &alt) != 1)
			{
				TFE_System::logWrite(LOG_ERROR, "level_loadGeometry", "Cannot read second altitude.");
				return false;
			}
			sector->secHeight = floatToFixed16(alt);

			// Sector flags
			line = parser.readLine(bufferPos);
			if (sscanf(line, " FLAGS %d %d %d", &sector->flags1, &sector->flags2, &sector->flags3) != 3)
			{
				TFE_System::logWrite(LOG_ERROR, "level_loadGeometry", "Cannot read sector flags.");
				return false;
			}
			// Create a door if needed.
			if (sector->flags1 & SEC_FLAGS1_DOOR)
			{
				InfElevator* elev = inf_allocateSpecialElevator(sector, IELEV_SP_DOOR);
				if (elev) { elev->flags |= INF_EFLAG_DOOR; }
			}
			// Create an exploding wall if needed.
			if (sector->flags1 & SEC_FLAGS1_EXP_WALL)
			{
				inf_allocateSpecialElevator(sector, IELEV_SP_EXPLOSIVE_WALL);
			}
			// Add secrets.
			if (sector->flags1 & SEC_FLAGS1_SECRET)
			{
				s_levelState.secretCount++;
			}

			// Layer
			line = parser.readLine(bufferPos);
			if (sscanf(line, " LAYER %d", &sector->layer) != 1)
			{
				TFE_System::logWrite(LOG_ERROR, "level_loadGeometry", "Cannot read sector layer.");
				return false;
			}
			s_levelState.minLayer = min(s_levelState.minLayer, sector->layer);
			s_levelState.maxLayer = max(s_levelState.maxLayer, sector->layer);

			// Vertices
			line = parser.readLine(bufferPos);
			s32 vertexCount;
			if (sscanf(line, " VERTICES %d", &vertexCount) != 1)
			{
				TFE_System::logWrite(LOG_ERROR, "level_loadGeometry", "Cannot read sector vertices.");
				return false;
			}
			const size_t vtxSize = vertexCount * sizeof(vec2_fixed);
			sector->verticesWS = (vec2_fixed*)level_alloc(vtxSize);
			sector->verticesVS = (vec2_fixed*)level_alloc(vtxSize);
			sector->vertexCount = vertexCount;

			for (s32 v = 0; v < vertexCount; v++)
			{
				line = parser.readLine(bufferPos);

				f32 x, z;
				sscanf(line, " X: %f Z: %f ", &x, &z);
				sector->verticesWS[v].x = floatToFixed16(x);
				sector->verticesWS[v].z = floatToFixed16(z);
			}

			// Walls
			line = parser.readLine(bufferPos);
			s32 wallCount;
			if (sscanf(line, " WALLS %d", &wallCount) != 1)
			{
				TFE_System::logWrite(LOG_ERROR, "level_loadGeometry", "Cannot read sector walls.");
				return false;
			}
			sector->walls = (RWall*)level_alloc(wallCount * sizeof(RWall));
			sector->wallCount = wallCount;

			for (s32 w = 0; w < wallCount; w++)
			{
				s32 light, flags3, flags2, flags1, walk, mirror, adjoin;
				s32 signTex, unused;
				s32 botTex, topTex, midTex, right, left;
				f32 signOffsetZ, signOffsetX;
				f32 botOffsetZ, botOffsetX;
				f32 topOffsetZ, topOffsetX;
				f32 midOffsetZ, midOffsetX;

				line = parser.readLine(bufferPos);
				if (sscanf(line, " WALL LEFT: %d RIGHT: %d MID: %d %f %f %d TOP: %d %f %f %d BOT: %d %f %f %d SIGN: %d %f %f ADJOIN: %d MIRROR: %d WALK: %d FLAGS: %d %d %d LIGHT: %d",
					&left, &right, &midTex, &midOffsetX, &midOffsetZ, &unused, &topTex, &topOffsetX, &topOffsetZ, &unused, &botTex, &botOffsetX, &botOffsetZ, &unused,
					&signTex, &signOffsetX, &signOffsetZ, &adjoin, &mirror, &walk, &flags1, &flags2, &flags3, &light) != 24)
				{
					TFE_System::logWrite(LOG_ERROR, "level_loadGeometry", "Cannot read wall.");
					return false;
				}

				RWall* wall = &sector->walls[w];
				wall->id = w;
				wall->sector = sector;
				wall->mirrorWall = nullptr;
				wall->seen = JFALSE;
				wall->flags1 = flags1;
				wall->flags2 = flags2;
				wall->flags3 = flags3;

				vec2_fixed* leftVtxWS = &sector->verticesWS[left];
				vec2_fixed* rightVtxWS = &sector->verticesWS[right];
				wall->w0 = leftVtxWS;
				wall->w1 = rightVtxWS;
				wall->v0 = &sector->verticesVS[left];
				wall->v1 = &sector->verticesVS[right];
				// Store the original position 0 in the wall since it is used by the sector rotation INF.
				wall->worldPos0.x = leftVtxWS->x;
				wall->worldPos0.z = leftVtxWS->z;

				wall->nextSector = nullptr;
				wall->mirror = -1;
				if (adjoin != -1)
				{
					wall->nextSector = &s_levelState.sectors[adjoin];
					if (mirror == -1)
					{
						TFE_System::logWrite(LOG_ERROR, "level_loadGeometry", "Adjoining wall missing mirror.");
					}
					wall->mirror = mirror;
				}

				wall->infLink = nullptr;
				wall->collisionFrame = 0;
				wall->drawFrame = 0;
				wall->drawFlags = 0;
				wall->wallLight = intToFixed16(light);

				wall->midTex = nullptr;
				if (midTex != -1)
				{
					wall->midTex = s_levelState.textures[midTex];
					wall->midOffset.x = floatToFixed16(midOffsetX) * 8;
					wall->midOffset.z = floatToFixed16(midOffsetZ) * 8;
				}

				wall->topTex = nullptr;
				if (topTex != -1)
				{
					wall->topTex = s_levelState.textures[topTex];
					wall->topOffset.x = floatToFixed16(topOffsetX) * 8;
					wall->topOffset.z = floatToFixed16(topOffsetZ) * 8;
				}

				wall->botTex = nullptr;
				if (botTex != -1)
				{
					wall->botTex = s_levelState.textures[botTex];
					wall->botOffset.x = floatToFixed16(botOffsetX) * 8;
					wall->botOffset.z = floatToFixed16(botOffsetZ) * 8;
				}

				wall->signTex = nullptr;
				if (signTex != -1)
				{
					wall->signTex = &s_levelState.textures[signTex];
					wall->signOffset.x = floatToFixed16(signOffsetX) * 8;
					wall->signOffset.z = floatToFixed16(signOffsetZ) * 8;
				}

				fixed16_16 dx = rightVtxWS->x - leftVtxWS->x;
				fixed16_16 dz = rightVtxWS->z - leftVtxWS->z;
				wall->angle  = vec2ToAngle(dx, dz);
				wall->length = vec2Length(dx, dz);
				wall_computeDirectionVector(wall);
				wall->texelLength = wall->length * 8;
			}
		}

		level_postProcessGeometry();

		return true;
	}

	void level_freeAllAssets()
	{
		TFE_Sprite_Jedi::freeLevelData();
		TFE_Model_Jedi::freeLevelData();
	}

	JBool level_isGoalComplete(s32 goalIndex)
	{
		for (s32 i = 0; i < NUM_COMPLETE; i++)
		{
			if (s_levelState.completeNum[COMPL_TRIG][i] == goalIndex)
			{
				return s_levelState.complete[COMPL_TRIG][i];
			}

			if (s_levelState.completeNum[COMPL_ITEM][i] == goalIndex)
			{
				return s_levelState.complete[COMPL_ITEM][i];
			}
		}
		return JFALSE;
	}
		
	JBool level_loadGoals(const char* levelName)
	{
		char levelPath[TFE_MAX_PATH];
		strcpy(levelPath, levelName);
		strcat(levelPath, ".GOL");
				
		FilePath filePath;
		FileStream file;
		if (!TFE_Paths::getFilePath(levelPath, &filePath))
		{
			TFE_System::logWrite(LOG_ERROR, "level_loadGoals", "Cannot find level goals '%s'.", levelName);
			return JFALSE;
		}
		if (!file.open(&filePath, Stream::MODE_READ))
		{
			TFE_System::logWrite(LOG_ERROR, "level_loadGoals", "Cannot open level goals '%s'.", levelName);
			return JFALSE;
		}

		size_t len = file.getSize();
		s_buffer.resize(len);
		file.readBuffer(s_buffer.data(), u32(len));
		file.close();

		TFE_Parser parser;
		size_t bufferPos = 0;
		parser.init(s_buffer.data(), s_buffer.size());
		parser.enableBlockComments();
		parser.addCommentString("//");
		parser.addCommentString("#");

		const char* line;
		line = parser.readLine(bufferPos);
		s32 versionMajor, versionMinor;
		if (sscanf(line, "GOL %d.%d", &versionMajor, &versionMinor) != 2)
		{
			TFE_System::logWrite(LOG_ERROR, "level_loadGeometry", "Cannot parse version for Goal file '%s'.", levelName);
			return false;
		}

		line = parser.readLine(bufferPos);
		while (line)
		{
			s32 goalNum, typeNum;
			char type[32];
			if (sscanf(line, " GOAL: %d %s %d", &goalNum, type, &typeNum) == 3)
			{
				if (typeNum < 0 || typeNum >= NUM_COMPLETE)
				{
					TFE_System::logWrite(LOG_ERROR, "Goals", "Bad item or trigger number in .GOL file");
				}

				if (typeNum >= 0 && typeNum < NUM_COMPLETE)
				{
					if (strcasecmp(type, "ITEM:") == 0)
					{
						s_levelState.completeNum[COMPL_ITEM][typeNum] = goalNum;
					}
					else if (strcasecmp(type, "TRIG:") == 0)
					{
						s_levelState.completeNum[COMPL_TRIG][typeNum] = goalNum;
					}
				}
			}

			line = parser.readLine(bufferPos);
		}
		file.close();

		return JTRUE;
	}

	void ambientSoundTaskFunc(MessageType msg)
	{
		task_begin;
		while (msg != MSG_FREE_TASK)
		{
			task_localBlockBegin;
			AmbientSound* ambientSound = (AmbientSound*)allocator_getHead(s_levelState.ambientSounds);
			while (ambientSound)
			{
				ambientSound->instanceId = sound_maintain(ambientSound->instanceId, ambientSound->soundId, ambientSound->pos);
				ambientSound = (AmbientSound*)allocator_getNext(s_levelState.ambientSounds);
			}
			task_localBlockEnd;
			task_yield(72);	// half a second.
		}
		task_end;
	}

	void level_addAmbientSound(SoundSourceId soundId, vec3_fixed pos)
	{
		if (!s_levelState.ambientSounds)
		{
			s_levelState.ambientSounds = allocator_create(sizeof(AmbientSound));
			s_levelIntState.ambientSoundTask = createSubTask("AmbientSound", ambientSoundTaskFunc);
		}
		AmbientSound* ambientSound = (AmbientSound*)allocator_newItem(s_levelState.ambientSounds);
		if (!ambientSound)
			return;
		ambientSound->soundId = soundId;
		ambientSound->instanceId = 0;
		ambientSound->pos = pos;
	}

	void level_addSound(const char* name, u32 freq, s32 priority)
	{
		// TODO
	}

	JBool level_loadObjects(const char* levelName, u8 difficulty)
	{
		char levelPath[TFE_MAX_PATH];
		strcpy(levelPath, levelName);
		strcat(levelPath, ".O");

		s32 curDiff = s32(difficulty) + 1;

		FilePath filePath;
		if (!TFE_Paths::getFilePath(levelPath, &filePath))
		{
			TFE_System::logWrite(LOG_ERROR, "Level Load", "Cannot find level objects '%s'.", levelName);
			return false;
		}
		FileStream file;
		if (!file.open(&filePath, Stream::MODE_READ))
		{
			TFE_System::logWrite(LOG_ERROR, "Level Load", "Cannot open level objects '%s'.", levelName);
			return false;
		}

		size_t len = file.getSize();
		s_buffer.resize(len);
		file.readBuffer(s_buffer.data(), u32(len));
		file.close();

		TFE_Parser parser;
		size_t bufferPos = 0;
		parser.init(s_buffer.data(), s_buffer.size());
		parser.enableBlockComments();
		parser.addCommentString("//");
		parser.addCommentString("#");
		parser.convertToUpperCase(true);

		// Only use the parser "read line" functionality and otherwise read in the same was as the DOS code.
		const char* line;
		line = parser.readLine(bufferPos);
		s32 versionMajor, versionMinor;
		if (sscanf(line, "O %d.%d", &versionMajor, &versionMinor) != 2)
		{
			TFE_System::logWrite(LOG_ERROR, "Level Load", "Cannot parse version for Object file '%s'.", levelName);
			return false;
		}

		while (nullptr != (line = parser.readLine(bufferPos)))
		{
			if (sscanf(line, "PODS %d", &s_levelIntState.podCount) == 1)
			{
				s_levelIntState.pods = (JediModel**)level_alloc(sizeof(JediModel*)*s_levelIntState.podCount);
				for (s32 p = 0; p < s_levelIntState.podCount; p++)
				{
					line = parser.readLine(bufferPos);
					s_levelIntState.pods[p] = nullptr;

					if (line)
					{
						char podName[32];
						if (sscanf(line, " POD: %s", podName) == 1)
						{
							s_levelIntState.pods[p] = TFE_Model_Jedi::get(podName);
							if (!s_levelIntState.pods[p])
							{
								s_levelIntState.pods[p] = TFE_Model_Jedi::get("default.3do");
							}
						}
						else
						{
							TFE_System::logWrite(LOG_WARNING, "Level Load", "Unknown line in pod list '%s' - skipping.", line);
						}
					}
				}
			}
			else if (sscanf(line, "SPRS %d", &s_levelIntState.spriteCount) == 1)
			{
				s_levelIntState.sprites = (JediWax**)level_alloc(sizeof(JediWax*)*s_levelIntState.spriteCount);
				for (s32 s = 0; s < s_levelIntState.spriteCount; s++)
				{
					line = parser.readLine(bufferPos);
					s_levelIntState.sprites[s] = nullptr;

					if (line)
					{
						char name[32];
						if (sscanf(line, " SPR: %s ", name) == 1)
						{
							s_levelIntState.sprites[s] = TFE_Sprite_Jedi::getWax(name);
							if (!s_levelIntState.sprites[s])
							{
								s_levelIntState.sprites[s] = TFE_Sprite_Jedi::getWax("default.wax");
							}
						}
						else
						{
							TFE_System::logWrite(LOG_WARNING, "Level Load", "Unknown line in sprite list '%s' - skipping.", line);
						}
					}
				}
			}
			else if (sscanf(line, "FMES %d", &s_levelIntState.fmeCount) == 1)
			{
				s_levelIntState.frames = (JediFrame**)level_alloc(sizeof(JediFrame*)*s_levelIntState.fmeCount);
				for (s32 f = 0; f < s_levelIntState.fmeCount; f++)
				{
					line = parser.readLine(bufferPos);
					s_levelIntState.frames[f] = nullptr;

					if (line)
					{
						char name[32];
						if (sscanf(line, " FME: %s ", name) == 1)
						{
							s_levelIntState.frames[f] = TFE_Sprite_Jedi::getFrame(name);
							if (!s_levelIntState.frames[f])
							{
								s_levelIntState.frames[f] = TFE_Sprite_Jedi::getFrame("default.fme");
							}
						}
						else
						{
							TFE_System::logWrite(LOG_WARNING, "Level Load", "Unknown line in fme list '%s' - skipping.", line);
						}
					}
				}
			}
			else if (sscanf(line, "SOUNDS %d", &s_levelIntState.soundCount) == 1)
			{
				s_levelIntState.soundIds = (SoundSourceId*)level_alloc(sizeof(SoundSourceId)*s_levelIntState.soundCount);
				for (s32 s = 0; s < s_levelIntState.soundCount; s++)
				{
					line = parser.readLine(bufferPos);
					s_levelIntState.soundIds[s] = NULL_SOUND;

					if (line)
					{
						char name[32];
						if (sscanf(line, " SOUND: %s ", name) == 1)
						{
							s_levelIntState.soundIds[s] = sound_load(name, SOUND_PRIORITY_LOW2);
						}
						else
						{
							TFE_System::logWrite(LOG_WARNING, "Level Load", "Unknown line in sound list '%s' - skipping.", line);
						}
					}
				}
			}
			else if (sscanf(line, "OBJECTS %d", &s_levelIntState.objectCount) == 1)
			{
				s32 count = s_levelIntState.objectCount;
				JBool readNextLine = JTRUE;
				for (s32 objIndex = 0; objIndex < count;)
				{
					if (readNextLine)
					{
						line = parser.readLine(bufferPos);
						if (!line) { break; }
					}
					else
					{
						readNextLine = JTRUE;
					}

					s32 objDiff = 0;
					f32 x, y, z, pch, yaw, rol;
					char objClass[32];

					if (sscanf(line, " CLASS: %s DATA: %d X: %f Y: %f Z: %f PCH: %f YAW: %f ROL: %f DIFF: %d", objClass, &s_dataIndex, &x, &y, &z, &pch, &yaw, &rol, &objDiff) > 5)
					{
						objIndex++;
						// objDiff >= 0: This difficulty and all greater.
						// objDiff <  0: Less than this difficulty.
						if ((objDiff >= 0 && curDiff < objDiff) || (objDiff < 0 && curDiff > TFE_Jedi::abs(objDiff)))
						{
							continue;
						}

						vec3_fixed posWS;
						posWS.x = floatToFixed16(x);
						posWS.y = floatToFixed16(y);
						posWS.z = floatToFixed16(z);

						// The DOS code allocated the object, tried to find the sector it is in and than frees the object
						// if it doesn't fit.
						// Instead TFE just reads the values and only allocates the object if it has a valid sector.
						RSector* sector = sector_which3D(posWS.x, posWS.y, posWS.z);
						if (!sector)
						{
							continue;
						}

						SecObject* obj = allocateObject();
						obj->posWS = posWS;
						obj->pitch = floatDegreesToFixed(pch);
						obj->yaw   = floatDegreesToFixed(yaw);
						obj->roll  = floatDegreesToFixed(rol);

						KEYWORD classType = getKeywordIndex(objClass);
						switch (classType)
						{
							case KW_3D:
							{
								if (s_levelIntState.pods && s_dataIndex >= 0 && s_dataIndex < s_levelIntState.podCount)
								{
									sector_addObject(sector, obj);
									obj3d_setData(obj, s_levelIntState.pods[s_dataIndex]);
									obj3d_computeTransform(obj);
								}
								else
								{
									TFE_System::logWrite(LOG_ERROR, "Level", "3DO index %d is out of range, count %d", s_dataIndex, s_levelIntState.podCount);
									freeObject(obj);
									obj = nullptr;
								}
							} break;
							case KW_SPRITE:
							{
								if (s_levelIntState.sprites && s_dataIndex >= 0 && s_dataIndex < s_levelIntState.spriteCount)
								{
									sector_addObject(sector, obj);
									sprite_setData(obj, s_levelIntState.sprites[s_dataIndex]);
								}
								else
								{
									TFE_System::logWrite(LOG_ERROR, "Level", "Sprite index %d is out of range, count %d", s_dataIndex, s_levelIntState.spriteCount);
									freeObject(obj);
									obj = nullptr;
								}
							} break;
							case KW_FRAME:
							{
								if (s_levelIntState.frames && s_dataIndex >= 0 && s_dataIndex < s_levelIntState.fmeCount)
								{
									sector_addObject(sector, obj);
									frame_setData(obj, s_levelIntState.frames[s_dataIndex]);
								}
								else
								{
									TFE_System::logWrite(LOG_ERROR, "Level", "Frame index %d is out of range, count %d", s_dataIndex, s_levelIntState.fmeCount);
									freeObject(obj);
									obj = nullptr;
								}
							} break;
							case KW_SPIRIT:
							{
								sector_addObject(sector, obj);
								spirit_setData(obj);
							} break;
							case KW_SOUND:
							{
								level_addAmbientSound(s_levelIntState.soundIds[s_dataIndex], obj->posWS);
								freeObject(obj);
								obj = nullptr;
							} break;
							case KW_SAFE:
							{
								if (!s_levelState.safeLoc)
								{
									s_levelState.safeLoc = allocator_create(sizeof(Safe));
								}
								Safe* safe = (Safe*)allocator_newItem(s_levelState.safeLoc);
								if (!safe)
									return JFALSE;
								safe->sector = sector;
								safe->x = obj->posWS.x;
								safe->z = obj->posWS.z;
								safe->yaw = obj->yaw;
								sector->flags1 |= SEC_FLAGS1_SAFESECTOR;

								freeObject(obj);
								obj = nullptr;
							} break;
							default:
							{
								freeObject(obj);
								obj = nullptr;
								TFE_System::logWrite(LOG_ERROR, "Level Load", "Invalid Object Class: %d - Skipping Object.", classType);
								continue;
							}
						}

						if (obj)
						{
							obj_addToRefList(obj, ObjRefType_Startup);	// Scripting
						}

						if (obj)
						{
							readNextLine = object_parseSeq(obj, &parser, &bufferPos);
							if (obj->entityFlags & ETFLAG_PLAYER)
							{
								if (!s_levelState.safeLoc)
								{
									s_levelState.safeLoc = allocator_create(sizeof(Safe));
								}
								Safe* safe = (Safe*)allocator_newItem(s_levelState.safeLoc);
								if (!safe)
									return JFALSE;
								safe->sector = obj->sector;
								safe->x = obj->posWS.x;
								safe->z = obj->posWS.z;
								safe->yaw = obj->yaw;
								sector->flags1 |= SEC_FLAGS1_SAFESECTOR;
							}
						}
					}
				}
			}
		}

		return JTRUE;
	}

	Safe* level_getSafeFromSector(RSector* sector)
	{
		Safe* safe = (Safe*)allocator_getHead(s_levelState.safeLoc);
		while (safe)
		{
			if (safe->sector == sector)
			{
				return safe;
			}
			safe = (Safe*)allocator_getNext(s_levelState.safeLoc);
		}
		return nullptr;
	}
		
	void getSkyParallax(fixed16_16* parallax0, fixed16_16* parallax1)
	{
		*parallax0 = s_levelState.parallax0;
		*parallax1 = s_levelState.parallax1;
	}

	void setSkyParallax(fixed16_16 parallax0, fixed16_16 parallax1)
	{
		s_levelState.parallax0 = parallax0;
		s_levelState.parallax1 = parallax1;
	}

	void setObjPos_AddToSector(SecObject* obj, s32 x, s32 y, s32 z, RSector* sector)
	{
		obj->posWS.x = x;
		obj->posWS.y = y;
		obj->posWS.z = z;
		sector_addObject(sector, obj);
	}

	//////////////////////////////////////////////
	// TFE: Level Script
	//////////////////////////////////////////////
	const char* c_levelScriptName = "LevelScript";
	const char* c_levelScriptFile = "LevelScript.fs";

	void freeLevelScript()
	{
		TFE_ForceScript::deleteModule(c_levelScriptName);
		s_levelState.levelScript = nullptr;
		s_levelState.levelScriptStart = nullptr;
		s_levelState.levelScriptUpdate = nullptr;
	}

	void loadLevelScript()
	{
		TFE_ForceScript::ModuleHandle scriptMod = TFE_ForceScript::getModule(c_levelScriptName);
		if (scriptMod)
		{
			s_levelState.levelScript = scriptMod;
		}
		else
		{
			s_levelState.levelScript = TFE_ForceScript::createModule(c_levelScriptName, c_levelScriptFile, true, API_GAME);
		}
		s_levelState.levelScriptStart = nullptr;
		s_levelState.levelScriptUpdate = nullptr;
		if (!s_levelState.levelScript) { return; }

		// Get the start and update functions.
		s_levelState.levelScriptStart  = TFE_ForceScript::findScriptFuncByDecl(s_levelState.levelScript, "void levelStart(string)");
		s_levelState.levelScriptUpdate = TFE_ForceScript::findScriptFuncByDecl(s_levelState.levelScript, "void levelUpdate(float)");
	}

	void startLevelScript(const char* levelName)
	{
		if (s_levelState.levelScript && s_levelState.levelScriptStart)
		{
			TFE_ForceScript::ScriptArg arg;
			arg.type = TFE_ForceScript::ARG_STRING;
			strcpy(arg.strValue, levelName);
			TFE_ForceScript::execFunc(s_levelState.levelScriptStart, 1, &arg);
		}
	}

	void updateLevelScript(f32 dt)
	{
		if (s_levelState.levelScript && s_levelState.levelScriptUpdate)
		{
			TFE_ForceScript::ScriptArg arg;
			arg.type = TFE_ForceScript::ARG_F32;
			arg.fValue = dt;
			TFE_ForceScript::execFunc(s_levelState.levelScriptUpdate, 1, &arg);
		}
	}

	TFE_ForceScript::ModuleHandle getLevelScript()
	{
		return s_levelState.levelScript;
	}

	TFE_ForceScript::FunctionHandle getLevelScriptFunc(const char* funcName)
	{
		if (!s_levelState.levelScript || !funcName) { return nullptr; }
		return TFE_ForceScript::findScriptFuncByNameNoCase(s_levelState.levelScript, funcName);
	}
}
