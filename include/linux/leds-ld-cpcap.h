/*
 * Copyright (C) 2009 Motorola, Inc.
 *
 * This program is free dispware; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free dispware Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free dispware
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 * 02111-1307, USA
 */

#ifndef __LED_LD_CPCAP_H__
#define __LED_LD_CPCAP_H__

#ifdef __KERNEL__

#define LD_CPCAP_LED_DRV "cpcap_led_driver"

#define LD_PRIVACY_LED_DEV "privacy-led"
#define LD_NOTIF_LED_DEV "notification-led"

#define LD_MSG_IND_DEV "notification-led"
#define LD_DISP_BUTTON_DEV "button-backlight"
#define LD_KPAD_DEV "keyboard-backlight"
#define LD_AF_LED_DEV "af-led"

#define LD_MSG_IND_ON               0x1
#define LD_MSG_IND_CURRENT          0x2
#define LD_MSG_IND_LO_CURRENT       0x0

#define LD_MSG_IND_CPCAP_MASK       0x03FF
#define LD_MSG_IND_CPCAP_BLINK_ON   0x0400

#define LD_MSG_IND_LOW              0x20
#define LD_MSG_IND_LOW_MED          0x20
#define LD_MSG_IND_MEDIUM           0x30
#define LD_MSG_IND_MED_HIGH         0x40
#define LD_MSG_IND_HIGH             0x50

#define LD_LED_RED                  0x01
#define LD_LED_GREEN                0x02
#define LD_LED_BLUE                 0x04
#define LD_LED_BLINK                0x80

#define LD_DISP_BUTTON_ON           0x1
#define LD_DISP_BUTTON_CURRENT      0xA
#define LD_DISP_BUTTON_DUTY_CYCLE	0x2A0
#define LD_DISP_BUTTON_CPCAP_MASK	0x3FF

#define LD_BLED_CPCAP_DUTY_CYCLE    0x41
#define LD_BLED_CPCAP_MASK          0x3FF
#define LD_BLED_CPCAP_CURRENT       0x6

#define LD_ALT_ADBL_CURRENT         0x4


struct cpcap_led {
	u8 blink_able;
	unsigned short cpcap_register;
	unsigned short cpcap_reg_mask;
	unsigned short cpcap_reg_period;
	unsigned short cpcap_reg_duty_cycle;
	unsigned short cpcap_reg_current;
	char *class_name;
	char *led_regulator;
};

#endif  /* __KERNEL__ */
#endif  /* __LED_LD_CPCAP_H__ */
