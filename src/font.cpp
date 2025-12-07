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
#include <math.h>

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
	bool isEnableOTBSupport = false;
	Config::instance()->getOption("otb-support", isEnableOTBSupport);
	if(isEnableOTBSupport)
		Config::instance()->getOption("otb-source-size", pixel_size); //读取OTB大小进行传参
	else
		Config::instance()->getOption("font-size", pixel_size);
	
	// [DEBUG LOG START]
    FILE *fp_open = fopen("/oem/.fbterm_otb.log", "a");
    if (fp_open) {
        fprintf(fp_open, "[Font::openFont] Setting up FreeType:\n");
        fprintf(fp_open, "  -> isEnableOTBSupport: %d\n", isEnableOTBSupport);
        fprintf(fp_open, "  -> Final pixel_size passed to FT: %d\n", pixel_size);
        fprintf(fp_open, "--------------------------------\n");
        fclose(fp_open);
    }
    // [DEBUG LOG END]

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

Font::Font()
{
	mHeight = mWidth = 0;
	mOtbEnabled = false;
	mOriginOtbSize = mTargetSize = 0;

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

		FT_Bitmap_Size *sizes = face->available_sizes;
		u32 index = 0, diffmin = (u32)-1;
		for (u32 i = 0; i < face->num_fixed_sizes; i++) {
			u32 diff = SUBS(sizes[i].size >> 6, (u32)dsize);
			if (diff < diffmin ) {
				index = i;
				diffmin = diff;
			}
		}

		mHeight = sizes[index].height;
		mWidth = sizes[index].width;
	}

	if (!(face->face_flags & FT_FACE_FLAG_FIXED_WIDTH)) mWidth = MIN(mWidth, (mHeight + 1) / 2);

	// Get OTB Option
	
	Config::instance()->getOption("otb-support", mOtbEnabled);
	if(mOtbEnabled){
		// Read origin/target size
		Config::instance()->getOption("font-size", mTargetSize);
		Config::instance()->getOption("otb-source-size", mOriginOtbSize);
	}

	FILE *fp_ctor = fopen("/oem/.fbterm_otb.log", "a");
	if (fp_ctor) {
		fprintf(fp_ctor, "[Font::Font] Constructor Init:\n");
		fprintf(fp_ctor, "  -> OTB Enabled: %s\n", mOtbEnabled ? "TRUE" : "FALSE");
		fprintf(fp_ctor, "  -> Target Size: %d\n", mTargetSize);
		fprintf(fp_ctor, "  -> Origin OTB Size: %d\n", mOriginOtbSize);
		fprintf(fp_ctor, "--------------------------------\n");
		fclose(fp_ctor);
	}
	// [DEBUG LOG END]

	// Patch end

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

// 邻域扫描
static u8 get_pixel_bilinear(const u8* src, int sw, int sh, float x, float y) {
    int x1 = (int)floor(x);
    int y1 = (int)floor(y);
    int x2 = x1 + 1;
    int y2 = y1 + 1;

    // 边界钳制，防止越界
    if (x1 < 0) x1 = 0; if (y1 < 0) y1 = 0;
    if (x2 >= sw) x2 = sw - 1; if (y2 >= sh) y2 = sh - 1;

    // 获取四个邻近点的像素值
    u8 p11 = src[y1 * sw + x1]; // 左上
    u8 p21 = src[y1 * sw + x2]; // 右上
    u8 p12 = src[y2 * sw + x1]; // 左下
    u8 p22 = src[y2 * sw + x2]; // 右下

    // 计算插值权重
    float x_diff = x - x1;
    float y_diff = y - y1;

    // 双线性插值公式
    // 先在 X 轴方向插值
    float val1 = p11 * (1.0f - x_diff) + p21 * x_diff;
    float val2 = p12 * (1.0f - x_diff) + p22 * x_diff;
    
    // 再在 Y 轴方向插值
    float val = val1 * (1.0f - y_diff) + val2 * y_diff;

    return (u8)val;
}

