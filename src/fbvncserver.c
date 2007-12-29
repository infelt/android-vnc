/*
 * $Id$
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * This project is an adaptation of the original fbvncserver for the iPAQ
 * and Zaurus.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#include <sys/stat.h>
#include <sys/sysmacros.h>             /* For makedev() */

#include <fcntl.h>
#include <linux/fb.h>

#include <assert.h>
#include <errno.h>

/* libvncserver */
#include "rfb/rfb.h"
#include "rfb/keysym.h"

/* kbde */
#include "keycodes.h"

/*****************************************************************************/

/* Android does not use /dev/fb0. */
#define FB_DEVICE "/dev/graphics/fb0"
static struct fb_var_screeninfo scrinfo;
static int fbfd = -1;
static unsigned short int *fbmmap = MAP_FAILED;
static unsigned short int *vncbuf;
static unsigned short int *fbbuf;

/* Android already has 5900 bound natively. */
#define VNC_PORT 5901
static rfbScreenInfoPtr vncscr;

/* Keep the keyboard simulation driver open persistently. */
#define KBD_DEVICE "/dev/kbde"
static int kbdfd = -1;

/* No idea, just copied from fbvncserver as part of the frame differerencing
 * algorithm.  I will probably be later rewriting all of this. */
static struct varblock_t
{
	int min_i;
	int min_j;
	int max_i;
	int max_j;
	int r_offset;
	int g_offset;
	int b_offset;
	int rfb_xres;
	int rfb_maxy;
} varblock;

/*****************************************************************************/

static void keyevent(rfbBool down, rfbKeySym key, rfbClientPtr cl);
static void ptrevent(int buttonMask, int x, int y, rfbClientPtr cl);

/*****************************************************************************/

static void init_fb(void)
{
	size_t pixels;
	size_t bytespp;

	if ((fbfd = open(FB_DEVICE, O_RDONLY)) == -1)
	{
		perror("open");
		exit(EXIT_FAILURE);
	}

	if (ioctl(fbfd, FBIOGET_VSCREENINFO, &scrinfo) != 0)
	{
		perror("ioctl");
		exit(EXIT_FAILURE);
	}

	pixels = scrinfo.xres * scrinfo.yres;
	bytespp = scrinfo.bits_per_pixel / 8;

	fprintf(stderr, "xres=%d, yres=%d, xresv=%d, yresv=%d, xoffs=%d, yoffs=%d, bpp=%d\n", 
	  (int)scrinfo.xres, (int)scrinfo.yres,
	  (int)scrinfo.xres_virtual, (int)scrinfo.yres_virtual,
	  (int)scrinfo.xoffset, (int)scrinfo.yoffset,
	  (int)scrinfo.bits_per_pixel);

	fbmmap = mmap(NULL, pixels * bytespp, PROT_READ, MAP_SHARED, fbfd, 0);

	if (fbmmap == MAP_FAILED)
	{
		perror("mmap");
		exit(EXIT_FAILURE);
	}
}

static void cleanup_fb(void)
{
	/* TODO */
}

/*****************************************************************************/

static void init_kbde(void)
{
	if ((kbdfd = open(KBD_DEVICE, O_WRONLY)) < 0)
	{
		if (errno == ENOENT)
		{
			mode_t mode;
			dev_t dev;

			mode = S_IFCHR | 0666;
			dev = makedev(11, 0);

			if (mknod(KBD_DEVICE, mode, dev) != 0)
				perror("mknod");
			else
			{
				if ((kbdfd = open(KBD_DEVICE, O_WRONLY)) < 0)
					perror("open");
			}
		}
		else
		{
			perror("open");
		}
	}
}

static void cleanup_kbde(void)
{
	if (kbdfd != -1)
		close(kbdfd);
}

/*****************************************************************************/

