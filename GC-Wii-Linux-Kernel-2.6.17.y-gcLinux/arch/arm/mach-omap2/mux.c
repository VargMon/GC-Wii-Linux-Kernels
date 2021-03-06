/*
 * linux/arch/arm/mach-omap2/mux.c
 *
 * OMAP1 pin multiplexing configurations
 *
 * Copyright (C) 2003 - 2005 Nokia Corporation
 *
 * Written by Tony Lindgren <tony.lindgren@nokia.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */
#include <linux/config.h>
#include <linux/module.h>
#include <linux/init.h>
#include <asm/system.h>
#include <asm/io.h>
#include <linux/spinlock.h>

#include <asm/arch/mux.h>

#ifdef CONFIG_OMAP_MUX

/* NOTE: See mux.h for the enumeration */

struct pin_config __initdata_or_module omap24xx_pins[] = {
/*
 *	description			mux	mux	pull	pull	debug
 *					offset	mode	ena	type
 */

/* 24xx I2C */
MUX_CFG_24XX("M19_24XX_I2C1_SCL",	0x111,	0,	0,	0,	1)
MUX_CFG_24XX("L15_24XX_I2C1_SDA",	0x112,	0,	0,	0,	1)
MUX_CFG_24XX("J15_24XX_I2C2_SCL",	0x113,	0,	0,	0,	1)
MUX_CFG_24XX("H19_24XX_I2C2_SDA",	0x114,	0,	0,	0,	1)

/* Menelaus interrupt */
MUX_CFG_24XX("W19_24XX_SYS_NIRQ",	0x12c,	0,	1,	1,	1)

/* 24xx clocks */
MUX_CFG_24XX("W14_24XX_SYS_CLKOUT",	0x137,	0,	1,	1,	1)

/* 24xx McBSP */
MUX_CFG_24XX("Y15_24XX_MCBSP2_CLKX",	0x124,	1,	1,	0,	1)
MUX_CFG_24XX("R14_24XX_MCBSP2_FSX",	0x125,	1,	1,	0,	1)
MUX_CFG_24XX("W15_24XX_MCBSP2_DR",	0x126,	1,	1,	0,	1)
MUX_CFG_24XX("V15_24XX_MCBSP2_DX",	0x127,	1,	1,	0,	1)

/* 24xx GPIO */
MUX_CFG_24XX("M21_242X_GPIO11",	 0x0c9,  3,      1,      1,      1)
MUX_CFG_24XX("AA10_242X_GPIO13",	0x0e5,  3,      0,      0,      1)
MUX_CFG_24XX("AA6_242X_GPIO14",	 0x0e6,  3,      0,      0,      1)
MUX_CFG_24XX("AA4_242X_GPIO15",	 0x0e7,  3,      0,      0,      1)
MUX_CFG_24XX("Y11_242X_GPIO16",	 0x0e8,  3,      0,      0,      1)
MUX_CFG_24XX("AA12_242X_GPIO17",	0x0e9,  3,      0,      0,      1)
MUX_CFG_24XX("AA8_242X_GPIO58",	 0x0ea,  3,      0,      0,      1)
MUX_CFG_24XX("Y20_24XX_GPIO60",		0x12c,	3,	0,	0,	1)
MUX_CFG_24XX("W4__24XX_GPIO74",	 0x0f2,  3,      0,      0,      1)
MUX_CFG_24XX("M15_24XX_GPIO92",		0x10a,	3,	0,	0,	1)
MUX_CFG_24XX("V14_24XX_GPIO117",	0x128,	3,	1,	0,	1)

/* TSC IRQ */
MUX_CFG_24XX("P20_24XX_TSC_IRQ",	0x108,	0,	0,	0,	1)

/* UART3  */
MUX_CFG_24XX("K15_24XX_UART3_TX",	0x118,	0,	0,	0,	1)
MUX_CFG_24XX("K14_24XX_UART3_RX",	0x119,	0,	0,	0,	1)

/* Keypad GPIO*/
MUX_CFG_24XX("T19_24XX_KBR0",		0x106,	3,	1,	1,	1)
MUX_CFG_24XX("R19_24XX_KBR1",		0x107,	3,	1,	1,	1)
MUX_CFG_24XX("V18_24XX_KBR2",		0x139,	3,	1,	1,	1)
MUX_CFG_24XX("M21_24XX_KBR3",		0xc9,	3,	1,	1,	1)
MUX_CFG_24XX("E5__24XX_KBR4",		0x138,	3,	1,	1,	1)
MUX_CFG_24XX("M18_24XX_KBR5",		0x10e,	3,	1,	1,	1)
MUX_CFG_24XX("R20_24XX_KBC0",		0x108,	3,	0,	0,	1)
MUX_CFG_24XX("M14_24XX_KBC1",		0x109,	3,	0,	0,	1)
MUX_CFG_24XX("H19_24XX_KBC2",		0x114,	3,	0,	0,	1)
MUX_CFG_24XX("V17_24XX_KBC3",		0x135,	3,	0,	0,	1)
MUX_CFG_24XX("P21_24XX_KBC4",		0xca,	3,	0,	0,	1)
MUX_CFG_24XX("L14_24XX_KBC5",		0x10f,	3,	0,	0,	1)
MUX_CFG_24XX("N19_24XX_KBC6",		0x110,	3,	0,	0,	1)

/* 24xx Menelaus Keypad GPIO */
MUX_CFG_24XX("B3__24XX_KBR5",		0x30,	3,	1,	1,	1)
MUX_CFG_24XX("AA4_24XX_KBC2",		0xe7,	3,	0,	0,	1)
MUX_CFG_24XX("B13_24XX_KBC6",		0x110,	3,	0,	0,	1)

};

int __init omap2_mux_init(void)
{
	omap_mux_register(omap24xx_pins, ARRAY_SIZE(omap24xx_pins));
	return 0;
}

#endif