// --- 主函数 ---
Font::Glyph *Font::getGlyph(u32 unicode)
{
    if (unicode >= 256 * 256) return 0;

    // 1. 缓存检查 (保持原样)
    if (!glyphCacheInited[unicode >> 8]) {
        glyphCacheInited[unicode >> 8] = true;
        memset(&glyphCache[unicode & 0xff00], 0, sizeof(Glyph *) * 256);
    }
    if (glyphCache[unicode]) return glyphCache[unicode];

    // 2. 加载字体 (保持原样)
    int i = fontIndex(unicode);
    if (i == -1) return 0;

    if (!fontFaces[i]) openFont(i);
    if (fontFaces[i] == (FT_Face)-1) return 0;

    FT_Face face = fontFaces[i];
    FT_UInt index = FT_Get_Char_Index(face, (FT_ULong)unicode);
    if (!index) return 0;

    // 加载字形，得到原始的大尺寸 OTB 位图
    FT_Load_Glyph(face, index, FT_LOAD_RENDER | fontFlags[i]);
    FT_Bitmap &bitmap = face->glyph->bitmap;

    // 3. 准备变量
    u32 src_w = bitmap.width;
    u32 src_h = bitmap.rows;
    u32 final_w = src_w;
    u32 final_h = src_h;
    u8* scaled_buffer = NULL;     // 用于存放缩放后的数据
    bool is_scaled = false;       // 标记是否进行了缩放
    float scale_ratio = 1.0f;     // 缩放比例

    // 4. OTB 缩放逻辑 (核心修改)
    if (mOtbEnabled && mOriginOtbSize > 0 && mTargetSize > 0 && src_w > 0 && src_h > 0) {
        
        scale_ratio = (float)mTargetSize / (float)mOriginOtbSize;
        
        // 计算目标尺寸
        final_w = (u32)(src_w * scale_ratio);
        final_h = (u32)(src_h * scale_ratio);
        
        // 防止尺寸变为 0
        if (final_w == 0) final_w = 1;
        if (final_h == 0) final_h = 1;

        // A. 解包：将 1-bit 位图转为 8-bit 临时 buffer
        u8* temp_src = new u8[src_w * src_h];
        u8* src_ptr = bitmap.buffer;
        
        for (u32 r = 0; r < src_h; r++) {
            for (u32 c = 0; c < src_w; c++) {
                u8 val = 0;
                if (bitmap.pixel_mode == FT_PIXEL_MODE_MONO) {
                    // 读取位图中的某一位：字节索引 + 位偏移
                    val = (src_ptr[c >> 3] & (0x80 >> (c & 7))) ? 0xFF : 0x00;
                } else {
                    // 如果本身就是灰度图(虽少见但兼容)
                    val = src_ptr[c]; 
                }
                temp_src[r * src_w + c] = val;
            }
            src_ptr += bitmap.pitch;
        }

        // B. 缩放：执行双线性插值
        scaled_buffer = new u8[final_w * final_h];
        is_scaled = true;

        for (u32 y = 0; y < final_h; y++) {
            for (u32 x = 0; x < final_w; x++) {
                // 坐标逆映射：目标图中心对齐源图中心
                float src_x = (x + 0.5f) / scale_ratio - 0.5f;
                float src_y = (y + 0.5f) / scale_ratio - 0.5f;
                
                scaled_buffer[y * final_w + x] = get_pixel_bilinear(temp_src, src_w, src_h, src_x, src_y);
            }
        }

        delete[] temp_src; // 释放临时解包数据
    }

    // 5. 构建 fbterm Glyph 对象
    u32 x, y, nx, ny, nw, nh;
    x = y = 0;
    nw = final_w;
    nh = final_h;
    Screen::instance()->rotateRect(x, y, nw, nh); // 处理屏幕旋转

    Glyph *glyph = (Glyph *)new u8[OFFSET(Glyph, pixmap) + nw * nh];

    // 6. 填充 Metrics (排版指标)
    if (is_scaled) {
        // 【关键】对 FreeType 返回的指标进行等比例缩小
        // 注意：FreeType 的 metrics 单位通常是 26.6 固定点数 (即像素值 * 64)
        // 我们先右移 6 位得到整数像素值，再乘以缩放比例
        
        glyph->left   = (s32)((face->glyph->metrics.horiBearingX >> 6) * scale_ratio);
        glyph->width  = (u8)final_w;
        glyph->height = (u8)final_h;
        
        // Top 值的计算最为复杂，涉及到基线对齐。
        // 原始公式：mHeight - 1 + (descender) - (horiBearingY)
        // descender 和 bearingY 都需要缩放。
        s32 scaled_descender = (s32)((face->size->metrics.descender >> 6) * scale_ratio);
        s32 scaled_bearingY  = (s32)((face->glyph->metrics.horiBearingY >> 6) * scale_ratio);
        
        // mHeight 是 fbterm 这一行的总高度 (即 mTargetSize)
        // 这里假设 Font 构造函数里 mHeight 已经被正确设为 mTargetSize
        glyph->top = mHeight - 1 + scaled_descender - scaled_bearingY;
        
    } else {
        // 原有逻辑：直接使用 FreeType 数据
        glyph->left   = face->glyph->metrics.horiBearingX >> 6;
        glyph->top    = mHeight - 1 + (face->size->metrics.descender >> 6) - (face->glyph->metrics.horiBearingY >> 6);
        glyph->width  = face->glyph->metrics.width >> 6;
        glyph->height = face->glyph->metrics.height >> 6;
    }
    glyph->pitch = nw;

    // 7. 填充像素数据 (PixMap)
    if (is_scaled && scaled_buffer) {
        // 使用缩放后的 8-bit 数据
        u8 *buf = scaled_buffer;
        for (y = 0; y < final_h; y++) {
            for (x = 0; x < final_w; x++) {
                nx = x, ny = y;
                Screen::instance()->rotatePoint(final_w, final_h, nx, ny);
                glyph->pixmap[ny * nw + nx] = buf[y * final_w + x];
            }
        }
        delete[] scaled_buffer; // 记得释放！
    } else {
        // 原有逻辑：直接读取 bitmap.buffer 并处理单色位图
        u8 *buf = bitmap.buffer;
        for (y = 0; y < src_h; y++, buf += bitmap.pitch) {
            for (x = 0; x < src_w; x++) {
                nx = x, ny = y;
                Screen::instance()->rotatePoint(src_w, src_h, nx, ny);

                u8 val = 0;
                if (bitmap.pixel_mode == FT_PIXEL_MODE_MONO) {
                    val = (buf[(x >> 3)] & (0x80 >> (x & 7))) ? 0xff : 0;
                } else {
                    val = buf[x];
                }
                glyph->pixmap[ny * nw + nx] = val;
            }
        }
    }

    glyphCache[unicode] = glyph;
    return glyph;
}