static void init_fb_server(int argc, char **argv)
{
	printf("Initializing server...\n");

	/* Allocate the VNC server buffer to be managed (not manipulated) by 
	 * libvncserver. */
	vncbuf = calloc(scrinfo.xres * scrinfo.yres, scrinfo.bits_per_pixel / 2);
	assert(vncbuf != NULL);

	/* Allocate the comparison buffer for detecting drawing updates from frame
	 * to frame. */
	fbbuf = calloc(scrinfo.xres * scrinfo.yres, scrinfo.bits_per_pixel / 2);
	assert(fbbuf != NULL);

	/* TODO: This assumes scrinfo.bits_per_pixel is 16. */
	vncscr = rfbGetScreen(&argc, argv, scrinfo.xres, scrinfo.yres, 5, 2, 2);
	assert(vncscr != NULL);

	vncscr->desktopName = "Android";
	vncscr->frameBuffer = (char *)vncbuf;
	vncscr->alwaysShared = TRUE;
	vncscr->httpDir = NULL;
	vncscr->port = VNC_PORT;

	vncscr->kbdAddEvent = keyevent;
	vncscr->ptrAddEvent = ptrevent;

	rfbInitServer(vncscr);

	/* Mark as dirty since we haven't sent any updates at all yet. */
	rfbMarkRectAsModified(vncscr, 0, 0, scrinfo.xres, scrinfo.yres);

	/* No idea. */
	varblock.r_offset = scrinfo.red.offset + scrinfo.red.length - 5;
	varblock.g_offset = scrinfo.green.offset + scrinfo.green.length - 5;
	varblock.b_offset = scrinfo.blue.offset + scrinfo.blue.length - 5;
	varblock.rfb_xres = scrinfo.yres;
	varblock.rfb_maxy = scrinfo.xres - 1;
}

/*****************************************************************************/

#define KEYSYM_CASE_REAL(a,bdown,bup) \
case a: \
	if (down) { ret = bdown; *len = sizeof(bdown) - 1; } \
	else { ret = bup; *len = sizeof(bup) - 1; } \
	break

#define KEYSYM_CASE(a,b) KEYSYM_CASE_REAL(a, b##_MAKE, b##_BREAK)
#define KEYSYM_CASE_SHIFT(a,b) KEYSYM_CASE_REAL(a, KBDE_KEY_LShift_MAKE b##_MAKE, KBDE_KEY_LShift_BREAK b##_BREAK)
#define KEYSYM_CASE_UD(a,b) KEYSYM_CASE_REAL(a, b##_MAKE b##_BREAK, b##_MAKE b##_BREAK)

