/*
 * drivers/mmc/host/sdhci-tegra.c
 *
 * SDHCI-compatible driver for NVIDIA Tegra SoCs
 *
 * Copyright (c) 2009-2010, NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#define NV_DEBUG 0

#include <linux/mmc/host.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/irq.h>
#include <linux/mmc/card.h>
#include <linux/clk.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/regulator/consumer.h>

#include <mach/sdhci.h>
#include <mach/pinmux.h>
#include <nvodm_sdio.h>
#include "sdhci.h"

#define DRIVER_DESC "NVIDIA Tegra SDHCI compliant driver"
#define DRIVER_NAME "tegra-sdhci"
#define WLAN_SDHCI_HOST_ID 0

static struct sdhci_host *wlan_sdhci_host_ptr = NULL;

struct tegra_sdhci {
	struct platform_device	*pdev;
	struct clk		*clk;
	NvOdmSdioHandle		hOdmSdio;
	const struct tegra_pingroup_config *pinmux;
	int			nr_pins;
	int			gpio_cd;
	int			gpio_polarity_cd;
	int			irq_cd;
	int			gpio_wp;
	int			gpio_polarity_wp;
	unsigned int		debounce;
	unsigned long		max_clk;
	bool			card_present;
	bool			clk_enable;
	bool			card_always_on;
	struct mmc_ios 		resume_ios;
	struct work_struct	card_detection_wq;
	struct sdhci_host 	*sdhost;
};

static inline unsigned long res_size(struct resource *res)
{
	return res->end - res->start + 1;
}

static int tegra_sdhci_enable_dma(struct sdhci_host *sdhost)
{
	return 0;
}

static void do_card_detect(struct work_struct *work)
{
	struct tegra_sdhci *host = container_of(work,
			struct tegra_sdhci, card_detection_wq);

	sdhci_card_detect(host->sdhost);
}

void sdhci_tegra_wlan_detect(void)
{
	if ( NULL != wlan_sdhci_host_ptr) {
	        printk("%s Detecting WLAN\n", __FUNCTION__);
		sdhci_card_detect(wlan_sdhci_host_ptr);
	} else	
	       printk("%s WLAN host ptr NOT SAVED\n", __FUNCTION__);
}
EXPORT_SYMBOL(sdhci_tegra_wlan_detect);

#ifdef CONFIG_MACH_MOT
void tegra_sdhci_status_notify_cb(void *dev_id)
{
	struct sdhci_host *sdhost = dev_id;
	struct tegra_sdhci *host = sdhci_priv(sdhost);
	dev_info(&host->pdev->dev, "%s\n", __func__);
	sdhci_card_detect(sdhost);
}
#endif

static irqreturn_t card_detect_isr(int irq, void *dev_id)
{
	struct sdhci_host *sdhost = dev_id;
	struct tegra_sdhci *host = sdhci_priv(sdhost);

	host->card_present =
		(gpio_get_value(host->gpio_cd)==host->gpio_polarity_cd);
	smp_wmb();

	schedule_work(&(host->card_detection_wq));

	return IRQ_HANDLED;
}

static bool tegra_sdhci_card_detect(struct sdhci_host *sdhost)
{
	struct tegra_sdhci *host = sdhci_priv(sdhost);
	smp_rmb();
	return host->card_present;
}

static int tegra_sdhci_get_ro(struct sdhci_host *sdhost)
{
	struct tegra_sdhci *host = sdhci_priv(sdhost);

	BUG_ON(host->gpio_wp == -1);
	return (gpio_get_value(host->gpio_wp)==host->gpio_polarity_wp);
}

static void tegra_sdhci_set_clock(struct sdhci_host *sdhost,
	unsigned int clock)
{
	struct tegra_sdhci *host = sdhci_priv(sdhost);

	if (clock) {
		clk_set_rate(host->clk, clock);
		sdhost->max_clk = clk_get_rate(host->clk);
		dev_dbg(&host->pdev->dev, "clock request: %uKHz. currently "
			"%uKHz\n", clock/1000, sdhost->max_clk/1000);
	}

	if (clock && !host->clk_enable) {
		clk_enable(host->clk);
		host->clk_enable = true;
	} else if (!clock && host->clk_enable) {
		clk_disable(host->clk);
		host->clk_enable = false;
	}
}

static struct sdhci_ops tegra_sdhci_wp_cd_ops = {
	.enable_dma		= tegra_sdhci_enable_dma,
	.get_ro			= tegra_sdhci_get_ro,
	.card_detect		= tegra_sdhci_card_detect,
	.set_clock		= tegra_sdhci_set_clock,
};

static struct sdhci_ops tegra_sdhci_cd_ops = {
	.enable_dma		= tegra_sdhci_enable_dma,
	.card_detect		= tegra_sdhci_card_detect,
	.set_clock		= tegra_sdhci_set_clock,
};

static struct sdhci_ops tegra_sdhci_wp_ops = {
	.enable_dma		= tegra_sdhci_enable_dma,
	.get_ro			= tegra_sdhci_get_ro,
	.set_clock		= tegra_sdhci_set_clock,
};

static struct sdhci_ops tegra_sdhci_ops = {
	.enable_dma		= tegra_sdhci_enable_dma,
	.set_clock		= tegra_sdhci_set_clock,
};

//Was __init
static int tegra_sdhci_probe(struct platform_device *pdev)
{
	struct sdhci_host *sdhost;
	struct tegra_sdhci *host;
	struct tegra_sdhci_platform_data *plat = pdev->dev.platform_data;
	struct resource *res;
	int ret = -ENODEV;

	if (pdev->id == -1) {
		dev_err(&pdev->dev, "dynamic instance assignment not allowed\n");
		return -ENODEV;
	}

	sdhost  = sdhci_alloc_host(&pdev->dev, sizeof(struct tegra_sdhci));
	if (IS_ERR_OR_NULL(sdhost)) {
		dev_err(&pdev->dev, "unable to allocate driver structure\n");
		return (!sdhost) ? -ENOMEM : PTR_ERR(sdhost);
	}
	sdhost->hw_name = dev_name(&pdev->dev);

	host = sdhci_priv(sdhost);
	host->sdhost = sdhost;

	host->hOdmSdio = NvOdmSdioOpen(pdev->id);
	if (!host->hOdmSdio)
		dev_info(&pdev->dev, "no ODM SDIO adaptation\n");

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "no memory I/O resource provided\n");
		ret = -ENODEV;
		goto err_sdhci_alloc;
	}
	if (!request_mem_region(res->start, res_size(res),
				dev_name(&pdev->dev))) {
		dev_err(&pdev->dev, "memory in use\n");
		ret = -EBUSY;
		goto err_sdhci_alloc;
	}
	sdhost->ioaddr = ioremap(res->start, res_size(res));
	if (!sdhost->ioaddr) {
		dev_err(&pdev->dev, "failed to map registers\n");
		ret = -ENXIO;
		goto err_request_mem;
	}
	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!res) {
		dev_err(&pdev->dev, "no IRQ resource provided\n");
		ret = -ENODEV;
		goto err_ioremap;
	}
	sdhost->irq = res->start;
	INIT_WORK(&(host->card_detection_wq), do_card_detect);
	host->clk = clk_get(&pdev->dev, NULL);
	if (!host->clk) {
		dev_err(&pdev->dev, "unable to get clock\n");
		ret = -ENODEV;
		goto err_ioremap;
	}

	host->pdev = pdev;
	host->pinmux = plat->pinmux;
	host->nr_pins = plat->nr_pins;
	host->gpio_cd = plat->gpio_nr_cd;
	host->gpio_polarity_cd = plat->gpio_polarity_cd;
	host->gpio_wp = plat->gpio_nr_wp;
	host->gpio_polarity_wp = plat->gpio_polarity_wp;
	host->card_always_on = plat->is_always_on;
	dev_info(&pdev->dev, "write protect: %d, card detect: %d, always on: %d\n",
		 host->gpio_wp, host->gpio_cd, host->card_always_on);
	host->irq_cd = -1;
	host->debounce = plat->debounce;
	if (plat->max_clk)
		host->max_clk = min_t(unsigned int, 52000000, plat->max_clk);
	else {
		dev_info(&pdev->dev, "no max_clk specified, default to 52MHz\n");
		host->max_clk = 52000000;
	}

#ifdef CONFIG_EMBEDDED_MMC_START_OFFSET
	sdhost->start_offset = plat->offset;
#endif

	if (host->gpio_cd != -1) {
		ret = gpio_request(host->gpio_cd, "card_detect");
		if (ret < 0) {
			dev_err(&pdev->dev, "request cd gpio failed\n");
			host->gpio_cd = -1;
			goto skip_gpio_cd;
		}
		host->irq_cd = gpio_to_irq(host->gpio_cd);
		if (host->irq_cd < 0) {
			/* fall back to non-GPIO card detect mode */
			dev_err(&pdev->dev, "invalid card detect GPIO\n");
			host->gpio_cd = -1;
			host->irq_cd = -1;
			goto skip_gpio_cd;
		} 
		ret = gpio_direction_input(host->gpio_cd);
		if (ret < 0) {
			dev_err(&pdev->dev, "failed to configure GPIO\n");
			gpio_free(host->gpio_cd);
			host->gpio_cd = -1;
			goto skip_gpio_cd;
		}
		ret = request_irq(host->irq_cd, card_detect_isr,
			IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
			mmc_hostname(sdhost->mmc), sdhost);
		if (ret) {
			dev_err(&pdev->dev, "unable to request IRQ\n");
			gpio_free(host->gpio_cd);
			host->gpio_cd = -1;
			host->irq_cd = -1;
			goto skip_gpio_cd;
		}
		host->card_present =
			(gpio_get_value(host->gpio_cd)==host->gpio_polarity_cd);
	}
