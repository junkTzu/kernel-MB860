/*
 * include/asm-arm/arch-tegra/include/mach/sdhci.h
 *
 * Copyright (C) 2009 Palm, Inc.
 * Author: Yvonne Yip <y@palm.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#ifndef __ASM_ARM_ARCH_TEGRA_SDHCI_H
#define __ASM_ARM_ARCH_TEGRA_SDHCI_H

#include <linux/mmc/host.h>
#include <asm/mach/mmc.h>

#include <mach/pinmux.h>
#include "nvcommon.h"
#include "nvodm_query.h"

struct tegra_sdhci_platform_data {
	int cd_gpio;
	int wp_gpio;
	int power_gpio;
	int is_8bit;
	struct mmc_platform_data mmc_data;

	//Stuff from legacy Tegra SDHCI drivers
	const struct tegra_pingroup_config *pinmux;
	int nr_pins;
	int gpio_nr_cd;		/* card detect gpio, -1 if unused */
	int gpio_polarity_cd;	/* active high card detect */
	int gpio_nr_wp;		/* write protect gpio, -1 if unused */
	int gpio_polarity_wp;	/* active high write protect */
	int bus_width;		/* bus width in bits */
	int is_removable;	/* card can be removed */
	unsigned int debounce;	/* debounce time in milliseconds */
	unsigned long max_clk;	/* maximum card clock */
	unsigned int max_power_class;
	int is_always_on;	/* card is not powered down in suspend */
#ifdef CONFIG_EMBEDDED_MMC_START_OFFSET
	unsigned long offset;	/* offset in blocks to MBR */
#endif
	char *regulator_str;	/* Voltage regulator used to control the
							   the card. */
#ifdef CONFIG_MACH_MOT
	unsigned int ocr_mask;	/* available voltages */
	int (*register_status_notify)\
		(void (*callback)(void *dev_id), void *dev_id);
	struct embedded_sdio_data *embedded_sdio;
#endif
};

#endif