// Font::Glyph *Font::getGlyph(u32 unicode)
// {
// 	if (unicode >= 256 * 256) return 0;

	

// 	if (!glyphCacheInited[unicode >> 8]) {
// 		glyphCacheInited[unicode >> 8] = true;
// 		memset(&glyphCache[unicode & 0xff00], 0, sizeof(Glyph *) * 256);
// 	}

// 	if (glyphCache[unicode]) return glyphCache[unicode];

// 	int i = fontIndex(unicode);
// 	if (i == -1) return 0;

// 	if (!fontFaces[i]) openFont(i);
// 	if (fontFaces[i] == (FT_Face)-1) return 0;

// 	FT_Face face = fontFaces[i];
// 	FT_UInt index = FT_Get_Char_Index(face, (FT_ULong)unicode);
// 	if (!index) return 0;

// 	FT_Load_Glyph(face, index, FT_LOAD_RENDER | fontFlags[i]);
// 	FT_Bitmap &bitmap = face->glyph->bitmap;

// 	u32 x, y, w, h, nx, ny, nw, nh;
// 	x = y = 0;
// 	w = nw = bitmap.width;
// 	h = nh = bitmap.rows;
// 	Screen::instance()->rotateRect(x, y, nw, nh);

// 	Glyph *glyph = (Glyph *)new u8[OFFSET(Glyph, pixmap) + nw * nh];
// 	glyph->left = face->glyph->metrics.horiBearingX >> 6;
// 	glyph->top = mHeight - 1 + (face->size->metrics.descender >> 6) - (face->glyph->metrics.horiBearingY >> 6);
// 	glyph->width = face->glyph->metrics.width >> 6;
// 	glyph->height = face->glyph->metrics.height >> 6;
// 	glyph->pitch = nw;

// 	u8 *buf = bitmap.buffer;
// 	for (y = 0; y < h; y++, buf += bitmap.pitch) {
// 		for (x = 0; x < w; x++) {
// 			nx = x, ny = y;
// 			Screen::instance()->rotatePoint(w, h, nx, ny);

// 			glyph->pixmap[ny * nw + nx] =
// 				(bitmap.pixel_mode == FT_PIXEL_MODE_MONO) ? ((buf[(x >> 3)] & (0x80 >> (x & 7))) ? 0xff : 0) : buf[x];
// 		}
// 	}

// 	glyphCache[unicode] = glyph;
// 	return glyph;
// }