skip_gpio_cd:
	ret = 0;
	if (host->gpio_wp != -1) {
		ret = gpio_request(host->gpio_wp, "write_protect");
		if (ret < 0) {
			dev_err(&pdev->dev, "request wp gpio failed\n");
			host->gpio_wp = -1;
			goto skip_gpio_wp;
		}
		ret = gpio_direction_input(host->gpio_wp);
		if (ret < 0) {
			dev_err(&pdev->dev, "configure wp gpio failed\n");
			gpio_free(host->gpio_wp);
			host->gpio_wp = -1;
		}
	}
skip_gpio_wp:
	ret = 0;
	if (host->pinmux && host->nr_pins)
		tegra_pinmux_config_tristate_table(host->pinmux,
			host->nr_pins, TEGRA_TRI_NORMAL);
	clk_set_rate(host->clk, host->max_clk);
	clk_enable(host->clk);
	host->max_clk = clk_get_rate(host->clk);
	host->clk_enable = true;

	if (host->gpio_wp != -1 && (host->gpio_cd != -1 || !plat->is_removable))
		sdhost->ops = &tegra_sdhci_wp_cd_ops;
	else if (host->gpio_wp != -1)
		sdhost->ops = &tegra_sdhci_wp_ops;
	else if (host->gpio_cd != -1 || !plat->is_removable)
		sdhost->ops = &tegra_sdhci_cd_ops;
	else
		sdhost->ops = &tegra_sdhci_ops;

	sdhost->quirks =
		SDHCI_QUIRK_BROKEN_TIMEOUT_VAL |
		SDHCI_QUIRK_SINGLE_POWER_WRITE |
		SDHCI_QUIRK_ENABLE_INTERRUPT_AT_BLOCK_GAP |
		SDHCI_QUIRK_BROKEN_WRITE_PROTECT |
		SDHCI_QUIRK_BROKEN_CARD_DETECTION |
		SDHCI_QUIRK_BROKEN_CTRL_HISPD |
		SDHCI_QUIRK_RUNTIME_DISABLE;
