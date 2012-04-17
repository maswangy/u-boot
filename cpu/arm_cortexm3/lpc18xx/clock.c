/*
 * (C) Copyright 2012
 *
 * Alexander Potashev, Emcraft Systems, aspotashev@emcraft.com
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#include <common.h>

#include "clock.h"

/*
 * PLL0* register map
 * Used for PLL0USB at 0x4005001C and for PLL0AUDIO at 0x4005002C.
 *
 * This structure is 0x10 bytes long, it is important when it embedding into
 * `struct lpc18xx_cgu_regs`.
 */
struct lpc18xx_pll0_regs {
	u32 stat;	/* PLL status register */
	u32 ctrl;	/* PLL control register */
	u32 mdiv;	/* PLL M-divider register */
	u32 np_div;	/* PLL N/P-divider register */
};

/*
 * CGU (Clock Generation Unit) register map
 * Should be mapped at 0x40050000.
 */
struct lpc18xx_cgu_regs {
	u32 rsv0[5];
	u32 freq_mon;		/* Frequency monitor */
	u32 xtal_osc_ctrl;	/* XTAL oscillator control */
	struct lpc18xx_pll0_regs pll0usb;	/* PLL0USB registers */
	struct lpc18xx_pll0_regs pll0audio;	/* PLL0AUDIO registers */
	u32 pll0audio_frac;	/* PLL0AUDIO fractional divider */
	u32 pll1_stat;		/* PLL1 status register */
	u32 pll1_ctrl;		/* PLL1 control register */
	u32 idiv[5];		/* IDIVA_CTRL .. IDIVE_CTRL */

	/* BASE_* clock configuration registers */
	u32 safe_clk;
	u32 usb0_clk;
	u32 periph_clk;
	u32 usb1_clk;
	u32 m4_clk;
	u32 spifi_clk;
	u32 spi_clk;
	u32 phy_rx_clk;
	u32 phy_tx_clk;
	u32 apb1_clk;
	u32 apb3_clk;
	u32 lcd_clk;
	u32 vadc_clk;
	u32 sdio_clk;
	u32 ssp0_clk;
	u32 ssp1_clk;
	u32 uart0_clk;
	u32 uart1_clk;
	u32 uart2_clk;
	u32 uart3_clk;
	u32 out_clk;
	u32 rsv1[4];
	u32 apll_clk;
	u32 cgu_out0_clk;
	u32 cgu_out1_clk;
};

/*
 * CGU registers base
 */
#define LPC18XX_CGU_BASE		0x40050000
#define LPC18XX_CGU			((volatile struct lpc18xx_cgu_regs *) \
					LPC18XX_CGU_BASE)

/*
 * Bit offsets in Clock Generation Unit (CGU) registers
 */
/*
 * Crystal oscillator control register (XTAL_OSC_CTRL)
 */
/* Oscillator-pad enable */
#define LPC18XX_CGU_XTAL_ENABLE		(1 << 0)
/* Select frequency range */
#define LPC18XX_CGU_XTAL_HF		(1 << 2)

#if (CONFIG_LPC18XX_EXTOSC_RATE < 10000000) || \
    (CONFIG_LPC18XX_EXTOSC_RATE > 25000000)
#error CONFIG_LPC18XX_EXTOSC_RATE is out of range for PLL1
#endif
/*
 * For all CGU clock registers
 */
/* CLK_SEL: Clock source selection */
#define LPC18XX_CGU_CLKSEL_BITS		24
#define LPC18XX_CGU_CLKSEL_MSK		(0x1f << LPC18XX_CGU_CLKSEL_BITS)
/* Crystal oscillator */
#define LPC18XX_CGU_CLKSEL_XTAL		(0x06 << LPC18XX_CGU_CLKSEL_BITS)
/* PLL1 */
#define LPC18XX_CGU_CLKSEL_PLL1		(0x09 << LPC18XX_CGU_CLKSEL_BITS)
/* Block clock automatically during frequency change */
#define LPC18XX_CGU_AUTOBLOCK_MSK	(1 << 11)
/*
 * PLL1 control register
 */
