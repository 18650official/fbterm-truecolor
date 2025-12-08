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

// #define OTB_STATUS_DEBUG

static FcCharSet *unicodeMap;
static FcFontSet *fontList;
 
static FT_Library ftlib;
static FT_Face *fontFaces;
static u32 *fontFlags;

static Font::Glyph **glyphCache;
static bool *glyphCacheInited;

// static void openFont(u32 index);

DEFINE_INSTANCE(Font)

Font *Font::createInstance()
{
    FcInit();

    s8 buf[64];
    Config::instance()->getOption("font-names", buf, sizeof(buf));

    // 如果未配置字体名，默认寻找 "mono"
    FcPattern *pat = FcNameParse((FcChar8 *)(*buf ? buf : "mono"));

    // ================= [修改重点 Start] =================
    // 只关注目标字号，不再根据 OTB 开关做特殊处理
    // ==================================================
    u32 pixel_size = 12; // 默认值
    Config::instance()->getOption("font-size", pixel_size);
	pixel_size += 2; //校正程序字体映射误差

#ifdef OTB_STATUS_DEBUG
    // [DEBUG LOG] 简化后的日志
    FILE *fp_open = fopen("/oem/.fbterm_otb.log", "a");
    if (fp_open) {
        fprintf(fp_open, "[Font::createInstance] FontConfig Init:\n");
        fprintf(fp_open, "  -> Requested Font Names: %s\n", buf);
        fprintf(fp_open, "  -> Requested Pixel Size: %d\n", pixel_size);
        fprintf(fp_open, "--------------------------------\n");
        fclose(fp_open);
    }
#endif
    // ================= [修改重点 End] ====================

    // 将目标字号告诉 FontConfig
    FcPatternAddDouble(pat, FC_PIXEL_SIZE, (double)pixel_size);

    FcPatternAddString(pat, FC_LANG, (FcChar8 *)"en");

    FcConfigSubstitute(NULL, pat, FcMatchPattern);
    FcDefaultSubstitute(pat);

    FcResult result;
    // 获取匹配到的字体列表
    FcFontSet *fs = FcFontSort(NULL, pat, FcTrue, &unicodeMap, &result);

    if (fs) {
        fontList = FcFontSetCreate();

        FcObjectSet *family = FcObjectSetCreate();
        FcObjectSetAdd(family, FC_FAMILY);

        for (u32 i = 0; i < fs->nfont; i++) {
            FcPattern *font = FcFontRenderPrepare(NULL, pat, fs->fonts[i]);
            if (!font) continue;

            // 去重逻辑：如果同名字体已经有了，就不重复添加
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

    // 如果找到了字体，创建 Font 实例 (会调用我们刚改过的 Font::Font 构造函数)
    if (fontList && fontList->nfont) return new Font();

    if (unicodeMap) FcCharSetDestroy(unicodeMap);
    if (fontList) FcFontSetDestroy(fontList);
    FcFini();
    return 0;
}

Font::Font()
{
    mHeight = mWidth = 0;
    
    // =======================================================
    // 1. 第一步：读取基础字体大小配置
    //    不再读取 otb-support 或 source-size，全部交给 openFont 智能判断
    // =======================================================
    mTargetSize = 12; // 默认兜底大小

    // 读取用户配置的字号 (例如 16)
    // 这个 mTargetSize 将成为所有字体缩放的“标准基准”
    Config::instance()->getOption("font-size", mTargetSize);
	mTargetSize += 2; //校正程序字体设定误差

    // [DEBUG LOG]
#ifdef OTB_STATUS_DEBUG
    FILE *fp_ctor = fopen("/oem/.fbterm_otb.log", "a");
    if (fp_ctor) {
        fprintf(fp_ctor, "[Font::Font] Constructor Init:\n");
        fprintf(fp_ctor, "  -> Target Size (Grid Base): %d\n", mTargetSize);
        fprintf(fp_ctor, "  -> Auto-Scaling Mode: ON (Config Independent)\n");
        fclose(fp_ctor);
    }
#endif

    // =======================================================
    // 2. 第二步：初始化 FreeType 并加载字体
    // =======================================================
    fontFaces = new FT_Face[fontList->nfont];
    fontScaleRatios = new float[fontList->nfont]; // 必须分配缩放比例数组
    fontFlags = new u32[fontList->nfont];

    // 初始化比例为 1.0 (不缩放)
    for (u32 i = 0; i < fontList->nfont; i++) 
        fontScaleRatios[i] = 1.0f;

    memset(fontFaces, 0, sizeof(FT_Face) * fontList->nfont);

    glyphCache = new Glyph *[256 * 256];
    glyphCacheInited = new bool[256];
    memset(glyphCacheInited, 0, sizeof(bool) * 256);

    FT_Init_FreeType(&ftlib);
    
    // 调用 openFont
    // 此时 openFont 会利用 this->mTargetSize 去智能匹配字体大小
    // 如果是 64px OTB，它会自动计算出 0.25 的缩放比存入 fontScaleRatios
    openFont(0);

    FT_Face face = fontFaces[0];
    if (face == (FT_Face)-1) return;

    // =======================================================
    // 3. 第三步：计算基础格子大小 (mWidth, mHeight)
    //    核心修改：强制使用 TargetSize 作为格子高度
    // =======================================================
    
    // 无论 FreeType 加载的是 64px 的原图还是 16px 的矢量，
    // 我们最终要在屏幕上画的格子高度就是 mTargetSize (比如 16)。
    // 这样避免了加载大尺寸 OTB 时，格子被意外撑大的问题。
    mHeight = mTargetSize - 2;
    
    // 计算宽度：通常终端字体宽度是高度的一半 (Standard Half-width)
    // 这对中英文混排最稳，避免了去读取 FreeType 可能返回的奇怪宽度
    mWidth = (mHeight + 1) / 2;

    // =======================================================
    // 4. 第四步：应用用户微调配置 (font-width/height)
    //    允许用户在 .fbtermrc 里微调格子大小
    // =======================================================
    u32 width_opt = 0;
    Config::instance()->getOption("font-width", width_opt);

    if (width_opt) {
        s8 buf[64];
        Config::instance()->getOption("font-width", buf, sizeof(buf));

        if (buf[0] == '+' || buf[0] == '-') mWidth += (s32)width_opt;
        else mWidth = width_opt;
    }

    u32 height_opt = 0;
    Config::instance()->getOption("font-height", height_opt);

    if (height_opt) {
        s8 buf[64];
        Config::instance()->getOption("font-height", buf, sizeof(buf));

        if (buf[0] == '+' || buf[0] == '-') mHeight += (s32)height_opt;
        else mHeight = height_opt;
    }

#ifdef OTB_STATUS_DEBUG
    if (fp_ctor = fopen("/oem/.fbterm_otb.log", "a")) {
        fprintf(fp_ctor, "  -> Final Grid Size: W=%d, H=%d\n", mWidth, mHeight);
        fclose(fp_ctor);
    }
#endif
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

	if (fontScaleRatios) 
		delete[] fontScaleRatios;

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

void Font::openFont(u32 index)
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

    // ================= [全自动智能逻辑 Start] =================

    // 1. 确定目标大小 (这是我们最终想要显示的大小，如 16)
    // 优先用全局配置的 target size，如果没有则用 fontconfig 的建议值
    u32 target_size = (mTargetSize > 0) ? mTargetSize : 16;
    double fc_size;
    if (FcPatternGetDouble(pattern, FC_PIXEL_SIZE, 0, &fc_size) == FcResultMatch) {
        if (mTargetSize == 0) target_size = (u32)fc_size;
    }

    u32 request_size = target_size; // 最终向 FreeType 请求的大小
    float ratio = 1.0f;             // 最终的缩放比例

    // 2. 决策逻辑
    if (FT_IS_SCALABLE(face)) {
        u32 safety_padding = 2; 

        if (target_size > safety_padding) {
            // 动态减小：如果是 16 就申请 14；如果是 24 就申请 22
            request_size = target_size - safety_padding;
        } else {
            // 极小字号保护
            request_size = target_size;
        }

        // 依然保持 1:1 的比例，不进行图像缩放
        ratio = 1.0f;
        
    } else {
        // [OTB/BDF 点阵字体] -> 智能寻找最佳尺寸
        
        bool found_exact = false;
        int max_size = 0;

        // 遍历所有可用尺寸
        if (face->num_fixed_sizes > 0) {
            for (int i = 0; i < face->num_fixed_sizes; i++) {
                int h = face->available_sizes[i].height;
                
                // 记录见过的最大尺寸 (作为保底)
                if (h > max_size) max_size = h;

                // 检查是否有完美匹配 (Exact Match)
                // 容差设为 0，必须严格相等
                if (h == (int)target_size) {
                    found_exact = true;
                    // 如果找到了原生 16px，不用找了，直接用
                    request_size = h;
                    ratio = 1.0f;
                    break; 
                }
            }
        }

        if (!found_exact) {
            // [没有原生尺寸] -> 启用缩放策略
            if (max_size > 0) {
                // 找到了一个最大的 (比如 64px)，用它来做超采样
                request_size = max_size;
                ratio = (float)target_size / (float)max_size;
            } else {
                // 极罕见情况：点阵字体没有任何尺寸信息，只能死马当活马医
                request_size = target_size;
                ratio = 1.0f;
            }
        }
    }

    // 3. 应用设置
    FT_Set_Pixel_Sizes(face, 0, request_size);
    
    // 4. 保存缩放比例供 getGlyph 使用
    if (fontScaleRatios) {
        fontScaleRatios[index] = ratio;
    }
    
    // ================= [全自动智能逻辑 End] =================

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

Font::Glyph *Font::getGlyph(u32 unicode)
{
    // ... (前置缓存检查代码保持不变) ...

    int i = fontIndex(unicode);
    if (i == -1) return 0;
    if (!fontFaces[i]) openFont(i);
    if (fontFaces[i] == (FT_Face)-1) return 0;

    FT_Face face = fontFaces[i];
    FT_UInt index = FT_Get_Char_Index(face, (FT_ULong)unicode);
    if (!index) return 0;

    FT_Load_Glyph(face, index, FT_LOAD_RENDER | fontFlags[i]);
    FT_Bitmap &bitmap = face->glyph->bitmap;

    // === 读取缩放比例 ===
    float ratio = fontScaleRatios[i];
    bool need_scale = (ratio < 0.99f || ratio > 1.01f); // 浮点数防抖

    // 准备原始数据
    u32 src_w = bitmap.width;
    u32 src_h = bitmap.rows;
    u32 final_w = src_w;
    u32 final_h = src_h;

    // 如果需要缩放，计算目标尺寸
    if (need_scale) {
        final_w = (u32)(src_w * ratio);
        final_h = (u32)(src_h * ratio);
        if (final_w == 0) final_w = 1;
        if (final_h == 0) final_h = 1;
    }

    // --- 构建 fbterm Glyph ---
    u32 x = 0, y = 0, nw = final_w, nh = final_h;
    Screen::instance()->rotateRect(x, y, nw, nh);

    Glyph *glyph = (Glyph *)new u8[OFFSET(Glyph, pixmap) + nw * nh];

    // --- 填充 Metrics (关键排版数据) ---
    if (need_scale) {
        // [缩放模式] 所有排版指标乘以比例
        glyph->left   = (s32)((face->glyph->metrics.horiBearingX >> 6) * ratio);
        glyph->width  = (u8)final_w;
        glyph->height = (u8)final_h;
        
        s32 scaled_descender = (s32)((face->size->metrics.descender >> 6) * ratio);
        s32 scaled_bearingY  = (s32)((face->glyph->metrics.horiBearingY >> 6) * ratio);
        glyph->top = mHeight - 1 + scaled_descender - scaled_bearingY;
    } else {
        // [原生模式] 直接读取 FreeType
        glyph->left   = face->glyph->metrics.horiBearingX >> 6;
        glyph->width  = face->glyph->metrics.width >> 6;
        glyph->height = face->glyph->metrics.height >> 6;
        glyph->top    = mHeight - 1 + (face->size->metrics.descender >> 6) - (face->glyph->metrics.horiBearingY >> 6);
    }
    glyph->pitch = nw;

    // --- 填充像素数据 ---
    u8 *dst_ptr = glyph->pixmap;

    if (need_scale) {
        // [缩放渲染]：解包 -> 双线性插值 -> 写入
        
        // 1. 临时解包源数据 (处理 MONO 格式)
        u8* temp_src = new u8[src_w * src_h];
        u8* src_ptr = bitmap.buffer;
        for (u32 r = 0; r < src_h; r++) {
            for (u32 c = 0; c < src_w; c++) {
                u8 val;
                if (bitmap.pixel_mode == FT_PIXEL_MODE_MONO) {
                    val = (src_ptr[c >> 3] & (0x80 >> (c & 7))) ? 0xFF : 0x00;
                } else {
                    val = src_ptr[c];
                }
                temp_src[r * src_w + c] = val;
            }
            src_ptr += bitmap.pitch;
        }

        // 2. 双线性插值并写入最终 buffer
        for (u32 r = 0; r < final_h; r++) {
            for (u32 c = 0; c < final_w; c++) {
                float src_x = (c + 0.5f) / ratio - 0.5f;
                float src_y = (r + 0.5f) / ratio - 0.5f;
                
                u8 val = get_pixel_bilinear(temp_src, src_w, src_h, src_x, src_y);
                
                u32 rx = c, ry = r;
                Screen::instance()->rotatePoint(final_w, final_h, rx, ry);
                dst_ptr[ry * nw + rx] = val;
            }
        }
        delete[] temp_src;

    } else {
        // [原生渲染]：直接拷贝 (带旋转处理)
        u8 *src_ptr = bitmap.buffer;
        for (u32 r = 0; r < src_h; r++, src_ptr += bitmap.pitch) {
            for (u32 c = 0; c < src_w; c++) {
                u32 rx = c, ry = r;
                Screen::instance()->rotatePoint(src_w, src_h, rx, ry);

                u8 val;
                if (bitmap.pixel_mode == FT_PIXEL_MODE_MONO) {
                    val = (src_ptr[(c >> 3)] & (0x80 >> (c & 7))) ? 0xff : 0;
                } else {
                    val = src_ptr[c];
                }
                dst_ptr[ry * nw + rx] = val;
            }
        }
    }

    glyphCache[unicode] = glyph;
    return glyph;
}