#ifdef CONFIG_ARCH_TEGRA_2x_SOC
	sdhost->quirks |= SDHCI_QUIRK_BROKEN_SPEC_VERSION |
		SDHCI_QUIRK_NO_64KB_ADMA;
	sdhost->version = SDHCI_SPEC_200;
#endif

	if (!plat->is_removable)
		host->card_present = true;

#ifdef CONFIG_MACH_MOT
	if (plat->ocr_mask)
		sdhost->mmc->ocr_avail = plat->ocr_mask;

	if (plat->register_status_notify)
		plat->register_status_notify(tegra_sdhci_status_notify_cb,
									sdhost);
#endif

	sdhost->data_width = plat->bus_width;
	sdhost->dma_mask = DMA_BIT_MASK(32);
	if (plat->regulator_str != NULL) {
		sdhost->regulator = regulator_get(NULL, plat->regulator_str);
		if (IS_ERR(sdhost->regulator)) {
			pr_err("Unable to acquire sdio regulator: %s due to error %ld\n",
				  plat->regulator_str, PTR_ERR(sdhost->regulator));
			sdhost->regulator = NULL;
		}
	}

	sdhost->max_power_class = plat->max_power_class;

	ret = sdhci_add_host(sdhost);
	if (ret)
		goto fail;

	platform_set_drvdata(pdev, sdhost);

	if (pdev->id == WLAN_SDHCI_HOST_ID) 
		wlan_sdhci_host_ptr = sdhost;

	dev_info(&pdev->dev, "probe complete\n");

	return  0;

