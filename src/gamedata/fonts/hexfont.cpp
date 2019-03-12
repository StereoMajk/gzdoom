/*
** bdffont.cpp
** Management for the VGA consolefont
**
**---------------------------------------------------------------------------
** Copyright 2019 Christoph Oelckers
** All rights reserved.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions
** are met:
**
** 1. Redistributions of source code must retain the above copyright
**    notice, this list of conditions and the following disclaimer.
** 2. Redistributions in binary form must reproduce the above copyright
**    notice, this list of conditions and the following disclaimer in the
**    documentation and/or other materials provided with the distribution.
** 3. The name of the author may not be used to endorse or promote products
**    derived from this software without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
** IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
** OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
** IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
** INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
** NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
** THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**---------------------------------------------------------------------------
**
*/

#include "doomerrors.h"
#include "textures.h"
#include "image.h"
#include "v_font.h"
#include "w_wad.h"
#include "utf8.h"
#include "sc_man.h"

#include "fontinternals.h"

// This is a font character that reads RLE compressed data.
class FHexFontChar : public FImageSource
{
public:
	FHexFontChar(uint8_t *sourcedata, int swidth, int width, int height);

	TArray<uint8_t> CreatePalettedPixels(int conversion) override;

protected:
	int SourceWidth;
	const uint8_t *SourceData;
};


//==========================================================================
//
// FHexFontChar :: FHexFontChar
//
// Used by HEX fonts.
//
//==========================================================================

FHexFontChar::FHexFontChar (uint8_t *sourcedata, int swidth, int width, int height)
: SourceData (sourcedata)
{
	SourceWidth = swidth;
	Width = width;
	Height = height;
	LeftOffset = 0;
	TopOffset = 0;
}

//==========================================================================
//
// FHexFontChar :: Get8BitPixels
//
// The render style has no relevance here.
//
//==========================================================================

TArray<uint8_t> FHexFontChar::CreatePalettedPixels(int)
{
	int destSize = Width * Height;
	TArray<uint8_t> Pixels(destSize, true);
	uint8_t *dest_p = Pixels.Data();
	const uint8_t *src_p = SourceData;

	memset(dest_p, 0, destSize);
	for (int y = 0; y < Height; y++)
	{
		for (int x = 0; x < SourceWidth; x++)
		{
			int byte = *src_p++;
			uint8_t *pixelstart = dest_p + 8 * x * Height + y;
			for (int bit = 0; bit < 8; bit++)
			{
				if (byte & (128 >> bit))
				{
					pixelstart[bit*Height] = y+2;
					// Add a shadow at the bottom right, similar to the old console font.
					if (y != Height - 1)
					{
						pixelstart[bit*Height + Height + 1] = 1;
					}
				}
			}
		}
	}
	return Pixels;
}



class FHexFont : public FFont
{
	TArray<uint8_t> glyphdata;
	unsigned glyphmap[65536] = {};
	
public:
	//==========================================================================
	//
	// parse a HEX font
	//
	//==========================================================================

	void ParseDefinition(int lumpnum)
	{
		FScanner sc;
		
		FirstChar = INT_MAX;
		LastChar = INT_MIN;
		sc.OpenLumpNum(lumpnum);
		sc.SetCMode(true);
		glyphdata.Push(0);	// ensure that index 0 can be used as 'not present'.
		while (sc.GetString())
		{
			int codepoint = (int)strtoull(sc.String, nullptr, 16);
			sc.MustGetStringName(":");
			sc.MustGetString();
			if (codepoint >= 0 && codepoint < 65536 && !sc.Compare("00000000000000000000000000000000"))	// don't set up empty glyphs.
			{
				unsigned size = (unsigned)strlen(sc.String);
				unsigned offset = glyphdata.Reserve(size/2 + 1);
				glyphmap[codepoint] = offset;
				glyphdata[offset++] = size / 2;
				for(unsigned i = 0; i < size; i+=2)
				{
					char hex[] = { sc.String[i], sc.String[i+1], 0 };
					glyphdata[offset++] = (uint8_t)strtoull(hex, nullptr, 16);
				}
				if (codepoint < FirstChar) FirstChar = codepoint;
				if (codepoint > LastChar) LastChar = codepoint;
			}
		}
	}
	
	//==========================================================================
	//
	// FHexFont :: FHexFont
	//
	// Loads a HEX font
	//
	//==========================================================================

	FHexFont (const char *fontname, int lump)
		: FFont(lump)
	{
		assert(lump >= 0);

		FontName = fontname;
		
		ParseDefinition(lump);

		Next = FirstFont;
		FirstFont = this;
		FontHeight = 16;
		SpaceWidth = 9;
		GlobalKerning = 0;
		translateUntranslated = true;
		
		LoadTranslations();
	}

	//==========================================================================
	//
	// FHexFont :: LoadTranslations
	//
	//==========================================================================

	void LoadTranslations()
	{
		const int spacing = 9;
		double luminosity[256];

		memset (PatchRemap, 0, 256);
		for (int i = 0; i < 18; i++) 
		{	
			// Create a gradient similar to the old console font.
			PatchRemap[i] = i;
			luminosity[i] = i == 1? 0.01 : 0.5 + (i-2) * (0.5 / 17.);
		}
		ActiveColors = 18;
		
		Chars.Resize(LastChar - FirstChar + 1);
		for (int i = FirstChar; i <= LastChar; i++)
		{
			if (glyphmap[i] > 0)
			{
				auto offset = glyphmap[i];
				int size = glyphdata[offset] / 16;
				Chars[i - FirstChar].TranslatedPic = new FImageTexture(new FHexFontChar (&glyphdata[offset+1], size, size * 9, 16));
				Chars[i - FirstChar].TranslatedPic->SetUseType(ETextureType::FontChar);
				Chars[i - FirstChar].XMove = size * spacing;
				TexMan.AddTexture(Chars[i - FirstChar].TranslatedPic);
			}
			else Chars[i - FirstChar].XMove = spacing;

		}
		BuildTranslations (luminosity, nullptr, &TranslationParms[1][0], ActiveColors, nullptr);
	}
	
};


//==========================================================================
//
//
//
//==========================================================================

FFont *CreateHexLumpFont (const char *fontname, int lump)
{
	return new FHexFont(fontname, lump);
}