static unsigned char *keysym2scancodes(rfbBool down, rfbKeySym key, size_t *len)
{
	unsigned char *ret;

	assert(len != NULL);

	fprintf(stderr, "Got keysym: %04x (down=%d)\n", (unsigned int)key, (int)down);

	switch(key)
	{
        KEYSYM_CASE      (XK_BackSpace,          KBDE_KEY_BackSpace);
        KEYSYM_CASE      (XK_Tab,                KBDE_KEY_Tab);
        KEYSYM_CASE      (XK_KP_Tab,             KBDE_KEY_Tab);
        KEYSYM_CASE      (XK_Return,             KBDE_KEY_Enter);
        KEYSYM_CASE      (XK_KP_Enter,           KBDE_KEY_Enter);
        KEYSYM_CASE      (XK_Escape,             KBDE_KEY_Escape);
        KEYSYM_CASE      (XK_space,              KBDE_KEY_Space);
        KEYSYM_CASE      (XK_KP_Space,           KBDE_KEY_Space);
        KEYSYM_CASE_SHIFT(XK_exclam,             KBDE_KEY_1);
        KEYSYM_CASE_SHIFT(XK_quotedbl,           KBDE_KEY_Quote);
        KEYSYM_CASE_SHIFT(XK_numbersign,         KBDE_KEY_3);
        KEYSYM_CASE_SHIFT(XK_dollar,             KBDE_KEY_4);
        KEYSYM_CASE_SHIFT(XK_percent,            KBDE_KEY_5);
        KEYSYM_CASE_SHIFT(XK_ampersand,          KBDE_KEY_7);
        KEYSYM_CASE      (XK_quoteright,         KBDE_KEY_Quote);
        KEYSYM_CASE_SHIFT(XK_parenleft,          KBDE_KEY_9);
        KEYSYM_CASE_SHIFT(XK_parenright,         KBDE_KEY_0);
        KEYSYM_CASE_SHIFT(XK_asterisk,           KBDE_KEY_8);
        KEYSYM_CASE      (XK_KP_Multiply,        KBDE_KEY_KP_Multiply);
        KEYSYM_CASE_SHIFT(XK_plus,               KBDE_KEY_Equal);
        KEYSYM_CASE      (XK_KP_Add,             KBDE_KEY_KP_Plus);
        KEYSYM_CASE      (XK_comma,              KBDE_KEY_Comma);
        KEYSYM_CASE      (XK_minus,              KBDE_KEY_Minus);
        KEYSYM_CASE      (XK_KP_Subtract,        KBDE_KEY_KP_Minus);
        KEYSYM_CASE      (XK_period,             KBDE_KEY_Period);
        KEYSYM_CASE      (XK_KP_Decimal,         KBDE_KEY_KP_Decimal);
        KEYSYM_CASE      (XK_slash,              KBDE_KEY_Slash);
        KEYSYM_CASE      (XK_KP_Divide,          KBDE_KEY_KP_Slash);
        KEYSYM_CASE      (XK_0,                  KBDE_KEY_0);
        KEYSYM_CASE      (XK_KP_0,               KBDE_KEY_KP_0);
        KEYSYM_CASE      (XK_1,                  KBDE_KEY_1);
        KEYSYM_CASE      (XK_KP_1,               KBDE_KEY_KP_1);
        KEYSYM_CASE      (XK_2,                  KBDE_KEY_2);
        KEYSYM_CASE      (XK_KP_2,               KBDE_KEY_KP_2);
        KEYSYM_CASE      (XK_3,                  KBDE_KEY_3);
        KEYSYM_CASE      (XK_KP_3,               KBDE_KEY_KP_3);
        KEYSYM_CASE      (XK_4,                  KBDE_KEY_4);
        KEYSYM_CASE      (XK_KP_4,               KBDE_KEY_KP_4);
        KEYSYM_CASE      (XK_5,                  KBDE_KEY_5);
        KEYSYM_CASE      (XK_KP_5,               KBDE_KEY_KP_5);
        KEYSYM_CASE      (XK_6,                  KBDE_KEY_6);
        KEYSYM_CASE      (XK_KP_6,               KBDE_KEY_KP_6);
        KEYSYM_CASE      (XK_7,                  KBDE_KEY_7);
        KEYSYM_CASE      (XK_KP_7,               KBDE_KEY_KP_7);
        KEYSYM_CASE      (XK_8,                  KBDE_KEY_8);
        KEYSYM_CASE      (XK_KP_8,               KBDE_KEY_KP_8);
        KEYSYM_CASE      (XK_9,                  KBDE_KEY_9);
        KEYSYM_CASE      (XK_KP_9,               KBDE_KEY_KP_9);
        KEYSYM_CASE_SHIFT(XK_colon,              KBDE_KEY_SemiColon);
        KEYSYM_CASE      (XK_semicolon,          KBDE_KEY_SemiColon);
		KEYSYM_CASE_SHIFT(XK_less,               KBDE_KEY_Comma);
        KEYSYM_CASE      (XK_equal,              KBDE_KEY_Equal);
        KEYSYM_CASE_SHIFT(XK_greater,            KBDE_KEY_Period);
        KEYSYM_CASE_SHIFT(XK_question,           KBDE_KEY_Slash);
        KEYSYM_CASE_SHIFT(XK_at,                 KBDE_KEY_2);
        KEYSYM_CASE_SHIFT(XK_A,                  KBDE_KEY_A);
        KEYSYM_CASE_SHIFT(XK_B,                  KBDE_KEY_B);
        KEYSYM_CASE_SHIFT(XK_C,                  KBDE_KEY_C);
        KEYSYM_CASE_SHIFT(XK_D,                  KBDE_KEY_D);
        KEYSYM_CASE_SHIFT(XK_E,                  KBDE_KEY_E);
        KEYSYM_CASE_SHIFT(XK_F,                  KBDE_KEY_F);
        KEYSYM_CASE_SHIFT(XK_G,                  KBDE_KEY_G);
        KEYSYM_CASE_SHIFT(XK_H,                  KBDE_KEY_H);
        KEYSYM_CASE_SHIFT(XK_I,                  KBDE_KEY_I);
        KEYSYM_CASE_SHIFT(XK_J,                  KBDE_KEY_J);
        KEYSYM_CASE_SHIFT(XK_K,                  KBDE_KEY_K);
        KEYSYM_CASE_SHIFT(XK_L,                  KBDE_KEY_L);
        KEYSYM_CASE_SHIFT(XK_M,                  KBDE_KEY_M);
        KEYSYM_CASE_SHIFT(XK_N,                  KBDE_KEY_N);
        KEYSYM_CASE_SHIFT(XK_O,                  KBDE_KEY_O);
        KEYSYM_CASE_SHIFT(XK_P,                  KBDE_KEY_P);
        KEYSYM_CASE_SHIFT(XK_Q,                  KBDE_KEY_Q);
        KEYSYM_CASE_SHIFT(XK_R,                  KBDE_KEY_R);
        KEYSYM_CASE_SHIFT(XK_S,                  KBDE_KEY_S);
        KEYSYM_CASE_SHIFT(XK_T,                  KBDE_KEY_T);
        KEYSYM_CASE_SHIFT(XK_U,                  KBDE_KEY_U);
        KEYSYM_CASE_SHIFT(XK_V,                  KBDE_KEY_V);
        KEYSYM_CASE_SHIFT(XK_W,                  KBDE_KEY_W);
        KEYSYM_CASE_SHIFT(XK_X,                  KBDE_KEY_X);
        KEYSYM_CASE_SHIFT(XK_Y,                  KBDE_KEY_Y);
        KEYSYM_CASE_SHIFT(XK_Z,                  KBDE_KEY_Z);
        KEYSYM_CASE      (XK_bracketleft,        KBDE_KEY_LBrace);
        KEYSYM_CASE      (XK_backslash,          KBDE_KEY_BackSlash);
        KEYSYM_CASE      (XK_asciicircum,        KBDE_KEY_Wake);
        KEYSYM_CASE_SHIFT(XK_underscore,         KBDE_KEY_Minus);
        KEYSYM_CASE      (XK_quoteleft,          KBDE_KEY_Tilde);
        KEYSYM_CASE      (XK_a,                  KBDE_KEY_A);
        KEYSYM_CASE      (XK_b,                  KBDE_KEY_B);
        KEYSYM_CASE      (XK_c,                  KBDE_KEY_C);
        KEYSYM_CASE      (XK_d,                  KBDE_KEY_D);
        KEYSYM_CASE      (XK_e,                  KBDE_KEY_E);
        KEYSYM_CASE      (XK_f,                  KBDE_KEY_F);
        KEYSYM_CASE      (XK_g,                  KBDE_KEY_G);
        KEYSYM_CASE      (XK_h,                  KBDE_KEY_H);
        KEYSYM_CASE      (XK_i,                  KBDE_KEY_I);
        KEYSYM_CASE      (XK_j,                  KBDE_KEY_J);
        KEYSYM_CASE      (XK_k,                  KBDE_KEY_K);
        KEYSYM_CASE      (XK_l,                  KBDE_KEY_L);
        KEYSYM_CASE      (XK_m,                  KBDE_KEY_M);
        KEYSYM_CASE      (XK_n,                  KBDE_KEY_N);
        KEYSYM_CASE      (XK_o,                  KBDE_KEY_O);
        KEYSYM_CASE      (XK_p,                  KBDE_KEY_P);
        KEYSYM_CASE      (XK_q,                  KBDE_KEY_Q);
        KEYSYM_CASE      (XK_r,                  KBDE_KEY_R);
        KEYSYM_CASE      (XK_s,                  KBDE_KEY_S);
        KEYSYM_CASE      (XK_t,                  KBDE_KEY_T);
        KEYSYM_CASE      (XK_u,                  KBDE_KEY_U);
        KEYSYM_CASE      (XK_v,                  KBDE_KEY_V);
        KEYSYM_CASE      (XK_w,                  KBDE_KEY_W);
        KEYSYM_CASE      (XK_x,                  KBDE_KEY_X);
        KEYSYM_CASE      (XK_y,                  KBDE_KEY_Y);
        KEYSYM_CASE      (XK_z,                  KBDE_KEY_Z);
        KEYSYM_CASE_SHIFT(XK_braceleft,          KBDE_KEY_LBrace);
        KEYSYM_CASE_SHIFT(XK_bar,                KBDE_KEY_BackSlash);
        KEYSYM_CASE_SHIFT(XK_asciitilde,         KBDE_KEY_Tilde);
        KEYSYM_CASE      (XK_Delete,             KBDE_KEY_Delete);

        KEYSYM_CASE      (XK_Left,               KBDE_KEY_ArrowL);
        KEYSYM_CASE      (XK_KP_Left,            KBDE_KEY_ArrowL);
        KEYSYM_CASE      (XK_Up,                 KBDE_KEY_ArrowU);
        KEYSYM_CASE      (XK_KP_Up,              KBDE_KEY_ArrowU);
        KEYSYM_CASE      (XK_Down,               KBDE_KEY_ArrowD);
        KEYSYM_CASE      (XK_KP_Down,            KBDE_KEY_ArrowD);
        KEYSYM_CASE      (XK_Right,              KBDE_KEY_ArrowR);
        KEYSYM_CASE      (XK_KP_Right,           KBDE_KEY_ArrowR);
        KEYSYM_CASE      (XK_End,                KBDE_KEY_End);
        KEYSYM_CASE      (XK_KP_End,             KBDE_KEY_End);
        KEYSYM_CASE      (XK_Home,               KBDE_KEY_Home);
        KEYSYM_CASE      (XK_KP_Home,            KBDE_KEY_Home);
        KEYSYM_CASE      (XK_Page_Down,          KBDE_KEY_PageDown);
        KEYSYM_CASE      (XK_KP_Page_Down,       KBDE_KEY_PageDown);
        KEYSYM_CASE      (XK_Page_Up,            KBDE_KEY_F1);
        KEYSYM_CASE      (XK_KP_Page_Up,         KBDE_KEY_F1);

        KEYSYM_CASE_UD   (XK_F1,                 KBDE_KEY_F1);
        KEYSYM_CASE_UD   (XK_F2,                 KBDE_KEY_WWWBack);
        KEYSYM_CASE_UD   (XK_F3,                 KBDE_KEY_F3);
        KEYSYM_CASE_UD   (XK_F4,                 KBDE_KEY_F4);
        KEYSYM_CASE_UD   (XK_F5,                 KBDE_KEY_F5);

        KEYSYM_CASE_SHIFT(XK_braceright,         KBDE_KEY_RBrace);
        KEYSYM_CASE      (XK_bracketright,       KBDE_KEY_RBrace);

		default:
			ret = NULL;
			break;
	}

	return ret;
}