fail:
	if (host->irq_cd != -1)
		free_irq(host->irq_cd, sdhost);
	if (host->gpio_cd != -1)
		gpio_free(host->gpio_cd);
	if (host->gpio_wp != -1)
		gpio_free(host->gpio_wp);

	if (host->pinmux && host->nr_pins)
		tegra_pinmux_config_tristate_table(host->pinmux,
			host->nr_pins, TEGRA_TRI_TRISTATE);

	clk_disable(host->clk);
	clk_put(host->clk);
err_ioremap:
	iounmap(sdhost->ioaddr);
err_request_mem:
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	release_mem_region(res->start, res_size(res));
err_sdhci_alloc:
	if (host->hOdmSdio)
		NvOdmSdioClose(host->hOdmSdio);
	sdhci_free_host(sdhost);
	dev_err(&pdev->dev, "probe failed\n");
	return ret;
}


static int tegra_sdhci_remove(struct platform_device *pdev)
{
	struct sdhci_host *sdhost = platform_get_drvdata(pdev);
	struct tegra_sdhci *host = sdhci_priv(sdhost);

	if (host->irq_cd != -1)
		free_irq(host->irq_cd, sdhost);

	if (host->gpio_cd != -1)
		gpio_free(host->gpio_cd);

	if (host->gpio_wp != -1)
		gpio_free(host->gpio_wp);

	if (host->pinmux && host->nr_pins)
		tegra_pinmux_config_tristate_table(host->pinmux,
			host->nr_pins, TEGRA_TRI_TRISTATE);

	if (host->clk_enable)
		clk_disable(host->clk);

	clk_put(host->clk);
	iounmap(sdhost->ioaddr);
	sdhost->ioaddr = NULL;

	if (host->hOdmSdio)
		NvOdmSdioClose(host->hOdmSdio);

	sdhci_free_host(sdhost);
	return 0;
}

#define is_card_sdio(_card) \
	((_card) && ((_card)->type == MMC_TYPE_SDIO))

#if defined(CONFIG_PM)
#define dev_to_host(_dev) platform_get_drvdata(to_platform_device(_dev))

static void tegra_sdhci_configure_interrupts(struct sdhci_host *sdhost, bool enable)
{
	u32 ierr;
	u32 clear = SDHCI_INT_ALL_MASK;
	u32 set;

	if (enable) {
		/* enable required MMC INTs */
		set = SDHCI_INT_BUS_POWER | SDHCI_INT_DATA_END_BIT |
		SDHCI_INT_DATA_CRC | SDHCI_INT_DATA_TIMEOUT | SDHCI_INT_INDEX |
		SDHCI_INT_END_BIT | SDHCI_INT_CRC | SDHCI_INT_TIMEOUT |
		SDHCI_INT_DATA_END | SDHCI_INT_RESPONSE;

		ierr = sdhci_readl(sdhost, SDHCI_INT_ENABLE);
		ierr &= clear;
		ierr |= set;
		sdhci_writel(sdhost, ierr, SDHCI_INT_ENABLE);
		sdhci_writel(sdhost, ierr, SDHCI_SIGNAL_ENABLE);
	} else {
		/* disable the interrupts */
		ierr = sdhci_readl(sdhost, SDHCI_INT_ENABLE);
		/* Card interrupt masking is done by sdio client driver */
		ierr &= SDHCI_INT_CARD_INT;
		sdhci_writel(sdhost, ierr, SDHCI_INT_ENABLE);
		sdhci_writel(sdhost, ierr, SDHCI_SIGNAL_ENABLE);
	}
}

