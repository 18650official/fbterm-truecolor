/*
 *   Copyright © 2008-2010 dragchan <zgchan317@gmail.com>
 *   This file is part of FbTerm.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation; either version 2
 *   of the License, or (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <fontconfig/fontconfig.h>
#include <ft2build.h>
#include FT_GLYPH_H
#include "font.h"
#include "screen.h"
#include "fbconfig.h"
#include <stdio.h>

#define OFFSET(TYPE, MEMBER) ((size_t)(&(((TYPE *)0)->MEMBER)))
#define SUBS(a, b) ((a) > (b) ? (a) - (b) : (b) - (a))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

static FcCharSet *unicodeMap;
static FcFontSet *fontList;
 
static FT_Library ftlib;
static FT_Face *fontFaces;
static u32 *fontFlags;

static Font::Glyph **glyphCache;
static bool *glyphCacheInited;

static void openFont(u32 index);

DEFINE_INSTANCE(Font)

Font *Font::createInstance()
{
	FcInit();

	s8 buf[64];
	Config::instance()->getOption("font-names", buf, sizeof(buf));

	FcPattern *pat = FcNameParse((FcChar8 *)(*buf ? buf : "mono"));

	u32 pixel_size = 12;
	Config::instance()->getOption("font-size", pixel_size);
	FcPatternAddDouble(pat, FC_PIXEL_SIZE, (double)pixel_size);

	FcPatternAddString(pat, FC_LANG, (FcChar8 *)"en");

	FcConfigSubstitute(NULL, pat, FcMatchPattern);
	FcDefaultSubstitute(pat);

	FcResult result;
	FcFontSet *fs = FcFontSort(NULL, pat, FcTrue, &unicodeMap, &result);

	if (fs) {
		fontList = FcFontSetCreate();

		FcObjectSet *family = FcObjectSetCreate();
		FcObjectSetAdd(family, FC_FAMILY);

		for (u32 i = 0; i < fs->nfont; i++) {
			FcPattern *font = FcFontRenderPrepare(NULL, pat, fs->fonts[i]);
			if (!font) continue;

			bool same = false;
			for (u32 j = 0; j < fontList->nfont; j++) {
				if (FcPatternEqualSubset(fontList->fonts[j], font, family)) {
					same = true;
					break;
				}
			}

			if (same) {
				FcPatternDestroy(font);
			} else {
				FcFontSetAdd(fontList, font);
			}
		}

		FcObjectSetDestroy(family);
	}

	FcPatternDestroy(pat);
	if (fs) FcFontSetDestroy(fs);

	if (fontList && fontList->nfont) return new Font();

	if (unicodeMap) FcCharSetDestroy(unicodeMap);
	if (fontList) FcFontSetDestroy(fontList);
	FcFini();
	return 0;
}

// Bilinear interpolation for scaling glyph pixmaps
// Parameters:
//   src: source image buffer
//   srcW, srcH: source dimensions
//   srcPitch: source row stride in bytes
//   dst: destination image buffer
//   dstW, dstH: destination dimensions
//   dstPitch: destination row stride in bytes
// Uses bilinear interpolation to smoothly scale grayscale bitmaps
static void bilinearScale(const u8 *src, u32 srcW, u32 srcH, u32 srcPitch,
                          u8 *dst, u32 dstW, u32 dstH, u32 dstPitch)
{
	if (srcW == 0 || srcH == 0 || dstW == 0 || dstH == 0) return;
	
	for (u32 dy = 0; dy < dstH; dy++) {
		for (u32 dx = 0; dx < dstW; dx++) {
			double sx, sy;
			
			if (dstW == 1) {
				sx = (srcW - 1) / 2.0;
			} else {
				sx = (double)dx * (srcW - 1) / (dstW - 1);
			}
			
			if (dstH == 1) {
				sy = (srcH - 1) / 2.0;
			} else {
				sy = (double)dy * (srcH - 1) / (dstH - 1);
			}
			
			u32 x0 = (u32)sx;
			u32 y0 = (u32)sy;
			u32 x1 = MIN(x0 + 1, srcW - 1);
			u32 y1 = MIN(y0 + 1, srcH - 1);
			
			double xFrac = sx - x0;
			double yFrac = sy - y0;
			
			u8 p00 = src[y0 * srcPitch + x0];
			u8 p10 = src[y0 * srcPitch + x1];
			u8 p01 = src[y1 * srcPitch + x0];
			u8 p11 = src[y1 * srcPitch + x1];
			
			double val = p00 * (1 - xFrac) * (1 - yFrac) +
			             p10 * xFrac * (1 - yFrac) +
			             p01 * (1 - xFrac) * yFrac +
			             p11 * xFrac * yFrac;
			
			dst[dy * dstPitch + dx] = (u8)(val + 0.5);
		}
	}
}

Font::Font()
{
	mHeight = mWidth = 0;
	mSrcHeight = 0;
	mScaleRatio = 1.0;
	mNeedScale = false;

	fontFaces = new FT_Face[fontList->nfont];
	fontFlags = new u32[fontList->nfont];
	memset(fontFaces, 0, sizeof(FT_Face) * fontList->nfont);

	glyphCache = new Glyph *[256 * 256];
	glyphCacheInited = new bool[256];
	memset(glyphCacheInited, 0, sizeof(bool) * 256);

	FT_Init_FreeType(&ftlib);
	openFont(0);

	FT_Face face = fontFaces[0];
	if (face == (FT_Face)-1) return;

	if (face->face_flags & FT_FACE_FLAG_SCALABLE) {
		mHeight = face->size->metrics.height >> 6;
		mWidth = face->size->metrics.max_advance >> 6;
	} else if (face->num_fixed_sizes) {
		double dsize;
		FcPatternGetDouble(fontList->fonts[0], FC_PIXEL_SIZE, 0, &dsize);
		u32 targetHeight = (u32)dsize;

		FT_Bitmap_Size *sizes = face->available_sizes;
		u32 index = 0;
		u32 maxHeight = 0;
		
		// Select the largest available bitmap size for best scaling quality
		for (u32 i = 0; i < face->num_fixed_sizes; i++) {
			if (sizes[i].height > maxHeight) {
				index = i;
				maxHeight = sizes[i].height;
			}
		}

		mSrcHeight = sizes[index].height;
		u32 srcWidth = sizes[index].width;
		
		// Check if scaling is needed
		if (mSrcHeight != targetHeight) {
			mNeedScale = true;
			mScaleRatio = (double)targetHeight / mSrcHeight;
			mHeight = targetHeight;
			mWidth = (u32)(srcWidth * mScaleRatio + 0.5);
		} else {
			mHeight = mSrcHeight;
			mWidth = srcWidth;
		}
	}

	if (!(face->face_flags & FT_FACE_FLAG_FIXED_WIDTH)) mWidth = MIN(mWidth, (mHeight + 1) / 2);

	u32 width = 0;
	Config::instance()->getOption("font-width", width);

	if (width) {
		s8 buf[64];
		Config::instance()->getOption("font-width", buf, sizeof(buf));

		if (buf[0] == '+' || buf[0] == '-') mWidth += (s32)width;
		else mWidth = width;
	}

	u32 height = 0;
	Config::instance()->getOption("font-height", height);

	if (height) {
		s8 buf[64];
		Config::instance()->getOption("font-height", buf, sizeof(buf));

		if (buf[0] == '+' || buf[0] == '-') mHeight += (s32)height;
		else mHeight = height;
	}
}

Font::~Font()
{
	for (u32 i = 0; i < 256; i++) {
		if (!glyphCacheInited[i]) continue;

		for (u32 j = 0; j < 256; j++) {
			if (glyphCache[i * 256 + j]) {
				delete[] (u8 *)glyphCache[i * 256 + j];
			}
		}
	}

	delete[] glyphCache;
	delete[] glyphCacheInited;

	for (u32 i = 0; i < fontList->nfont; i++) {
		if (fontFaces[i] && fontFaces[i] != (FT_Face)-1) {
			FT_Done_Face(fontFaces[i]);
		}
	}

	delete[] fontFaces;
	delete[] fontFlags;

	FT_Done_FreeType(ftlib);
	FcCharSetDestroy(unicodeMap);
	FcFontSetDestroy(fontList);
	FcFini();
}

void Font::showInfo(bool verbose)
{
	if (!verbose) return;

	printf("[font] width: %dpx, height: %dpx, ordered list: ", mWidth, mHeight);

	u32 index;
	FcChar8 *family;
	for (index = 0; index < fontList->nfont - 1; index++) {
		FcPatternGetString(fontList->fonts[index], FC_FAMILY, 0, &family);
		printf("%s, ", family);
	}

	FcPatternGetString(fontList->fonts[index], FC_FAMILY, 0, &family);
	printf("%s\n", family);
}

static void openFont(u32 index)
{
	if (index >= fontList->nfont) return;

	FcPattern *pattern = fontList->fonts[index];

	FcChar8 *name = (FcChar8 *)"";
	FcPatternGetString(pattern, FC_FILE, 0, &name);

	int id = 0;
	FcPatternGetInteger (pattern, FC_INDEX, 0, &id);

	FT_Face face;
	if (FT_New_Face(ftlib, (const char *)name, id, &face)) {
		fontFaces[index] = (FT_Face)-1;
		return;
	}

	double ysize;
	FcPatternGetDouble(pattern, FC_PIXEL_SIZE, 0, &ysize);
	FT_Set_Pixel_Sizes(face, 0, (FT_UInt)ysize);

	int load_flags = FT_LOAD_DEFAULT;

	FcBool scalable, antialias;
	FcPatternGetBool(pattern, FC_SCALABLE, 0, &scalable);
	FcPatternGetBool(pattern, FC_ANTIALIAS, 0, &antialias);

	if (scalable && antialias) load_flags |= FT_LOAD_NO_BITMAP;

	if (antialias) {
		FcBool hinting;
		int hint_style;
		FcPatternGetBool(pattern, FC_HINTING, 0, &hinting);
		FcPatternGetInteger(pattern, FC_HINT_STYLE, 0, &hint_style);

		if (!hinting || hint_style == FC_HINT_NONE) {
			load_flags |= FT_LOAD_NO_HINTING;
		} else {
			load_flags |= FT_LOAD_TARGET_LIGHT;
		}
	} else {
		load_flags |= FT_LOAD_TARGET_MONO;
	}

	fontFaces[index] = face;
	fontFlags[index] = load_flags;
}

static int fontIndex(u32 unicode)
{
	if (!FcCharSetHasChar(unicodeMap, unicode)) return -1;

	FcCharSet *charset;
	for (u32 i = 0; i < fontList->nfont; i++) {
		FcPatternGetCharSet(fontList->fonts[i], FC_CHARSET, 0, &charset);
		if (FcCharSetHasChar(charset, unicode)) return i;
	}

	return -1;
}

Font::Glyph *Font::getGlyph(u32 unicode)
{
	if (unicode >= 256 * 256) return 0;

	if (!glyphCacheInited[unicode >> 8]) {
		glyphCacheInited[unicode >> 8] = true;
		memset(&glyphCache[unicode & 0xff00], 0, sizeof(Glyph *) * 256);
	}

	if (glyphCache[unicode]) return glyphCache[unicode];

	int i = fontIndex(unicode);
	if (i == -1) return 0;

	if (!fontFaces[i]) openFont(i);
	if (fontFaces[i] == (FT_Face)-1) return 0;

	FT_Face face = fontFaces[i];
	FT_UInt index = FT_Get_Char_Index(face, (FT_ULong)unicode);
	if (!index) return 0;

	FT_Load_Glyph(face, index, FT_LOAD_RENDER | fontFlags[i]);
	FT_Bitmap &bitmap = face->glyph->bitmap;

	u32 x, y, w, h, nx, ny, nw, nh;
	x = y = 0;
	w = nw = bitmap.width;
	h = nh = bitmap.rows;
	Screen::instance()->rotateRect(x, y, nw, nh);

	Glyph *glyph = (Glyph *)new u8[OFFSET(Glyph, pixmap) + nw * nh];
	glyph->left = face->glyph->metrics.horiBearingX >> 6;
	glyph->top = mHeight - 1 + (face->size->metrics.descender >> 6) - (face->glyph->metrics.horiBearingY >> 6);
	glyph->width = face->glyph->metrics.width >> 6;
	glyph->height = face->glyph->metrics.height >> 6;
	glyph->pitch = nw;

	u8 *buf = bitmap.buffer;
	for (y = 0; y < h; y++, buf += bitmap.pitch) {
		for (x = 0; x < w; x++) {
			nx = x, ny = y;
			Screen::instance()->rotatePoint(w, h, nx, ny);

			glyph->pixmap[ny * nw + nx] =
				(bitmap.pixel_mode == FT_PIXEL_MODE_MONO) ? ((buf[(x >> 3)] & (0x80 >> (x & 7))) ? 0xff : 0) : buf[x];
		}
	}

	// Apply bilinear scaling if needed for bitmap fonts
	// Only scale if primary font needs scaling and current font is also bitmap
	if (mNeedScale && !(face->face_flags & FT_FACE_FLAG_SCALABLE)) {
		// Calculate scaled dimensions
		u32 scaledW = (u32)(nw * mScaleRatio + 0.5);
		u32 scaledH = (u32)(nh * mScaleRatio + 0.5);
		
		// Create new scaled glyph
		Glyph *scaledGlyph = (Glyph *)new u8[OFFSET(Glyph, pixmap) + scaledW * scaledH];
		
		// Scale the pixmap using bilinear interpolation
		bilinearScale(glyph->pixmap, nw, nh, nw,
		              scaledGlyph->pixmap, scaledW, scaledH, scaledW);
		
		// Adjust metrics
		scaledGlyph->left = (s16)(glyph->left * mScaleRatio + 0.5);
		scaledGlyph->top = (s16)(glyph->top * mScaleRatio + 0.5);
		scaledGlyph->width = (s16)(glyph->width * mScaleRatio + 0.5);
		scaledGlyph->height = (s16)(glyph->height * mScaleRatio + 0.5);
		scaledGlyph->pitch = scaledW;
		
		// Delete original glyph and use scaled one
		delete[] (u8 *)glyph;
		glyph = scaledGlyph;
	}

	glyphCache[unicode] = glyph;
	return glyph;
}
