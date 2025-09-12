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

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <ctime>
#include "screen.h"
#include "font.h"
#include "fbshellman.h"
#include "fbconfig.h"
#include "fbdev.h"
#include "config.h"
#ifdef ENABLE_VESA
#include "vesadev.h"
#endif

#define MIN(a,b) ((a) < (b) ? (a) : (b))
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#define redraw(args...) (FbShellManager::instance()->redraw(args))

static const s8 show_cursor[] = "\e[?25h";
static const s8 hide_cursor[] = "\e[?25l";
static const s8 disable_blank[] = "\e[9;0]";
static const s8 enable_blank[] = "\e[9;10]";
static const s8 clear_screen[] = "\e[2J\e[H";

DEFINE_INSTANCE(Screen)

Screen *Screen::createInstance()
{
	if (!Font::instance() || !FW(1) || !FH(1)) {
		fprintf(stderr, "init font error!\n");
		return 0;
	}

	Screen *pScreen = 0;

#ifdef ENABLE_VESA
	s8 buf[16];
	Config::instance()->getOption("vesa-mode", buf, sizeof(buf));
	if (!strcmp(buf, "list")) {
		VesaDev::printModes();
		return 0;
	}

	u32 mode = 0;
	Config::instance()->getOption("vesa-mode", mode);

	if (!mode) pScreen = FbDev::initFbDev();
	if (!pScreen) pScreen = VesaDev::initVesaDev(mode);
#else
	pScreen = FbDev::initFbDev();
#endif

	if (!pScreen) return 0;

	if (pScreen->mRotateType == Rotate90 || pScreen->mRotateType == Rotate270) {
		u32 tmp = pScreen->mWidth;
		pScreen->mWidth = pScreen->mHeight;
		pScreen->mHeight = tmp;
	}

	if (!pScreen->mCols) pScreen->mCols = pScreen->mWidth / FW(1);
	if (!pScreen->mRows) pScreen->mRows = pScreen->mHeight / FH(1);

	if (!pScreen->mCols || !pScreen->mRows) {
		fprintf(stderr, "font size is too huge!\n");
		delete pScreen;
		return 0;
	}

	pScreen->initFillDraw();
	return pScreen;
}

Screen::Screen()
{
	mWidth = mHeight = 0;
	mCols = mRows = 0;
	mBitsPerPixel = mBytesPerLine = 0;

	mScrollEnable = true;
	mScrollType = Redraw;
	mOffsetMax = 0;
	mOffsetCur = 0;

	mVMemBase = 0;
	mPalette = 0;

	u32 type = Rotate0;
	Config::instance()->getOption("screen-rotate", type);
	if (type > Rotate270) type = Rotate0;
	mRotateType = (RotateType)type;

	s32 ret = write(STDIN_FILENO, hide_cursor, sizeof(hide_cursor) - 1);
	ret = write(STDIN_FILENO, disable_blank, sizeof(disable_blank) - 1);
}

Screen::~Screen()
{
	Font::uninstance();
	endFillDraw();

	s32 ret = write(STDIN_FILENO, show_cursor, sizeof(show_cursor) - 1);
	ret = write(STDIN_FILENO, enable_blank, sizeof(enable_blank) - 1);
	ret = write(STDIN_FILENO, clear_screen, sizeof(clear_screen) - 1);
}

void Screen::showInfo(bool verbose)
{
	if (!verbose) return;

	static const s8* const scrollstr[4] = {
		"redraw", "ypan", "ywrap", "xpan"
	};
	printf("[screen] driver: %s, mode: %dx%d-%dbpp, scrolling: %s\n",
		drvId(), mWidth, mHeight, mBitsPerPixel, scrollstr[mScrollType]);
}

void Screen::switchVc(bool enter)
{
	mOffsetCur = 0;
	setupOffset();

	setupPalette(!enter);
	if (enter && mPalette) eraseMargin(true, mRows);
}