/* Power-down */
#define LPC18XX_CGU_PLL1CTRL_PD_MSK		(1 << 0)
/* Input clock bypass control */
#define LPC18XX_CGU_PLL1CTRL_BYPASS_MSK		(1 << 1)
/* PLL feedback select */
#define LPC18XX_CGU_PLL1CTRL_FBSEL_MSK		(1 << 6)
/* PLL direct CCO output */
#define LPC18XX_CGU_PLL1CTRL_DIRECT_MSK		(1 << 7)
/* Post-divider division ratio P. The value applied is 2**P. */
#define LPC18XX_CGU_PLL1CTRL_PSEL_BITS		8
#define LPC18XX_CGU_PLL1CTRL_PSEL_MSK \
	(3 << LPC18XX_CGU_PLL1CTRL_PSEL_BITS)
/* Pre-divider division ratio */
#define LPC18XX_CGU_PLL1CTRL_NSEL_BITS		12
#define LPC18XX_CGU_PLL1CTRL_NSEL_MSK \
	(3 << LPC18XX_CGU_PLL1CTRL_NSEL_BITS)
/* Feedback-divider division ratio (M) */
#define LPC18XX_CGU_PLL1CTRL_MSEL_BITS		16
#define LPC18XX_CGU_PLL1CTRL_MSEL_MSK \
	(0xff << LPC18XX_CGU_PLL1CTRL_MSEL_BITS)
/*
 * PLL1 status register
 */
/* PLL1 lock indicator */
#define LPC18XX_CGU_PLL1STAT_LOCK	(1 << 0)

/*
 * Clock values
 */
static u32 clock_val[CLOCK_END];

/*
 * Set LPC18XX_PLL1_CLK_OUT to the output rate of PLL1
 */
#define LPC18XX_PLL1_CLK_OUT \
	(CONFIG_LPC18XX_EXTOSC_RATE * CONFIG_LPC18XX_PLL1_M)

/*
 * Compile time sanity checks for defined board clock setup
 */
#ifndef CONFIG_LPC18XX_EXTOSC_RATE
#error CONFIG_LPC18XX_EXTOSC_RATE is not set, set to the external osc rate
#endif

/*
 * Verify that the request PLL1 output frequency fits the range
 * of 156 MHz to 320 MHz, so that the PLL1 Direct Mode is applicable.
 * Our clock configuration code (`clock_setup()`) support only this mode.
 */
#if LPC18XX_PLL1_CLK_OUT < 156000000
#error Requested PLL1 output frequency is too low
#endif
#if LPC18XX_PLL1_CLK_OUT > 320000000
#error Requested PLL1 output frequency is too high
#endif

/*
 * We cannot change the PLL1 multiplier value immediately to the maximum, it has
 * to be increased in two steps. The following is the value for the first step.
 */
#define LPC18XX_PLL1_M_INTERMEDIATE	9

/*
 * Use this function to implement delays until the clock system is initialized
 */
static void cycle_delay(int n)
{
	volatile int i;
	for (i = 0; i < n; i++);
}

/*
 * Set-up the external crystal oscillator, PLL1, CPU core clock and
 * all necessary clocks for peripherals.
 */
