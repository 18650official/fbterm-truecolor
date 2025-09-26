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

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/kd.h>
#include <linux/fb.h>
#include "fbdev.h"
#include "font.h"

#define MIN(a,b) ((a) < (b) ? (a) : (b))
#define MAX(a,b) ((a) > (b) ? (a) : (b))

static fb_fix_screeninfo finfo;
static fb_var_screeninfo vinfo;

static s32 fbdev_fd;

FbDev *FbDev::initFbDev()
{
	s8 *fbdev = getenv("FRAMEBUFFER");

	if (fbdev) {
		fbdev_fd = open(fbdev, O_RDWR);
	} else {
		fbdev_fd = open("/dev/fb0", O_RDWR);
		if (fbdev_fd < 0) fbdev_fd = open("/dev/fb/0", O_RDWR);
	}

	if (fbdev_fd < 0) {
		fprintf(stderr, "can't open frame buffer device!\n");
		return 0;
	}

	fcntl(fbdev_fd, F_SETFD, fcntl(fbdev_fd, F_GETFD) | FD_CLOEXEC);
	ioctl(fbdev_fd, FBIOGET_FSCREENINFO, &finfo);
	ioctl(fbdev_fd, FBIOGET_VSCREENINFO, &vinfo);

	if (finfo.type != FB_TYPE_PACKED_PIXELS) {
		fprintf(stderr, "unsupported frame buffer device!\n");
		return 0;
	}

	switch (vinfo.bits_per_pixel) {
	case 8:
		if (finfo.visual != FB_VISUAL_PSEUDOCOLOR) {
			fprintf(stderr, "only support pseudo-color visual with 8bpp depth!\n");
			return 0;
		}
		break;

	case 15:
	case 16:
	case 32:
		if (finfo.visual != FB_VISUAL_TRUECOLOR && finfo.visual != FB_VISUAL_DIRECTCOLOR) {
			fprintf(stderr, "only support true-color/direct-color visual with 15/16/32bpp depth!\n");
			return 0;
		}
		break;

	default:
		fprintf(stderr, "only support frame buffer device with 8/15/16/32 color depth!\n");
		return 0;
	}

	return new FbDev();
}

FbDev::FbDev()
{
	mWidth = vinfo.xres;
	mHeight = vinfo.yres;
	mBitsPerPixel = vinfo.bits_per_pixel;
	mBytesPerLine = finfo.line_length;
	mVMemBase = (u8 *)mmap(0, finfo.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fbdev_fd, 0);


	if (mRotateType == Rotate0 || mRotateType == Rotate180) {
		bool ypan = (vinfo.yres_virtual > vinfo.yres && finfo.ypanstep && !(FH(1) % finfo.ypanstep));
		bool ywrap = (finfo.ywrapstep && !(FH(1) % finfo.ywrapstep));
		if (ywrap && !(vinfo.vmode & FB_VMODE_YWRAP)) {
			vinfo.vmode |= FB_VMODE_YWRAP;
			ioctl(fbdev_fd, FBIOPUT_VSCREENINFO, &vinfo);
			ywrap = (vinfo.vmode & FB_VMODE_YWRAP);
		}

		if ((ypan || ywrap) && !ioctl(fbdev_fd, FBIOPAN_DISPLAY, &vinfo)) {
			if (ywrap) {
				mScrollType = YWrap;
				mOffsetMax = vinfo.yres_virtual - 1;
			} else {
				mScrollType = YPan;
				mOffsetMax = vinfo.yres_virtual - vinfo.yres;
			}
		}
	}
}

FbDev::~FbDev()
{
	munmap(mVMemBase, finfo.smem_len);
	close(fbdev_fd);

	if (mScrollType != Redraw) {
		ioctl(STDIN_FILENO, KDSETMODE, KD_GRAPHICS);
		ioctl(STDIN_FILENO, KDSETMODE, KD_TEXT);
	}
}

const s8 *FbDev::drvId()
{
	return finfo.id;
}

void FbDev::setupOffset()
{
	vinfo.yoffset = mOffsetCur;
	ioctl(fbdev_fd, FBIOPAN_DISPLAY, &vinfo);
}