bool Screen::move(u16 scol, u16 srow, u16 dcol, u16 drow, u16 w, u16 h)
{
	if (!mScrollEnable || mScrollType == Redraw || scol != dcol) return false;

	u16 top = MIN(srow, drow), bot = MAX(srow, drow) + h;
	u16 left = scol, right = scol + w;

	u32 noaccel_redraw_area = w * (bot - top - 1);
	u32 accel_redraw_area = mCols * mRows - w * h;

	if (noaccel_redraw_area <= accel_redraw_area) return false;

	if (mRotateType == Rotate0 || mRotateType == Rotate270) mOffsetCur += FH((s32)srow - drow);
	else mOffsetCur -= FH((s32)srow - drow);

	bool redraw_all = false;
	if (mScrollType == YPan || mScrollType == XPan) {
		redraw_all = true;

		if (mOffsetCur < 0) mOffsetCur = mOffsetMax;
		else if ((u32)mOffsetCur > mOffsetMax) mOffsetCur = 0;
		else redraw_all = false;
	} else {
		if (mOffsetCur < 0) mOffsetCur += mOffsetMax + 1;
		else if ((u32)mOffsetCur > mOffsetMax) mOffsetCur -= mOffsetMax + 1;
	}

	setupOffset();

	if (top) redraw(0, 0, mCols, top);
	if (bot < mRows) redraw(0, bot, mCols, mRows - bot);
	if (left > 0) redraw(0, top, left, bot - top - 1);
	if (right < mCols) redraw(right, top, mCols - right, bot - top - 1);

	if (redraw_all) {
		eraseMargin(true, mRows);
	} else {
		eraseMargin(drow > srow, drow > srow ? (drow - srow) : (srow - drow));
	}

	return !redraw_all;
}

void Screen::eraseMargin(bool top, u16 h)
{
	if (mWidth % FW(1)) {
		fillRect(FW(mCols), top ? 0 : FH(mRows - h), mWidth % FW(1), FH(h), 0);
	}

	if (mHeight % FH(1)) {
		fillRect(0, FH(mRows), mWidth, mHeight % FH(1), 0);
	}
}

void Screen::drawText(u32 x, u32 y, u8 fc, u8 bc, u16 num, u32 *text, bool *dw)
{
	u32 startx, fw = FW(1);

	u16 startnum; 
	u32 *starttext;
	bool *startdw, draw_space = false, draw_text = false;

	for (; num; num--, text++, dw++, x += fw) {
		if (*text == 0x20) {
			if (draw_text) {
				draw_text = false;
				drawGlyphs(startx, y, fc, bc, startnum - num, starttext, startdw);
			}

			if (!draw_space) {
				draw_space = true;
				startx = x;
			}
		} else {
			if (draw_space) {
				draw_space = false;
				fillRect(startx, y, x - startx, FH(1), bc);
			}

			if (!draw_text) {
				draw_text = true;
				starttext = text;
				startdw = dw;
				startnum = num;
				startx = x;
			}

			if (*dw) x += fw;
		}
	}

	if (draw_text) {
		drawGlyphs(startx, y, fc, bc, startnum - num, starttext, startdw);
	} else if (draw_space) {
		fillRect(startx, y, x - startx, FH(1), bc);
	}
}

// True-color draw function
// screen.cpp, add this new function somewhere

void Screen::drawTextTrueColor(u32 x, u32 y, VTerm::CharAttr attr, u16 num, u32 *text, bool *dw)
{
    u32 startx, fw = FW(1);
    u32 startnum, *starttext;
    bool *startdw, draw_space = false, draw_text = false;

    // This loop logic is identical to the original drawText
    for (; num; num--, text++, dw++, x += fw) {
        if (*text == 0x20) { // It's a space
            if (draw_text) {
                draw_text = false;
                drawGlyphsTrueColor(startx, y, attr, startnum - num, starttext, startdw);
            }
            if (!draw_space) {
                draw_space = true;
                startx = x;
            }
        } else { // It's a character
            if (draw_space) {
                draw_space = false;
                fillRectWithAttr(startx, y, x - startx, FH(1), attr);
            }
            if (!draw_text) {
                draw_text = true;
                starttext = text;
                startdw = dw;
                startnum = num;
                startx = x;
            }
            if (*dw) x += fw;
        }
    }

    // Draw any remaining text or spaces
    if (draw_text) {
        drawGlyphsTrueColor(startx, y, attr, startnum - num, starttext, startdw);
    } else if (draw_space) {
        fillRectWithAttr(startx, y, x - startx, FH(1), attr);
    }
}