static void keyevent(rfbBool down, rfbKeySym key, rfbClientPtr cl)
{
	unsigned char *scancodes;
	size_t len;

	if (kbdfd == -1)
		return;

	if ((scancodes = keysym2scancodes(down, key, &len)) != NULL)
	{
		if (write(kbdfd, scancodes, len) < len)
			perror("write");
	}
}

static void ptrevent(int buttonMask, int x, int y, rfbClientPtr cl)
{
	/* XXX */
}

#define PIXEL_FB_TO_RFB(p,r,g,b) ((p>>r)&0x1f001f)|(((p>>g)&0x1f001f)<<5)|(((p>>b)&0x1f001f)<<10)

static void update_screen(void)
{
	unsigned int *f, *c, *r;
	int x, y;

	varblock.min_i = varblock.min_j = 9999;
	varblock.max_i = varblock.max_j = -1;

	f = (unsigned int *)fbmmap;        /* -> framebuffer         */
	c = (unsigned int *)fbbuf;         /* -> compare framebuffer */
	r = (unsigned int *)vncbuf;        /* -> remote framebuffer  */

	for (y = 0; y < scrinfo.yres; y++)
	{
		/* Compare every 2 pixels at a time, assuming that changes are likely
		 * in pairs. */
		for (x = 0; x < scrinfo.xres; x += 2)
		{
			unsigned int pixel = *f;

			if (pixel != *c)
			{
				*c = pixel;

				/* XXX: Undo the checkered pattern to test the efficiency
				 * gain using hextile encoding. */
				if (pixel == 0x18e320e4 || pixel == 0x20e418e3)
					pixel = 0x18e318e3;

				*r = PIXEL_FB_TO_RFB(pixel,
				  varblock.r_offset, varblock.g_offset, varblock.b_offset);

				if (x < varblock.min_i)
					varblock.min_i = x;
				else
				{
					if (x > varblock.max_i)
						varblock.max_i = x;

					if (y > varblock.max_j)
						varblock.max_j = y;
					else if (y < varblock.min_j)
						varblock.min_j = y;
				}
			}

			f++, c++;
			r++;
		}
	}

	if (varblock.min_i < 9999)
	{
		if (varblock.max_i < 0)
			varblock.max_i = varblock.min_i;

		if (varblock.max_j < 0)
			varblock.max_j = varblock.min_j;

		fprintf(stderr, "Dirty page: %dx%d+%d+%d...\n",
		  (varblock.max_i+2) - varblock.min_i, (varblock.max_j+1) - varblock.min_j,
		  varblock.min_i, varblock.min_j);

		rfbMarkRectAsModified(vncscr, varblock.min_i, varblock.min_j,
		  varblock.max_i + 2, varblock.max_j + 1);

		rfbProcessEvents(vncscr, 10000);
	}
}

/*****************************************************************************/

int main(int argc, char **argv)
{
	printf("Initializing framebuffer device " FB_DEVICE "...\n");
	init_fb();

	printf("Initializing keyboard emulation driver " KBD_DEVICE "...\n");
	init_kbde();

	printf("Initializing VNC server:\n");
	printf("	width:  %d\n", (int)scrinfo.xres);
	printf("	height: %d\n", (int)scrinfo.yres);
	printf("	bpp:    %d\n", (int)scrinfo.bits_per_pixel);
	printf("	port:   %d\n", (int)VNC_PORT);
	init_fb_server(argc, argv);

	/* Implement our own event loop to detect changes in the framebuffer. */
	while (1)
	{
		while (vncscr->clientHead == NULL)
			rfbProcessEvents(vncscr, 100000);

		rfbProcessEvents(vncscr, 100000);
		update_screen();
	}

	printf("Cleaning up...\n");
	cleanup_fb();
	cleanup_kbde();
}