void FbDev::setupPalette(bool restore)
{
	if (finfo.visual == FB_VISUAL_TRUECOLOR || (!restore && !mPalette)) return;

	static bool palette_saved = false;
	static u16 saved_red[256], saved_green[256], saved_blue[256];
	u32 cols, rcols, gcols, bcols;
	fb_cmap cmap;

	#define INIT_CMAP(_red, _green, _blue) \
	do { \
		cmap.start = 0; \
		cmap.len = cols; \
		cmap.red = _red; \
		cmap.green = _green; \
		cmap.blue = _blue; \
		cmap.transp = 0; \
	} while (0)

	if (finfo.visual == FB_VISUAL_PSEUDOCOLOR) cols = NR_COLORS;
	else {
		rcols = 1 << vinfo.red.length;
		gcols = 1 << vinfo.green.length;
		bcols = 1 << vinfo.blue.length;

		cols = MAX(rcols, MAX(gcols, bcols));
	}

	if (restore) {
		if (!palette_saved) return;

		INIT_CMAP(saved_red, saved_green, saved_blue);
		ioctl(fbdev_fd, FBIOPUTCMAP, &cmap);
	} else {
		if (!palette_saved) {
			palette_saved = true;

			INIT_CMAP(saved_red, saved_green, saved_blue);
			ioctl(fbdev_fd, FBIOGETCMAP, &cmap);
		}

		u16 red[cols], green[cols], blue[cols];

		if (finfo.visual == FB_VISUAL_PSEUDOCOLOR) {
			for (u32 i = 0; i < NR_COLORS; i++) {
				red[i] = (mPalette[i].red << 8) | mPalette[i].red;
				green[i] = (mPalette[i].green << 8) | mPalette[i].green;
				blue[i] = (mPalette[i].blue << 8) | mPalette[i].blue;
			}
		} else {
			for (u32 i = 0; i < rcols; i++) {
				red[i] = (65535 / (rcols - 1)) * i;
			}

			for (u32 i = 0; i < gcols; i++) {
				green[i] = (65535 / (gcols - 1)) * i;
			}

			for (u32 i = 0; i < bcols; i++) {
				blue[i] = (65535 / (bcols - 1)) * i;
			}
		}

		INIT_CMAP(red, green, blue);
		ioctl(fbdev_fd, FBIOPUTCMAP, &cmap);
	}
}

// This is the new, enhanced, and DYNAMIC version of drawEmojiBitmap.
bool FbDev::drawEmojiBitmap(u32 x, u32 y, u32 code, u8 bc)
{
    // 1. Construct the path to the bitmap file.
    char bitmap_path[256];
    sprintf(bitmap_path, "/usr/share/emoji/%X.rgb", code);

    // 2. Open the bitmap file.
    FILE* fp = fopen(bitmap_path, "rb");
    if (!fp) {
        return false;
    }

    // ===================================================================
    // ==                     --- DYNAMIC SIZE FIX ---                  ==
    // ===================================================================
    // Use the font's actual height as the emoji size.
    // Assumes emojis are square and their size matches the font height.
    const int EMOJI_SIZE = FH(1); // Get current font height
    const int EMOJI_WIDTH = FW(2); // Emojis are double-width characters
    // Dynamically allocate the buffer on the stack (or heap if size is large)
    unsigned char emoji_buffer[EMOJI_SIZE * EMOJI_WIDTH * 3];
    // ===================================================================

    size_t bytes_to_read = sizeof(emoji_buffer);
    size_t bytes_read = fread(emoji_buffer, 1, bytes_to_read, fp);
    fclose(fp);

    if (bytes_read != bytes_to_read) {
        fillRect(x, y, FW(2), FH(1), bc);
        return false; 
    }

    // 3. Pre-fill the area with the background color.
    fillRect(x, y, FW(2), FH(1), bc);

    // 4. Get framebuffer memory address.
    unsigned short *vmem_start = (unsigned short *)mVMemBase;
    unsigned char *pixel_ptr = emoji_buffer;

    // 5. Loop through each pixel and draw non-black pixels.
    for (int row = 0; row < EMOJI_SIZE; row++) {
        if ((y + row) >= mHeight) break;

        unsigned short *dest = vmem_start + (y + row) * (mBytesPerLine / 2) + x;
        
        for (int col = 0; col < EMOJI_WIDTH; col++) {
            if ((x + col) >= mWidth) break;

            unsigned char r = *pixel_ptr++;
            unsigned char g = *pixel_ptr++;
            unsigned char b = *pixel_ptr++;
            
            if (r != 0 || g != 0 || b != 0) {
                unsigned short color16 = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
                *(dest + col) = color16;
            }
        }
    }
    return true;
}