void Screen::drawGlyphs(u32 x, u32 y, u8 fc, u8 bc, u16 num, u32 *text, bool *dw)
{
	for (; num--; text++, dw++) {
		bool is_emoji = false;
		u32 c = *text;

		if ((c >= 0x1F300 && c <= 0x1F9FF) || // Main Emoji & Pictographs blocks
            (c >= 0x2600 && c <= 0x27BF))     // Older symbols often used as emoji
        {
            is_emoji = 1; // It's an emoji, set the flag!
        } else {
            is_emoji = 0; // It's a normal character, clear the flag.
        }

		if(!is_emoji) drawGlyph(x, y, fc, bc, (u16)*text, *dw); // 绘制常规字体
		else{
			// 绘制emoji
			bool r = drawEmojiBitmap(x, y, c);
			if(!r) drawGlyph(x, y, fc, bc, (u16)*text, *dw);
			*dw = 1;
		}
		x += *dw ? FW(2) : FW(1);
	}
}

// // This is Miku's ultimate version of drawGlyphs.
// // It includes black box logging and a fallback mechanism.
// void Screen::drawGlyphs(u32 x, u32 y, u8 fc, u8 bc, u16 num, u32 *text, bool *dw)
// {
//     // --- Miku's Black Box Recorder ---
//     // We use a static FILE pointer so we only have to open the file once
//     // for the entire lifetime of the program. This is much more efficient.
//     static FILE* log_fp = NULL;
//     if (log_fp == NULL) {
//         // Open the log file in append mode ("a").
//         // If it fails, log_fp will remain NULL and we'll skip logging.
//         log_fp = fopen("/root/fbterm_log.txt", "a");
//         if (log_fp) {
//             fprintf(log_fp, "\n\n--- FBTERM LOG STARTED AT %ld ---\n", time(NULL));
//         }
//     }
//     // --- End of Recorder Setup ---

//     for (; num--; text++, dw++) {
//         // Dereference the pointer to get the full 32-bit Unicode code point.
//         u32 c = *text;

//         // Log the character code to our file for analysis.
//         if (log_fp) {
//             fprintf(log_fp, "drawGlyphs received code: 0x%X\n", c);
//             // For immediate debugging, you can uncomment the next line, but it will be very slow.
//             // fflush(log_fp);
//         }

//         // Miku's double-check emoji determination logic.
//         bool is_emoji = ((c >= 0x1F300 && c <= 0x1F9FF) ||
//                          (c >= 0x2600 && c <= 0x27BF));

//         if (is_emoji) {
          
//             drawEmojiBitmap(x, y, c);
            
//         } else {
//             // --- Standard Character Path ---
//             // If it's not an emoji, use the standard glyph drawing function.
//             // We cast `c` to u16 here. This is safe because all non-emoji characters
//             // we care about (like CJK) have code points that fit within a u16.
//             drawGlyph(x, y, fc, bc, (u16)c, *dw);
//         }
        
//         // Advance the cursor based on the character width.
//         x += *dw ? FW(2) : FW(1);
//     }
// }

// 24-bit support
void Screen::drawGlyphsTrueColor(u32 x, u32 y, VTerm::CharAttr attr, u16 num, u32 *text, bool *dw)
{
    for (; num--; text++, dw++) {
        drawGlyphTrueColor(x, y, attr, *text, *dw);
        x += *dw ? FW(2) : FW(1);
    }
}

void Screen::adjustOffset(u32 &x, u32 &y)
{
	if (mScrollType == XPan) x += mOffsetCur;
	else y += mOffsetCur;
}

void Screen::fillRect(u32 x, u32 y, u32 w, u32 h, u8 color)
{
	if (x >= mWidth || y >= mHeight || !w || !h) return;
	if (x + w > mWidth) w = mWidth - x;
	if (y + h > mHeight) h = mHeight - y;

	rotateRect(x, y, w, h);
	adjustOffset(x, y);

	for (; h--;) {
		if (mScrollType == YWrap && y > mOffsetMax) y -= mOffsetMax + 1;
		(this->*fill)(x, y++, w, color);
	}
}

