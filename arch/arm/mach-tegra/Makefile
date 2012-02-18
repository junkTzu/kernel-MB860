obj-y                                   += ahb.o
obj-y                                   += common.o
obj-$(CONFIG_ARCH_TEGRA_2x_SOC)         += common-t2.o
obj-y									+= board-common.o
obj-y                                   += devices.o
obj-y                                   += apbio.o
obj-y                                   += io.o
obj-y                                   += irq.o
obj-$(CONFIG_TEGRA_GRHOST)              += syncpt.o
obj-y                                   += clock.o
obj-y                                   += timer.o
obj-y									+= syncpt.o
ifeq ($(CONFIG_ARCH_TEGRA_2x_SOC),y)
obj-y                                   += tegra2_clocks.o
obj-y                                   += timer-t2.o
endif
obj-y                                   += gpio.o
obj-y                                   += sleep.o
obj-$(CONFIG_ARCH_TEGRA_2x_SOC)         += sleep-t2.o
obj-y									+= pinmux.o
obj-$(CONFIG_ARCH_TEGRA_2x_SOC)			+= pinmux-t2-tables.o
obj-y                                   += devices.o
obj-y                                   += delay.o
ifeq ($(CONFIG_SMP),y)
obj-y									+= platsmp.o localtimer.o headsmp.o
endif
obj-$(CONFIG_ARCH_TEGRA_2x_SOC)			+= fuse-cache.o
obj-$(CONFIG_MACH_TEGRA_GENERIC)		+= board-generic.o
obj-$(CONFIG_MACH_MOT)					+= board-mot.o board-mot-power.o board-mot-lights.o board-mot-touch.o board-mot-sensors.o board-mot-misc.o board-mot-modem.o board-mot-wlan.o board-mot-usb.o board-mot-camera.o
obj-$(CONFIG_MOT_WIMAX)					+= board-mot-wimax.o
obj-$(CONFIG_CPU_FREQ)                  += cpu-tegra.o
ifeq ($(CONFIG_TEGRA_AUTO_HOTPLUG),y)
obj-$(CONFIG_ARCH_TEGRA_3x_SOC)         += cpu-tegra3.o
endif
ifeq ($(CONFIG_CPU_IDLE),y)
obj-y                                   += cpuidle.o
ifeq ($(CONFIG_PM_SLEEP),y)
obj-$(CONFIG_ARCH_TEGRA_2x_SOC)         += cpuidle-t2.o
endif
endif
obj-$(CONFIG_TEGRA_IOVMM)				+= iovmm.o
obj-$(CONFIG_TEGRA_IOVMM_GART)			+= iovmm-gart.o
obj-y                                   += reset.o
obj-$(CONFIG_TEGRA_SYSTEM_DMA)			+= dma.o
obj-$(CONFIG_TEGRA_PCI)					+= pci.o
obj-$(CONFIG_TEGRA_PCI)					+= pci-enum.o
obj-y                                   += powergate.o
obj-$(CONFIG_PM_SLEEP)					+= pm.o
obj-$(CONFIG_PM_SLEEP)					+= pm-irq.o
obj-y                                   += gic.o
obj-y									+= fuse.o
obj-y                                   += kfuse.o
obj-$(CONFIG_TEGRA_SILICON_PLATFORM)    += tegra_odm_fuses.o
obj-y                                   += i2c_error_recovery.o
obj-$(CONFIG_TEGRA_LEGACY_AUDIO)        += tegra_i2s_audio.o
obj-$(CONFIG_TEGRA_LEGACY_AUDIO)        += tegra_spdif_audio.o
obj-y									+= mc.o
obj-$(CONFIG_USB_SUPPORT)               += usb_phy.o
obj-$(CONFIG_FIQ)                       += fiq.o
obj-$(CONFIG_TEGRA_FIQ_DEBUGGER)        += tegra_fiq_debugger.o
obj-$(CONFIG_TEGRA_PWM)                 += pwm.o
obj-$(CONFIG_TEGRA_ARB_SEMAPHORE)		+= arb_sema.o
ifeq ($(CONFIG_TEGRA_SILICON_PLATFORM),y)
obj-y                                   += dvfs.o
obj-$(CONFIG_ARCH_TEGRA_2x_SOC)         += tegra2_dvfs.o
obj-$(CONFIG_ARCH_TEGRA_3x_SOC)         += tegra3_dvfs.o
obj-$(CONFIG_ARCH_TEGRA_3x_SOC)         += latency_allowance.o
obj-$(CONFIG_TEGRA_EDP_LIMITS)          += edp.o
endif
ifeq ($(CONFIG_TEGRA_SILICON_PLATFORM),y)
obj-$(CONFIG_ARCH_TEGRA_2x_SOC)         += tegra2_speedo.o
obj-$(CONFIG_ARCH_TEGRA_3x_SOC)         += tegra3_speedo.o
obj-$(CONFIG_ARCH_TEGRA_3x_SOC)         += tegra3_actmon.o
endif

