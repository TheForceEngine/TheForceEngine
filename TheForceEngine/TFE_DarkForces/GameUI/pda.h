#pragma once
//////////////////////////////////////////////////////////////////////
// Dark Forces
// Mission Briefing
//////////////////////////////////////////////////////////////////////

#include <TFE_System/types.h>

namespace TFE_DarkForces
{
	void pda_start(const char* levelName);
	void pda_cleanup();
	void pda_resetState();
	void pda_resetTab();

	JBool pda_isOpen();
	void  pda_update();

	// TFE
	void pda_initHighResFont();
	u32 pda_blendRgbPixels(u32 srcPixel, u32 dstPixel);
}