// This function decides which background color to use (palette or truecolor)
void Screen::fillRectWithAttr(u32 x, u32 y, u32 w, u32 h, VTerm::CharAttr attr)
{
    u8 color_index;
    if (attr.is_truecolor & 2) { // Is background truecolor?
        // The "palette reprogramming" hack:
        // We set a high-numbered palette entry to our desired true color.
        Color c = {(u8)(attr.true_bcolor >> 16), (u8)(attr.true_bcolor >> 8), (u8)attr.true_bcolor};
        mPalette[255] = c;
        setupPalette(false); // Update the internal color table
        color_index = 255;   // Use this temporary palette index
    } else {
        color_index = attr.bcolor; // Use the normal palette index
    }
    fillRect(x, y, w, h, color_index);
}

void Screen::drawGlyph(u32 x, u32 y, u8 fc, u8 bc, u16 code, bool dw)
{
	if (x >= mWidth || y >= mHeight) return;

	s32 w = (dw ? FW(2) : FW(1)), h = FH(1);
	if (x + w > mWidth) w = mWidth - x;
	if (y + h > mHeight) h = mHeight - y;

	Font::Glyph *glyph = (Font::Glyph *)Font::instance()->getGlyph(code);
	if (!glyph) {
		fillRect(x, y, w, h, bc);
		return;
	}

	s32 top = glyph->top;
	if (top < 0) top = 0;

	s32 left = glyph->left;
	if ((s32)x + left < 0) left = -x;

	s32 width = glyph->width;
	if (width > w - left) width = w - left;
	if ((s32)x + left + width > (s32)mWidth) width = mWidth - ((s32)x + left);
	if (width < 0) width = 0;

	s32 height = glyph->height;
	if (height > h - top) height = h - top;
	if (y + top + height > mHeight) height = mHeight - (y + top);
	if (height < 0) height = 0;

	if (top) fillRect(x, y, w, top, bc);
	if (left > 0) fillRect(x, y + top, left, height, bc);

	s32 right = width + left;
	if (w > right) fillRect((s32)x + right, y + top, w - right, height, bc);

	s32 bot = top + height;
	if (h > bot) fillRect(x, y + bot, w, h - bot, bc);

	x += left;
	y += top;
	if (x >= mWidth || y >= mHeight || !width || !height) return;

	u32 nwidth = width, nheight = height;
	rotateRect(x, y, nwidth, nheight);

	u8 *pixmap = glyph->pixmap;
	u32 wdiff = glyph->width - width, hdiff = glyph->height - height;

	if (wdiff) {
		if (mRotateType == Rotate180) pixmap += wdiff;
		else if (mRotateType == Rotate270) pixmap += wdiff * glyph->pitch;
	}

	if (hdiff) {
		if (mRotateType == Rotate90) pixmap += hdiff;
		else if (mRotateType == Rotate180) pixmap += hdiff * glyph->pitch;
	}

	adjustOffset(x, y);
	for (; nheight--; y++, pixmap += glyph->pitch) {
		if ((mScrollType == YWrap) && y > mOffsetMax) y -= mOffsetMax + 1;
		(this->*draw)(x, y, nwidth, fc, bc, pixmap);
	}
}