obj-$(CONFIG_ARCH_TEGRA_2x_SOC)         += tegra2_emc.o
obj-$(CONFIG_ARCH_TEGRA_3x_SOC)         += tegra3_emc.o
obj-$(CONFIG_ARCH_TEGRA_2x_SOC)         += wakeups-t2.o
obj-$(CONFIG_ARCH_TEGRA_3x_SOC)         += wakeups-t3.o
obj-$(CONFIG_ARCH_TEGRA_2x_SOC)         += pm-t2.o
obj-$(CONFIG_ARCH_TEGRA_3x_SOC)         += pm-t3.o

obj-$(CONFIG_ARCH_TEGRA_2x_SOC)         += pinmux-t2-tables.o
obj-$(CONFIG_LOCAL_TIMERS)				+= localtimer.o
obj-$(CONFIG_SMP)						+= platsmp.o
obj-$(CONFIG_HOTPLUG_CPU)				+= hotplug.o
obj-$(CONFIG_SMP)						+= headsmp.o
obj-$(CONFIG_TEGRA_SYSTEM_DMA)			+= dma.o
obj-$(CONFIG_CPU_FREQ)                  += cpu-tegra.o
obj-$(CONFIG_TEGRA_PCI)					+= pcie.o
obj-$(CONFIG_USB_SUPPORT)				+= usb_phy.o

ifeq ($(CONFIG_TEGRA_THERMAL_THROTTLE),y)
obj-$(CONFIG_ARCH_TEGRA_2x_SOC)         += tegra2_throttle.o
obj-$(CONFIG_ARCH_TEGRA_3x_SOC)         += tegra3_throttle.o
endif

ccflags-$(CONFIG_TEGRA_NVOS)			+= -DNV_DEBUG=0
ccflags-$(CONFIG_TEGRA_NVOS)			+= -DNV_OAL=0
ccflags-$(CONFIG_TEGRA_NVOS)			+= -DNV_IS_AVP=0
ccflags-$(CONFIG_TEGRA_NVOS)			+= -DNV_USE_FUSE_CLOCK_ENABLE=0
obj-$(CONFIG_TEGRA_NVOS)				+= nvos_user.o
obj-$(CONFIG_TEGRA_NVOS)				+= nvos/

obj-$(CONFIG_TEGRA_NVRM)				+= board-nvodm.o
obj-$(CONFIG_TEGRA_NVRM)				+= nvrm/
obj-$(CONFIG_TEGRA_NVRM)				+= nvodm/
obj-$(CONFIG_TEGRA_NVRM)				+= odm_kit/
obj-$(CONFIG_TEGRA_NVRM)				+= nvreftrack/
obj-$(CONFIG_TEGRA_NVRM)				+= nvrm_user.o
obj-$(CONFIG_TEGRA_NVRM)				+= nvrpc_user.o
obj-$(CONFIG_TEGRA_NVRM)				+= tegra_exports.o
obj-y									+= nvddk/

obj-$(CONFIG_TEGRA_NVEC)				+= nvec_user.o
obj-$(CONFIG_TEGRA_NVEC)				+= nvec/

obj-$(CONFIG_TEGRA_FUSE)				+= sysfs-fuse.o
obj-$(CONFIG_TEGRA_ODM_VIBRATE)			+= vibrate.o
obj-$(CONFIG_TEGRA_AES_USER)			+= nvaes_user.o

obj-$(CONFIG_INPUT_MOT_WEBTOP)          += aev.o evfwd.o

obj-${CONFIG_MACH_OLYMPUS}              += board-olympus.o
obj-${CONFIG_MACH_OLYMPUS}              += board-olympus-i2c.o
obj-${CONFIG_MACH_OLYMPUS}              += board-olympus-keypad.o
obj-${CONFIG_MACH_OLYMPUS}              += board-olympus-pinmux.o
obj-${CONFIG_MACH_OLYMPUS}              += board-olympus-panel.o

obj-${CONFIG_MACH_PAZ00}				+= board-paz00.o
obj-${CONFIG_MACH_PAZ00}				+= board-paz00-pinmux.o

obj-${CONFIG_MACH_SEABOARD}             += board-seaboard.o
obj-${CONFIG_MACH_SEABOARD}             += board-seaboard-pinmux.o

obj-${CONFIG_MACH_STINGRAY}             += board-stingray.o
obj-${CONFIG_MACH_STINGRAY}             += board-stingray-pinmux.o
obj-${CONFIG_MACH_STINGRAY}             += board-stingray-panel.o
obj-${CONFIG_MACH_STINGRAY}             += board-stingray-keypad.o
obj-${CONFIG_MACH_STINGRAY}             += board-stingray-wifi.o
obj-${CONFIG_MACH_STINGRAY}             += board-stingray-sensors.o
obj-${CONFIG_MACH_STINGRAY}             += board-stingray-wlan_nvs.o
obj-${CONFIG_MACH_STINGRAY}             += board-stingray-touch.o
obj-${CONFIG_MACH_STINGRAY}             += board-stingray-power.o
obj-${CONFIG_MACH_STINGRAY}             += board-stingray-bluetooth.o
obj-${CONFIG_MACH_STINGRAY}             += board-stingray-gps.o
obj-${CONFIG_MACH_STINGRAY}             += board-stingray-bootinfo.o
obj-${CONFIG_MACH_STINGRAY}             += board-stingray-memory.o

obj-${CONFIG_MACH_TRIMSLICE}            += board-trimslice.o
obj-${CONFIG_MACH_TRIMSLICE}            += board-trimslice-pinmux.o