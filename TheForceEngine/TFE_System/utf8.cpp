#include <cstring>

#include "parser.h"
#include <assert.h>
#include <algorithm>

s32 convertCharToUtf8(char c, char* out)
{
	// Non-extended ASCII
	if (c >= 0)
	{
		out[0] = c;
		return 1;
	}
	// Extended ASCII encoded as two bytes.
	u8 code = (u8)c;

	// OK this is a fix for the trademark ND added to the GOG-only version
	// which has compatibility issues in NT vs *nix systems.
	// I can't believe we are writing code to satisfy 
	// Disney Star Wars marketing but here we are ¯\_(ツ)_/¯  
	if (code == 153) 
	{
		out[0] = (char)0xE2;
		out[1] = (char)0x84;
		out[2] = (char)0xA2;
		return 3;
	}

	out[0] = (char)(0xc0 + (code >> 6));
	out[1] = (char)(0x80 + (code & 0x3f));
	return 2;
}

s32 convertUtf8ToChar(const char* data, char* c)
{
	if (!c) { return 0; }
	if (!data || data[0] == 0) { *c = 0; return 0; }

	// Non-extended ASCII
	if (data[0] >= 0)
	{
		c[0] = data[0];
		return 1;
	}
	// Variable size codes.
	u8 code0 = (u8)data[0];
	if ((code0 & 0xe0) == 0xc0)
	{
		u8 code1 = (u8)data[1];
		code0 &= 0xbf;
		code1 &= 0x7f;
		*c = char(code1 | (code0 << 6));
		return 2;
	}
	else if ((code0 & 0xf0) == 0xe0)
	{
		u8 code1 = (u8)data[1];
		u8 code2 = (u8)data[2];
		code0 &= 0x0f;
		code1 &= 0x3f;
		code2 &= 0x3f;
		u32 code = code2 | (code1 << 6) | (code0 << 12);
		if (code > 255)
		{
			// Hack to handle 'TM'
			if (code == 8482)
			{
				*c = char(153);	// extended ascii code.
			}
			else
			{
				*c = '?';
			}
		}
		else
		{
			*c = char(code);
		}
		return 3;
	}
	else if ((code0 & 0xf8) == 0xf0)
	{
		u8 code1 = (u8)data[1];
		u8 code2 = (u8)data[2];
		u8 code3 = (u8)data[3];
		code0 &= 0x07;
		code1 &= 0x3f;
		code2 &= 0x3f;
		code3 &= 0x3f;
		u32 code = code3 | (code2 << 6) | (code1 << 12) | (code0 << 18);
		if (code > 255)
		{
			// Hack to handle 'TM'
			if (code == 8482)
			{
				*c = char(153);	// extended ascii code.
			}
			else
			{
				*c = '?';
			}
		}
		else
		{
			*c = char(code);
		}
		return 4;
	}
	// Invalid code.
	assert(0);
	*c = 0;
	return 0;
}

void convertExtendedAsciiToUtf8(const char* str, char* utf)
{
    #ifndef _WIN32		
		strcpy(utf, str);
	#else
		char* out = utf;
		size_t len = strlen(str);
		for (size_t i = 0; i < len; i++)
		{
			s32 step = convertCharToUtf8(str[i], out);
			out += step;
		}
		*out = 0;
	#endif
}

void convertUtf8ToExtendedAscii(const char* utf, char* str)
{
	#ifndef _WIN32
		strcpy(str, utf);
	#else
		char* out = str;
		size_t len = strlen(utf);
		size_t index = 0;
		for (size_t i = 0; i < len && index < len; i++)
		{
			s32 step = convertUtf8ToChar(utf, out);
			utf += step;
			index += step;
			out++;
		}
		*out = 0;
	#endif
}