// This is our new, standalone, "ultimate" glyph drawing function.
// It takes a full CharAttr object and can handle both palette and true color modes.
void Screen::drawGlyphTrueColor(u32 x, u32 y, VTerm::CharAttr attr, u16 code, bool dw)
{
    // --- Phase 1: Color Resolution (The "Palette Reprogramming" Hack) ---
    
    u8 final_fc = attr.fcolor; // Start with the default palette foreground color index
    u8 final_bc = attr.bcolor; // Start with the default palette background color index
    bool palette_needs_update = false;

    // Check if the foreground is in true color mode
    if (attr.is_truecolor & 1) { 
        // Unpack the 24-bit RGB color
        Color c = {(u8)(attr.true_fcolor >> 16), (u8)(attr.true_fcolor >> 8), (u8)attr.true_fcolor};
        
        // Hijack palette entry #254 and reprogram it with our true color
        mPalette[254] = c; 
        final_fc = 254; // Use this temporary index for drawing
        palette_needs_update = true;
    }

    // Check if the background is in true color mode
    if (attr.is_truecolor & 2) { 
        // Unpack the 24-bit RGB color
        Color c = {(u8)(attr.true_bcolor >> 16), (u8)(attr.true_bcolor >> 8), (u8)attr.true_bcolor};
        
        // Hijack palette entry #255 and reprogram it with our true color
        mPalette[255] = c;
        final_bc = 255; // Use this temporary index for drawing
        palette_needs_update = true;
    }

    // If we changed any palette entry, we must tell the screen renderer to update its internal color lookup table.
    if (palette_needs_update) {
        setupPalette(false); 
    }
    
    // --- Phase 2: Geometry Calculation & Rendering (Copied from original drawGlyph) ---
    // The rest of this code is the original, proven drawing logic.
    // We just replace the original 'fc' and 'bc' parameters with our 'final_fc' and 'final_bc'.
    
    if (x >= mWidth || y >= mHeight) return;

    s32 w = (dw ? FW(2) : FW(1)), h = FH(1);
    if (x + w > mWidth) w = mWidth - x;
    if (y + h > mHeight) h = mHeight - y;

    Font::Glyph *glyph = (Font::Glyph *)Font::instance()->getGlyph(code);
    if (!glyph) {
        fillRect(x, y, w, h, final_bc); // Use final_bc
        return;
    }

    s32 top = glyph->top;
    if (top < 0) top = 0;

    s32 left = glyph->left;
    if ((s32)x + left < 0) left = -x;

    s32 width = glyph->width;
    if (width > w - left) width = w - left;
    if ((s32)x + left + width > (s32)mWidth) width = mWidth - ((s32)x + left);
    if (width < 0) width = 0;

    s32 height = glyph->height;
    if (height > h - top) height = h - top;
    if (y + top + height > mHeight) height = mHeight - (y + top);
    if (height < 0) height = 0;

    if (top) fillRect(x, y, w, top, final_bc); // Use final_bc
    if (left > 0) fillRect(x, y + top, left, height, final_bc); // Use final_bc

    s32 right = width + left;
    if (w > right) fillRect((s32)x + right, y + top, w - right, height, final_bc); // Use final_bc

    s32 bot = top + height;
    if (h > bot) fillRect(x, y + bot, w, h - bot, final_bc); // Use final_bc

    x += left;
    y += top;
    if (x >= mWidth || y >= mHeight || !width || !height) return;

    u32 nwidth = width, nheight = height;
    rotateRect(x, y, nwidth, nheight);

    u8 *pixmap = glyph->pixmap;
    u32 wdiff = glyph->width - width, hdiff = glyph->height - height;

    if (wdiff) {
        if (mRotateType == Rotate180) pixmap += wdiff;
        else if (mRotateType == Rotate270) pixmap += wdiff * glyph->pitch;
    }

    if (hdiff) {
        if (mRotateType == Rotate90) pixmap += hdiff;
        else if (mRotateType == Rotate180) pixmap += hdiff * glyph->pitch;
    }

    adjustOffset(x, y);
    for (; nheight--; y++, pixmap += glyph->pitch) {
        if ((mScrollType == YWrap) && y > mOffsetMax) y -= mOffsetMax + 1;
        (this->*draw)(x, y, nwidth, final_fc, final_bc, pixmap); // Use final_fc and final_bc
    }
}

void Screen::rotateRect(u32 &x, u32 &y, u32 &w, u32 &h)
{
	u32 tmp;
	switch (mRotateType) {
	case Rotate0:
		break;

	case Rotate90:
		tmp = x;
		x = mHeight - y - h;
		y = tmp;

		tmp = w;
		w = h;
		h = tmp;
		break;

	case Rotate180:
		x = mWidth - x - w;
		y = mHeight - y - h;
		break;

	case Rotate270:
		tmp = y;
		y = mWidth - x - w;
		x = tmp;

		tmp = w;
		w = h;
		h = tmp;
		break;
	}
}

void Screen::rotatePoint(u32 W, u32 H, u32 &x, u32 &y)
{
	u32 tmp;
	switch (mRotateType) {
	case Rotate0:
		break;

	case Rotate90:
		tmp = x;
		x = H - y - 1;
		y = tmp;
		break;

	case Rotate180:
		x = W - x - 1;
		y = H - y - 1;
		break;

	case Rotate270:
		tmp = y;
		y = W - x - 1;
		x = tmp;
		break;
	}
}