static void clock_setup(void)
{
	/*
	 * Configure and enable the external crystal oscillator
	 */
#if CONFIG_LPC18XX_EXTOSC_RATE > 15000000
	LPC18XX_CGU->xtal_osc_ctrl |= LPC18XX_CGU_XTAL_HF;
#else
	LPC18XX_CGU->xtal_osc_ctrl &= ~LPC18XX_CGU_XTAL_HF;
#endif
	LPC18XX_CGU->xtal_osc_ctrl &= ~LPC18XX_CGU_XTAL_ENABLE;

	/*
	 * Wait for the external oscillator to stabilize
	 */
	cycle_delay(5000);

	/*
	 * Switch the M4 core clock to the 12MHz external oscillator
	 */
	LPC18XX_CGU->m4_clk =
		(LPC18XX_CGU->m4_clk & ~LPC18XX_CGU_CLKSEL_MSK) |
		LPC18XX_CGU_CLKSEL_XTAL | LPC18XX_CGU_AUTOBLOCK_MSK;

	/*
	 * Reset PLL1 configuration
	 */
	LPC18XX_CGU->pll1_ctrl =
		(LPC18XX_CGU->pll1_ctrl &
			~LPC18XX_CGU_CLKSEL_MSK &
			~LPC18XX_CGU_PLL1CTRL_PSEL_MSK &
			~LPC18XX_CGU_PLL1CTRL_NSEL_MSK &
			~LPC18XX_CGU_PLL1CTRL_MSEL_MSK &
			~LPC18XX_CGU_PLL1CTRL_BYPASS_MSK) |
		LPC18XX_CGU_CLKSEL_XTAL;

	/*
	 * Intermediate PLL1 configuration, do not reach the desired output rate
	 * at this point.
	 */
	LPC18XX_CGU->pll1_ctrl |=
		LPC18XX_CGU_PLL1CTRL_FBSEL_MSK |
		LPC18XX_CGU_PLL1CTRL_DIRECT_MSK |
		(0 << LPC18XX_CGU_PLL1CTRL_NSEL_BITS) |
		((LPC18XX_PLL1_M_INTERMEDIATE - 1) <<
			LPC18XX_CGU_PLL1CTRL_MSEL_BITS);

	/*
	 * Enable PLL1 if it was disabled
	 */
	LPC18XX_CGU->pll1_ctrl &= ~LPC18XX_CGU_PLL1CTRL_PD_MSK;

	/*
	 * Wait for the PLL1 lock detector
	 */
	while (!(LPC18XX_CGU->pll1_stat & LPC18XX_CGU_PLL1STAT_LOCK));

	/*
	 * Use PLL1 as clock source for BASE_M4_CL and BASE_UARTx_CLK
	 */
	LPC18XX_CGU->m4_clk    = LPC18XX_CGU_CLKSEL_PLL1 |
		LPC18XX_CGU_AUTOBLOCK_MSK;
	LPC18XX_CGU->uart0_clk = LPC18XX_CGU_CLKSEL_PLL1 |
		LPC18XX_CGU_AUTOBLOCK_MSK;
	LPC18XX_CGU->uart1_clk = LPC18XX_CGU_CLKSEL_PLL1 |
		LPC18XX_CGU_AUTOBLOCK_MSK;
	LPC18XX_CGU->uart2_clk = LPC18XX_CGU_CLKSEL_PLL1 |
		LPC18XX_CGU_AUTOBLOCK_MSK;
	LPC18XX_CGU->uart3_clk = LPC18XX_CGU_CLKSEL_PLL1 |
		LPC18XX_CGU_AUTOBLOCK_MSK;

	/*
	 * Raise PLL1 multiplier to the requested value
	 */
	LPC18XX_CGU->pll1_ctrl =
		(LPC18XX_CGU->pll1_ctrl & ~LPC18XX_CGU_PLL1CTRL_MSEL_MSK) |
		((CONFIG_LPC18XX_PLL1_M - 1) <<
			LPC18XX_CGU_PLL1CTRL_MSEL_BITS);
}

/*
 * Initialize the reference clocks.
 */
void clock_init(void)
{
	clock_setup();

	/*
	 * Set SysTick timer rate to the CPU core clock
	 */
	clock_val[CLOCK_SYSTICK] = LPC18XX_PLL1_CLK_OUT;

	/*
	 * Set the CPU core clock
	 */
	clock_val[CLOCK_CCLK] = LPC18XX_PLL1_CLK_OUT;

	/*
	 * Set UARTx base clock rate
	 */
	clock_val[CLOCK_UART0] = LPC18XX_PLL1_CLK_OUT;
	clock_val[CLOCK_UART1] = LPC18XX_PLL1_CLK_OUT;
	clock_val[CLOCK_UART2] = LPC18XX_PLL1_CLK_OUT;
	clock_val[CLOCK_UART3] = LPC18XX_PLL1_CLK_OUT;
}

/*
 * Return a clock value for the specified clock.
 *
 * @param clck          id of the clock
 * @returns             frequency of the clock
 */
unsigned long clock_get(enum clock clck)
{
	return clock_val[clck];
}