static int tegra_sdhci_restore(struct sdhci_host *sdhost)
{
	unsigned long timeout;
	u8 mask = SDHCI_RESET_ALL;

	sdhci_writeb(sdhost, mask, SDHCI_SOFTWARE_RESET);

	sdhost->clock = 0;

	/* Wait max 100 ms */
	timeout = 100;

	/* hw clears the bit when it's done */
	while (sdhci_readb(sdhost, SDHCI_SOFTWARE_RESET) & mask) {
		if (timeout == 0) {
			pr_err("%s: Reset 0x%x never completed.\n",
				mmc_hostname(sdhost->mmc), (int)mask);
			return -EIO;
		}
		timeout--;
		mdelay(1);
	}

	tegra_sdhci_configure_interrupts(sdhost, true);
	sdhost->last_clk = 0;
	return 0;
}

static int tegra_sdhci_suspend(struct device *dev)
{
	struct sdhci_host *sdhost = dev_to_host(dev);
	struct tegra_sdhci *host = sdhci_priv(sdhost);
	struct pm_message event = { PM_EVENT_SUSPEND };
	int ret = 0;

	if (host->card_always_on && is_card_sdio(sdhost->mmc->card)) {
		struct mmc_ios ios;

		memcpy(&host->resume_ios, &sdhost->mmc->ios, sizeof(struct mmc_ios));
		memcpy(&ios, &sdhost->mmc->ios, sizeof(struct mmc_ios));
		ios.clock = 0;
		ios.vdd = 0;
		ios.power_mode = MMC_POWER_OFF;
		ios.bus_width = MMC_BUS_WIDTH_1;
		ios.timing = MMC_TIMING_LEGACY;
		sdhost->mmc->ops->set_ios(sdhost->mmc, &ios);

		/* Disable the interrupts */
		tegra_sdhci_configure_interrupts(sdhost, false);

		return ret;
	}

	ret = sdhci_suspend_host(sdhost, event);
	if (ret) {
		dev_err(dev, "failed to suspend host\n");
		return ret;
	}

	if (host->hOdmSdio)
		NvOdmSdioSuspend(host->hOdmSdio);

	return ret;
}

static int tegra_sdhci_resume(struct device *dev)
{
	struct sdhci_host *sdhost = dev_to_host(dev);
	struct tegra_sdhci *host = sdhci_priv(sdhost);

	if (!host->clk_enable) {
		clk_enable(host->clk);
		host->clk_enable = true;
	}

	if (host->gpio_cd != -1)
		host->card_present =
			(gpio_get_value(host->gpio_cd) == host->gpio_polarity_cd);

	if (host->card_always_on && is_card_sdio(sdhost->mmc->card)) {
		int ret = 0;

		/* soft reset SD host controller and enable MMC INTs */
		ret = tegra_sdhci_restore(sdhost);
		if (ret) {
			dev_err(dev, "failed to resume host\n");
			return ret;
		}

		mmiowb();
		sdhost->mmc->ops->set_ios(sdhost->mmc, &host->resume_ios);
		return 0;
	}

	if (host->hOdmSdio)
		NvOdmSdioResume(host->hOdmSdio);

	return sdhci_resume_host(sdhost);
}
static struct dev_pm_ops tegra_sdhci_pm = {
	.suspend = tegra_sdhci_suspend,
	.resume = tegra_sdhci_resume,
};
#define tegra_sdhci_pm_ops &tegra_sdhci_pm
#else
#define tegra_sdhci_pm_ops NULL
#endif

struct platform_driver tegra_sdhci_driver = {
	.probe		= tegra_sdhci_probe,
	.remove		= tegra_sdhci_remove,
	.driver		= {
		.name	= "tegra-sdhci",
		.owner	= THIS_MODULE,
		.pm	= tegra_sdhci_pm_ops,
	},
};

static int __init tegra_sdhci_init(void)
{
	return platform_driver_register(&tegra_sdhci_driver);
}

static void __exit tegra_sdhci_exit(void)
{
	platform_driver_unregister(&tegra_sdhci_driver);
}

module_init(tegra_sdhci_init);
module_exit(tegra_sdhci_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION(DRIVER_DESC);
