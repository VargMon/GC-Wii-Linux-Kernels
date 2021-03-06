/*
 * drivers/video/gcn-vifb.c
 *
 * Nintendo GameCube/Wii Video Interface (VI) frame buffer driver
 * Copyright (C) 2004-2009 The GameCube Linux Team
 * Copyright (C) 2004 Michael Steil <mist@c64.org>
 * Copyright (C) 2004,2005 Todd Jeffreys <todd@voidpointer.org>
 * Copyright (C) 2006,2007,2008,2009 Albert Herranz
 *
 * Based on vesafb (c) 1998 Gerd Knorr <kraxel@goldbach.in-berlin.de>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 */

#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/string.h>
#include <linux/tty.h>
#include <linux/wait.h>
#include <linux/io.h>

#define DRV_MODULE_NAME   "gcn-vifb"
#define DRV_DESCRIPTION   "Nintendo GameCube/Wii Video Interface (VI) driver"
#define DRV_AUTHOR        "Michael Steil <mist@c64.org>, " \
			  "Todd Jeffreys <todd@voidpointer.org>, " \
			  "Albert Herranz"

static char vifb_driver_version[] = "1.0i";

#define drv_printk(level, format, arg...) \
	 printk(level DRV_MODULE_NAME ": " format , ## arg)


/*
 * Hardware registers.
 */
#define VI_DCR			0x02
#define VI_HTR0			0x04
#define VI_TFBL			0x1c
#define VI_TFBR			0x20
#define VI_BFBL			0x24
#define VI_BFBR			0x28
#define VI_DPV			0x2c

#define VI_DI0			0x30
#define VI_DI1			0x34
#define VI_DI2			0x38
#define VI_DI3			0x3C
#define VI_DI_INT		(1 << 31)
#define VI_DI_ENB		(1 << 28)
#define VI_DI_VCT_SHIFT	16
#define VI_DI_VCT_MASK		0x03FF0000
#define VI_DI_HCT_SHIFT	0
#define VI_DI_HCT_MASK		0x000003FF

#define VI_VISEL		0x6e
#define VI_VISEL_PROGRESSIVE	(1 << 0)


/*
 * Video control data structure.
 */
struct vi_ctl {
	spinlock_t lock;

	void __iomem *io_base;
	unsigned int irq;

	int in_vtrace;
	wait_queue_head_t vtrace_waitq;

	int visible_page;
	unsigned long page_address[2];
	unsigned long flip_pending;

	struct fb_info *info;
};


/*
 * Video mode handling
 */

struct vi_video_mode {
	char *name;
	const u32 *regs;
	int width;
	int height;
	int lines;
};

static const u32 vi_Mode640X480NtscYUV16[32] = {
	0x0F060001, 0x476901AD, 0x02EA5140, 0x00030018,
	0x00020019, 0x410C410C, 0x40ED40ED, 0x00435A4E,
	0x00000000, 0x00435A4E, 0x00000000, 0x00000000,
	0x110701AE, 0x10010001, 0x00010001, 0x00010001,
	0x00000000, 0x00000000, 0x28500100, 0x1AE771F0,
	0x0DB4A574, 0x00C1188E, 0xC4C0CBE2, 0xFCECDECF,
	0x13130F08, 0x00080C0F, 0x00FF0000, 0x00000000,
	0x02800000, 0x000000FF, 0x00FF00FF, 0x00FF00FF
};

static const u32 vi_Mode640x480NtscProgressiveYUV16[32] = {
	0x1e0c0005, 0x476901ad, 0x02ea5140, 0x00060030,
	0x00060030, 0x81d881d8, 0x81d881d8, 0x10000000,
	0x00000000, 0x00000000, 0x00000000, 0x037702b6,
	0x90010001, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x28280100, 0x1ae771f0,
	0x0db4a574, 0x00c1188e, 0xc4c0cbe2, 0xfcecdecf,
	0x13130f08, 0x00080c0f, 0x00ff0000, 0x00010001,
	0x02800000, 0x000000ff, 0x00ff00ff, 0x00ff00ff,
};

static const u32 vi_Mode640X576Pal50YUV16[32] = {
	0x11F50101, 0x4B6A01B0, 0x02F85640, 0x00010023,
	0x00000024, 0x4D2B4D6D, 0x4D8A4D4C, 0x0066D480,
	0x00000000, 0x0066D980, 0x00000000, 0x00C901F3,
	0x913901B1, 0x90010001, 0x00010001, 0x00010001,
	0x00000000, 0x00000000, 0x28500100, 0x1AE771F0,
	0x0DB4A574, 0x00C1188E, 0xC4C0CBE2, 0xFCECDECF,
	0x13130F08, 0x00080C0F, 0x00FF0000, 0x00000000,
	0x02800000, 0x000000FF, 0x00FF00FF, 0x00FF00FF
};

static const u32 vi_Mode640X480Pal60YUV16[32] = {
	0x0F060001, 0x476901AD, 0x02EA5140, 0x00030018,
	0x00020019, 0x410C410C, 0x40ED40ED, 0x0066D480,
	0x00000000, 0x0066D980, 0x00000000, 0x00C9010F,
	0x910701AE, 0x90010001, 0x00010001, 0x00010001,
	0x00000000, 0x00000000, 0x28500100, 0x1AE771F0,
	0x0DB4A574, 0x00C1188E, 0xC4C0CBE2, 0xFCECDECF,
	0x13130F08, 0x00080C0F, 0x00FF0000, 0x00000000,
	0x02800000, 0x000000FF, 0x00FF00FF, 0x00FF00FF
};

static struct vi_video_mode vi_video_modes[] = {
#define VI_VM_NTSC                0
	[VI_VM_NTSC] = {
		.name = "NTSC/PAL60 480i",
		.regs = vi_Mode640X480NtscYUV16,
		.width = 640,
		.height = 480,
		.lines = 525,
	},
#define VI_VM_NTSC_PROGRESSIVE    (VI_VM_NTSC+1)
	[VI_VM_NTSC_PROGRESSIVE] = {
		.name = "NTSC 480p",
		.regs = vi_Mode640x480NtscProgressiveYUV16,
		.width = 640,
		.height = 480,
		.lines = 525,
	},
#define VI_VM_PAL50               (VI_VM_NTSC_PROGRESSIVE+1)
	[VI_VM_PAL50] = {
		.name = "PAL50 576i",
		.regs = vi_Mode640X576Pal50YUV16,
		.width = 640,
		.height = 576,
		.lines = 625,
	},
#define VI_VM_PAL60               (VI_VM_PAL50+1)
	[VI_VM_PAL60] = {
		/* this seems to be actually the same as NTSC 480i */
		.name = "PAL60 480i",
		.regs = vi_Mode640X480Pal60YUV16,
		.width = 640,
		.height = 480,
		.lines = 525,
	},
};


static struct fb_fix_screeninfo vifb_fix = {
	.id = DRV_MODULE_NAME,
	.type = FB_TYPE_PACKED_PIXELS,
	.visual = FB_VISUAL_TRUECOLOR,	/* lies, lies, lies, ... */
	.accel = FB_ACCEL_NONE,
};

static struct fb_var_screeninfo vifb_var = {
	.bits_per_pixel = 16,
	.activate = FB_ACTIVATE_NOW,
	.height = -1,
	.width = -1,
	.right_margin = 32,
	.upper_margin = 16,
	.lower_margin = 4,
	.vsync_len = 4,
	.vmode = FB_VMODE_INTERLACED,
};

/*
 * setup parameters
 */
static struct vi_video_mode *vi_current_video_mode;
static int ypan = 1;		/* 0..nothing, 1..ypan */

/* FIXME: is this really needed? */
static u32 pseudo_palette[17];


/* some glue to the gx side */
static inline void gcngx_dispatch_vtrace(struct vi_ctl *ctl)
{
#ifdef CONFIG_FB_GAMECUBE_GX
	gcngx_vtrace(ctl);
#endif
}


/*
 *
 * Color space handling.
 */

/*
 * RGB to YCbYCr conversion support bits.
 * We are using here the ITU.BT-601 Y'CbCr standard.
 *
 * References:
 * - "Colour Space Conversions" by Adrian Ford and Alan Roberts, 1998
 *   (google for coloureq.pdf)
 *
 */

#define RGB2YUV_SHIFT   16
#define RGB2YUV_LUMA    16
#define RGB2YUV_CHROMA 128

#define Yr ((int)(0.299 * (1<<RGB2YUV_SHIFT)))
#define Yg ((int)(0.587 * (1<<RGB2YUV_SHIFT)))
#define Yb ((int)(0.114 * (1<<RGB2YUV_SHIFT)))

#define Ur ((int)(-0.169 * (1<<RGB2YUV_SHIFT)))
#define Ug ((int)(-0.331 * (1<<RGB2YUV_SHIFT)))
#define Ub ((int)(0.500 * (1<<RGB2YUV_SHIFT)))

#define Vr ((int)(0.500 * (1<<RGB2YUV_SHIFT)))	/* same as Ub */
#define Vg ((int)(-0.419 * (1<<RGB2YUV_SHIFT)))
#define Vb ((int)(-0.081 * (1<<RGB2YUV_SHIFT)))

/*
 * Converts two 16bpp rgb pixels into a dual yuy2 pixel.
 */
static inline uint32_t rgbrgb16toycbycr(uint16_t rgb1, uint16_t rgb2)
{
	register int Y1, Cb, Y2, Cr;
	register int r1, g1, b1;
	register int r2, g2, b2;
	register int r, g, b;

	/* fast path, thanks to bohdy */
	if (!(rgb1 | rgb2))
		return 0x00800080;	/* black, black */

	/* RGB565 */
	r1 = ((rgb1 >> 11) & 0x1f);
	g1 = ((rgb1 >> 5) & 0x3f);
	b1 = ((rgb1 >> 0) & 0x1f);

	/* fast (approximated) scaling to 8 bits, thanks to Masken */
	r1 = (r1 << 3) | (r1 >> 2);
	g1 = (g1 << 2) | (g1 >> 4);
	b1 = (b1 << 3) | (b1 >> 2);

	Y1 = clamp(((Yr * r1 + Yg * g1 + Yb * b1) >> RGB2YUV_SHIFT)
		   + RGB2YUV_LUMA, 16, 235);
	if (rgb1 == rgb2) {
		/* this is just another fast path */
		Y2 = Y1;
		r = r1;
		g = g1;
		b = b1;
	} else {
		/* same as we did for r1 before */
		r2 = ((rgb2 >> 11) & 0x1f);
		g2 = ((rgb2 >> 5) & 0x3f);
		b2 = ((rgb2 >> 0) & 0x1f);
		r2 = (r2 << 3) | (r2 >> 2);
		g2 = (g2 << 2) | (g2 >> 4);
		b2 = (b2 << 3) | (b2 >> 2);

		Y2 = clamp(((Yr * r2 + Yg * g2 + Yb * b2) >> RGB2YUV_SHIFT)
			   + RGB2YUV_LUMA,
			   16, 235);

		r = (r1 + r2) / 2;
		g = (g1 + g2) / 2;
		b = (b1 + b2) / 2;
	}

	Cb = clamp(((Ur * r + Ug * g + Ub * b) >> RGB2YUV_SHIFT)
		   + RGB2YUV_CHROMA, 16, 240);
	Cr = clamp(((Vr * r + Vg * g + Vb * b) >> RGB2YUV_SHIFT)
		   + RGB2YUV_CHROMA, 16, 240);

	return (((uint8_t) Y1) << 24) | (((uint8_t) Cb) << 16) |
	    (((uint8_t) Y2) << 8) | (((uint8_t) Cr) << 0);
}

/*
 *
 * Video hardware support.
 */

/*
 * Get video mode reported by hardware.
 * 0=NTSC, 1=PAL, 2=MPAL, 3=debug
 */
static inline int vi_get_mode(struct vi_ctl *ctl)
{
	return (in_be16(ctl->io_base + VI_DCR) >> 8) & 3;
}

static inline int vi_is_mode_ntsc(struct vi_ctl *ctl)
{
	return vi_get_mode(ctl) == 0;
}

static inline int vi_is_mode_progressive(__u32 vmode)
{
	return (vmode & FB_VMODE_MASK) == FB_VMODE_NONINTERLACED;
}

static inline int vi_can_do_progressive(struct vi_ctl *ctl)
{
	return in_be16(ctl->io_base + VI_VISEL) & VI_VISEL_PROGRESSIVE;
}

static void vi_guess_mode(struct vi_ctl *ctl)
{
	void __iomem *io_base = ctl->io_base;
	u16 mode;

	if (vi_current_video_mode == NULL) {
		/* auto detection */
		if (in_be32(io_base + VI_HTR0) == 0x4B6A01B0) {
			/* PAL50 */
			vi_current_video_mode = vi_video_modes + VI_VM_PAL50;
		} else {
			/* NTSC/PAL60 */
			mode = vi_get_mode(ctl);
			switch (mode) {
			case 0:	/* NTSC */
				/* check if we can support progressive */
				vi_current_video_mode =
				    vi_video_modes +
				    (vi_can_do_progressive(ctl) ?
				     VI_VM_NTSC_PROGRESSIVE : VI_VM_NTSC);
				break;
				/* XXX this code is never reached */
			case 1:	/* PAL60 */
				vi_current_video_mode =
				    vi_video_modes + VI_VM_PAL60;
				break;
			default:	/* MPAL or DEBUG, we don't support */
				break;
			}
		}
	}

	/* if we get here something wrong happened */
	if (vi_current_video_mode == NULL) {
		drv_printk(KERN_DEBUG, "failed to guess video mode,"
			   "using NTSC\n");
		vi_current_video_mode = vi_video_modes + VI_VM_NTSC;
	}
}

/*
 * Set the address from where the video encoder will display data on screen.
 */
void vi_set_framebuffer(struct vi_ctl *ctl, u32 addr)
{
	struct fb_info *info = ctl->info;
	void __iomem *io_base = ctl->io_base;

	/* set top field */
	out_be32(io_base + VI_TFBL, 0x10000000 | (addr >> 5));

	/* set bottom field */
	if (!vi_is_mode_progressive(info->var.vmode))
		addr += info->fix.line_length;
	out_be32(io_base + VI_BFBL, 0x10000000 | (addr >> 5));
}

/*
 * Swap the visible and back pages.
 */
static inline void vi_flip_page(struct vi_ctl *ctl)
{
	ctl->visible_page ^= 1;
	vi_set_framebuffer(ctl, ctl->page_address[ctl->visible_page]);

	ctl->flip_pending = 0;
}

static void vi_enable_interrupts(struct vi_ctl *ctl, int enable)
{
	void __iomem *io_base = ctl->io_base;
	u16 vtrap, htrap;

	if (enable) {
		/*
		 * The vertical retrace happens while the beam moves from
		 * the last drawn dot in the last line to the first dot in
		 * the first line.
		 */

		/* XXX should we incorporate this in the video mode struct ? */
		vtrap = vi_current_video_mode->lines;
		htrap = vi_is_mode_ntsc(ctl) ? 430 : 433;

		/* non-progressive needs interlacing */
		if (!(vi_is_mode_progressive(ctl->info->var.vmode)
		    && vi_can_do_progressive(ctl))) {
			vtrap /= 2;
		}

		/* first dot, first line */
		out_be32(io_base + VI_DI0,
			VI_DI_INT | VI_DI_ENB |
		       (1 << VI_DI_VCT_SHIFT) | (1 << VI_DI_HCT_SHIFT));
		/* last dot, last line */
		out_be32(io_base + VI_DI1,
			VI_DI_INT | VI_DI_ENB |
		       (vtrap << VI_DI_VCT_SHIFT) | (htrap << VI_DI_HCT_SHIFT));
	} else {
		out_be32(io_base + VI_DI0, 0);
		out_be32(io_base + VI_DI1, 0);
	}
	/* these two are currently not used */
	out_be32(io_base + VI_DI2, 0);
	out_be32(io_base + VI_DI3, 0);
}

static void vi_dispatch_vtrace(struct vi_ctl *ctl)
{
	unsigned long flags;

	spin_lock_irqsave(&ctl->lock, flags);
	if (ctl->flip_pending)
		vi_flip_page(ctl);
	spin_unlock_irqrestore(&ctl->lock, flags);

	wake_up_interruptible(&ctl->vtrace_waitq);
}

static irqreturn_t vi_irq_handler(int irq, void *dev)
{
	struct fb_info *info = dev_get_drvdata((struct device *)dev);
	struct vi_ctl *ctl = info->par;
	void __iomem *io_base = ctl->io_base;
	u32 val;

	/* DI0 and DI1 are used to account for the vertical retrace */
	val = in_be32(io_base + VI_DI0);
	if (val & VI_DI_INT) {
		ctl->in_vtrace = 0;
		gcngx_dispatch_vtrace(ctl); /* backwards compatibility */

		out_be32(io_base + VI_DI0, val & ~VI_DI_INT);
		return IRQ_HANDLED;
	}
	val = in_be32(io_base + VI_DI1);
	if (val & VI_DI_INT) {
		ctl->in_vtrace = 1;
		vi_dispatch_vtrace(ctl);
		gcngx_dispatch_vtrace(ctl); /* backwards compatibility */

		out_be32(io_base + VI_DI1, val & ~VI_DI_INT);
		return IRQ_HANDLED;
	}

	/* currently unused, just in case */
	val = in_be32(io_base + VI_DI2);
	if (val & VI_DI_INT) {
		out_be32(io_base + VI_DI2, val & ~VI_DI_INT);
		return IRQ_HANDLED;
	}
	val = in_be32(io_base + VI_DI3);
	if (val & VI_DI_INT) {
		out_be32(io_base + VI_DI3, val & ~VI_DI_INT);
		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

/*
 * Linux framebuffer support routines.
 *
 */

/*
 * This is just a quick, dirty and cheap way of getting right colors on the
 * linux framebuffer console.
 */
unsigned int vifb_writel(unsigned int rgbrgb, void *address)
{
	uint16_t *rgb = (uint16_t *)&rgbrgb;
	return fb_writel_real(rgbrgb16toycbycr(rgb[0], rgb[1]), address);
}

/*
 * Restore the video hardware to sane defaults.
 */
int vifb_restorefb(struct fb_info *info)
{
	struct vi_ctl *ctl = info->par;
	void __iomem *io_base = ctl->io_base;
	int i;
	unsigned long flags;

	/* set page 0 as the visible page and cancel pending flips */
	spin_lock_irqsave(&ctl->lock, flags);
	ctl->visible_page = 1;
	vi_flip_page(ctl);
	spin_unlock_irqrestore(&ctl->lock, flags);

	/* initialize video registers */
	for (i = 0; i < 7; i++) {
		out_be32(io_base + i * sizeof(__u32),
			vi_current_video_mode->regs[i]);
	}
	out_be32(io_base + VI_TFBR,
		vi_current_video_mode->regs[VI_TFBR / sizeof(__u32)]);
	out_be32(io_base + VI_BFBR,
		vi_current_video_mode->regs[VI_BFBR / sizeof(__u32)]);
	out_be32(io_base + VI_DPV,
		vi_current_video_mode->regs[VI_DPV / sizeof(__u32)]);
	for (i = 16; i < 32; i++) {
		out_be32(io_base + i * sizeof(__u32),
			vi_current_video_mode->regs[i]);
	}

	/* enable the video retrace handling */
	vi_enable_interrupts(ctl, 1);

	return 0;
}
EXPORT_SYMBOL(vifb_restorefb);

/*
 * FIXME: do we really need this?
 */
static int vifb_setcolreg(unsigned regno, unsigned red, unsigned green,
			   unsigned blue, unsigned transp, struct fb_info *info)
{
	/*
	 *  Set a single color register. The values supplied are
	 *  already rounded down to the hardware's capabilities
	 *  (according to the entries in the `var' structure). Return
	 *  != 0 for invalid regno.
	 */

	if (regno >= info->cmap.len)
		return 1;

	switch (info->var.bits_per_pixel) {
	case 16:
		if (info->var.red.offset == 10) {
			/* 1:5:5:5, not used currently */
			((u32 *) (info->pseudo_palette))[regno] =
			    ((red & 0xf800) >> 1) |
			    ((green & 0xf800) >> 6) | ((blue & 0xf800) >> 11);
		} else {
			/* 0:5:6:5 */
			((u32 *) (info->pseudo_palette))[regno] =
			    ((red & 0xf800)) |
			    ((green & 0xfc00) >> 5) | ((blue & 0xf800) >> 11);
		}
		break;
	case 8:
	case 15:
	case 24:
	case 32:
		break;
	}
	return 0;
}

/*
 * Pan the display by altering the framebuffer address in hardware.
 */
static int vifb_pan_display(struct fb_var_screeninfo *var,
			     struct fb_info *info)
{
	struct vi_ctl *ctl = info->par;
	unsigned long flags;
	int offset;

	offset = (var->yoffset * info->fix.line_length) +
	    var->xoffset * (var->bits_per_pixel / 8);
	vi_set_framebuffer(ctl, info->fix.smem_start + offset);

	spin_lock_irqsave(&ctl->lock, flags);
	ctl->visible_page = (offset) ? 1 : 0;
	spin_unlock_irqrestore(&ctl->lock, flags);

	return 0;
}

static int vifb_ioctl(struct fb_info *info,
		       unsigned int cmd, unsigned long arg)
{
	struct vi_ctl *ctl = info->par;
	void __user *argp;
	unsigned long flags;
	int page;

	switch (cmd) {
	case FBIOWAITRETRACE:
		interruptible_sleep_on(&ctl->vtrace_waitq);
		return signal_pending(current) ? -EINTR : 0;
	case FBIOFLIPHACK:
		/*
		 * If arg == NULL then
		 *   Try to flip the video page as soon as possible.
		 *   Returns the current visible video page number.
		 */
		if (!arg) {
			spin_lock_irqsave(&ctl->lock, flags);
			if (ctl->in_vtrace)
				vi_flip_page(ctl);
			else
				ctl->flip_pending = 1;
			spin_unlock_irqrestore(&ctl->lock, flags);
			return ctl->visible_page;
		}

		/*
		 * If arg != NULL then
		 *   Wait until the video page number pointed by arg
		 *   is not visible.
		 *   Returns the current visible video page number.
		 */
		argp = (void __user *)arg;
		if (copy_from_user(&page, argp, sizeof(int)))
			return -EFAULT;

		if (page != 0 && page != 1)
			return -EINVAL;

		spin_lock_irqsave(&ctl->lock, flags);
		ctl->flip_pending = 0;
		if (ctl->visible_page == page) {
			if (ctl->in_vtrace) {
				vi_flip_page(ctl);
			} else {
				ctl->flip_pending = 1;
				spin_unlock_irqrestore(&ctl->lock, flags);
				interruptible_sleep_on(&ctl->vtrace_waitq);
				return signal_pending(current) ?
					-EINTR : ctl->visible_page;
			}
		}
		spin_unlock_irqrestore(&ctl->lock, flags);
		return ctl->visible_page;
	}
#ifdef CONFIG_FB_GAMECUBE_GX
	/* see if the GX module will handle it */
	return gcngx_ioctl(info, cmd, arg);
#else
	return -EINVAL;
#endif
}

/*
 * Set the video mode according to info->var.
 */
static int vifb_set_par(struct fb_info *info)
{
	/* just load sane default here */
	vifb_restorefb(info);
	return 0;
}

/*
 * Check var and eventually tweak it to something supported.
 * Do not modify par here.
 */
static int vifb_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
	struct vi_ctl *ctl = info->par;

	/* check bpp */
	if (var->bits_per_pixel != 16 ||	/* check bpp */
	    var->xres_virtual != vi_current_video_mode->width ||
	    var->xres != vi_current_video_mode->width ||
	    /* XXX isobel, do not break old sdl */
	    var->yres_virtual > 2 * vi_current_video_mode->height ||
	    var->yres > vi_current_video_mode->height ||
	    (vi_is_mode_progressive(var->vmode) &&
	     !vi_can_do_progressive(ctl))) {	/* trying to set progressive? */
		return -EINVAL;
	}
	return 0;
}

static int vifb_mmap(struct fb_info *info, struct vm_area_struct *vma)
{
	unsigned long off;
	unsigned long start;
	u32 len;

	off = vma->vm_pgoff << PAGE_SHIFT;

	/* frame buffer memory */
	start = info->fix.smem_start;
	len = PAGE_ALIGN((start & ~PAGE_MASK) + info->fix.smem_len);
	start &= PAGE_MASK;
	if ((vma->vm_end - vma->vm_start + off) > len)
		return -EINVAL;
	off += start;
	vma->vm_pgoff = off >> PAGE_SHIFT;

	/* this is an IO map, tell maydump to skip this VMA */
	vma->vm_flags |= VM_IO | VM_RESERVED;

	/* we share RAM between the cpu and the video hardware */
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

	if (io_remap_pfn_range(vma, vma->vm_start, off >> PAGE_SHIFT,
				vma->vm_end - vma->vm_start,
				vma->vm_page_prot))
		return -EAGAIN;
	return 0;
}


struct fb_ops vifb_ops = {
	.owner = THIS_MODULE,
	.fb_setcolreg = vifb_setcolreg,
	.fb_pan_display = vifb_pan_display,
	.fb_ioctl = vifb_ioctl,
	.fb_set_par = vifb_set_par,
	.fb_check_var = vifb_check_var,
	.fb_mmap = vifb_mmap,
	.fb_fillrect = cfb_fillrect,
	.fb_copyarea = cfb_copyarea,
	.fb_imageblit = cfb_imageblit,
};

/*
 * Driver model helper routines.
 *
 */

static int vifb_do_probe(struct device *dev,
			 struct resource *mem, unsigned int irq,
			 unsigned long xfb_start, unsigned long xfb_size)
{
	struct fb_info *info;
	struct vi_ctl *ctl;

	int video_cmap_len;
	int err = -EINVAL;

	info = framebuffer_alloc(sizeof(struct vi_ctl), dev);
	if (!info)
		goto err_framebuffer_alloc;

	info->fbops = &vifb_ops;
	info->var = vifb_var;
	info->fix = vifb_fix;
	ctl = info->par;
	ctl->info = info;

	/* first thing needed */
	ctl->io_base = ioremap(mem->start, mem->end - mem->start + 1);
	ctl->irq = irq;

	vi_guess_mode(ctl);

	info->var.xres = vi_current_video_mode->width;
	info->var.yres = vi_current_video_mode->height;

	/* enable non-interlaced if it supports progressive */
	if (vi_can_do_progressive(ctl))
		info->var.vmode = FB_VMODE_NONINTERLACED;

	/* horizontal line in bytes */
	info->fix.line_length = info->var.xres * (info->var.bits_per_pixel / 8);

	/*
	 * Location and size of the external framebuffer.
	 */
	info->fix.smem_start = xfb_start;
	info->fix.smem_len = xfb_size;

	if (!request_mem_region(info->fix.smem_start, info->fix.smem_len,
				DRV_MODULE_NAME)) {
		drv_printk(KERN_WARNING,
			   "failed to request video memory at %p\n",
			   (void *)info->fix.smem_start);
	}

	info->screen_base = ioremap(info->fix.smem_start, info->fix.smem_len);
	if (!info->screen_base) {
		drv_printk(KERN_ERR,
			   "failed to ioremap video memory at %p (%dk)\n",
			   (void *)info->fix.smem_start,
			   info->fix.smem_len / 1024);
		err = -EIO;
		goto err_ioremap;
	}

	spin_lock_init(&ctl->lock);
	init_waitqueue_head(&ctl->vtrace_waitq);

	ctl->visible_page = 0;
	ctl->page_address[0] = info->fix.smem_start;
	ctl->page_address[1] =
	    info->fix.smem_start + info->var.yres * info->fix.line_length;

	ctl->flip_pending = 0;

	drv_printk(KERN_INFO,
		   "framebuffer at 0x%p, mapped to 0x%p, size %dk\n",
		   (void *)info->fix.smem_start, info->screen_base,
		   info->fix.smem_len / 1024);
	drv_printk(KERN_INFO,
		   "mode is %dx%dx%d, linelength=%d, pages=%d\n",
		   info->var.xres, info->var.yres,
		   info->var.bits_per_pixel,
		   info->fix.line_length,
		   info->fix.smem_len / (info->fix.line_length*info->var.yres));

	info->var.xres_virtual = info->var.xres;
	info->var.yres_virtual = info->fix.smem_len / info->fix.line_length;

	if (ypan && info->var.yres_virtual > info->var.yres) {
		drv_printk(KERN_INFO, "scrolling: pan,  yres_virtual=%d\n",
			   info->var.yres_virtual);
	} else {
		drv_printk(KERN_INFO, "scrolling: redraw, yres_virtual=%d\n",
			   info->var.yres_virtual);
		info->var.yres_virtual = info->var.yres;
		ypan = 0;
	}

	info->fix.ypanstep = ypan ? 1 : 0;
	info->fix.ywrapstep = 0;
	if (!ypan)
		info->fbops->fb_pan_display = NULL;

	/* use some dummy values for timing to make fbset happy */
	info->var.pixclock = 10000000 / info->var.xres * 1000 / info->var.yres;
	info->var.left_margin = (info->var.xres / 8) & 0xf8;
	info->var.hsync_len = (info->var.xres / 8) & 0xf8;

	/* we support ony 16 bits per pixel */
	info->var.red.offset = 11;
	info->var.red.length = 5;
	info->var.green.offset = 5;
	info->var.green.length = 6;
	info->var.blue.offset = 0;
	info->var.blue.length = 5;
	info->var.transp.offset = 0;
	info->var.transp.length = 0;
	video_cmap_len = 16;

	info->pseudo_palette = pseudo_palette;
	if (fb_alloc_cmap(&info->cmap, video_cmap_len, 0)) {
		err = -ENOMEM;
		goto err_alloc_cmap;
	}

	info->flags = FBINFO_FLAG_DEFAULT | (ypan) ? FBINFO_HWACCEL_YPAN : 0;

	dev_set_drvdata(dev, info);

	vi_enable_interrupts(ctl, 0);

	err = request_irq(ctl->irq, vi_irq_handler, 0, DRV_MODULE_NAME, dev);
	if (err) {
		drv_printk(KERN_ERR, "unable to register IRQ %u\n", ctl->irq);
		goto err_request_irq;
	}

	/* now register us */
	if (register_framebuffer(info) < 0) {
		err = -EINVAL;
		goto err_register_framebuffer;
	}

	/* setup the framebuffer address */
	vifb_restorefb(info);

#ifdef CONFIG_FB_GAMECUBE_GX
	err = gcngx_init(info);
	if (err)
		goto err_gcngx_init;
#endif

	drv_printk(KERN_INFO, "fb%d: %s frame buffer device\n",
		   info->node, info->fix.id);

	return 0;

#ifdef CONFIG_FB_GAMECUBE_GX
err_gcngx_init:
	unregister_framebuffer(info);
#endif
err_register_framebuffer:
	free_irq(ctl->irq, 0);
err_request_irq:
	fb_dealloc_cmap(&info->cmap);
err_alloc_cmap:
	iounmap(info->screen_base);
err_ioremap:
	release_mem_region(info->fix.smem_start, info->fix.smem_len);

	dev_set_drvdata(dev, NULL);
	iounmap(ctl->io_base);
	framebuffer_release(info);
err_framebuffer_alloc:
	return err;
}

static int vifb_do_remove(struct device *dev)
{
	struct fb_info *info = dev_get_drvdata(dev);
	struct vi_ctl *ctl = info->par;

	if (!info)
		return -ENODEV;

#ifdef CONFIG_FB_GAMECUBE_GX
	gcngx_exit(info);
#endif
	free_irq(ctl->irq, dev);
	unregister_framebuffer(info);
	fb_dealloc_cmap(&info->cmap);
	iounmap(info->screen_base);
	release_mem_region(info->fix.smem_start, info->fix.smem_len);

	dev_set_drvdata(dev, NULL);
	iounmap(ctl->io_base);
	framebuffer_release(info);
	return 0;
}

#ifndef MODULE

static int __devinit vifb_setup(char *options)
{
	char *this_opt;

	if (!options || !*options)
		return 0;

	drv_printk(KERN_DEBUG, "options: %s\n", options);

	while ((this_opt = strsep(&options, ",")) != NULL) {
		if (!*this_opt)
			continue;

		if (!strcmp(this_opt, "redraw"))
			ypan = 0;
		else if (!strcmp(this_opt, "ypan"))
			ypan = 1;
		else if (!strcmp(this_opt, "ywrap"))
			ypan = 2;
		else if (!strncmp(this_opt, "tv=", 3)) {
			if (!strncmp(this_opt + 3, "PAL", 3))
				vi_current_video_mode =
				    vi_video_modes + VI_VM_PAL50;
			else if (!strncmp(this_opt + 3, "NTSC", 4))
				vi_current_video_mode =
				    vi_video_modes + VI_VM_NTSC;
		}
	}
	return 0;
}

#endif	/* MODULE */


/*
 * OF platform driver hooks.
 *
 */

static int __init vifb_of_probe(struct of_device *odev,
				 const struct of_device_id *match)
{
	struct resource res;
	const unsigned long *prop;
	unsigned long xfb_start, xfb_size;
	int retval;

	retval = of_address_to_resource(odev->node, 0, &res);
	if (retval) {
		drv_printk(KERN_ERR, "no io memory range found\n");
		return -ENODEV;
	}

	prop = of_get_property(odev->node, "xfb-start", NULL);
	if (!prop) {
		drv_printk(KERN_ERR, "no xfb start found\n");
		return -ENODEV;
	}
	xfb_start = *prop;

	prop = of_get_property(odev->node, "xfb-size", NULL);
	if (!prop) {
		drv_printk(KERN_ERR, "no xfb size found\n");
		return -ENODEV;
	}
	xfb_size = *prop;

	return vifb_do_probe(&odev->dev,
			     &res, irq_of_parse_and_map(odev->node, 0),
			     xfb_start, xfb_size);
}

static int __exit vifb_of_remove(struct of_device *odev)
{
	return vifb_do_remove(&odev->dev);
}


static struct of_device_id vifb_of_match[] = {
	{ .compatible = "nintendo,flipper-video", },
	{ .compatible = "nintendo,hollywood-video", },
	{ },
};

MODULE_DEVICE_TABLE(of, vifb_of_match);

static struct of_platform_driver vifb_of_driver = {
	.owner = THIS_MODULE,
	.name = DRV_MODULE_NAME,
	.match_table = vifb_of_match,
	.probe = vifb_of_probe,
	.remove = vifb_of_remove,
};

/*
 * Module interface hooks
 *
 */

static int __init vifb_init_module(void)
{
#ifndef MODULE
	char *option = NULL;

	if (fb_get_options(DRV_MODULE_NAME, &option)) {
		/* for backwards compatibility */
		if (fb_get_options("gcnfb", &option))
			return -ENODEV;
	}
	vifb_setup(option);
#endif

	drv_printk(KERN_INFO, "%s - version %s\n", DRV_DESCRIPTION,
		   vifb_driver_version);

	return of_register_platform_driver(&vifb_of_driver);
}

static void __exit vifb_exit_module(void)
{
	of_unregister_platform_driver(&vifb_of_driver);
}

module_init(vifb_init_module);
module_exit(vifb_exit_module);

MODULE_DESCRIPTION(DRV_DESCRIPTION);
MODULE_AUTHOR(DRV_AUTHOR);
MODULE_LICENSE("GPL");

