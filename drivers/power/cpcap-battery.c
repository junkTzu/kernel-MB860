/*
 * Copyright (C) 2007-2009 Motorola, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 * 02111-1307, USA
 */

#include <asm/div64.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/poll.h>
#include <linux/power_supply.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/spi/cpcap.h>
#include <linux/spi/cpcap-regbits.h>
#include <linux/spi/spi.h>
#include <linux/time.h>
#include <linux/miscdevice.h>
#include <linux/debugfs.h>

#define CPCAP_BATT_IRQ_BATTDET 0x01
#define CPCAP_BATT_IRQ_OV      0x02
#define CPCAP_BATT_IRQ_CC_CAL  0x04
#define CPCAP_BATT_IRQ_ADCDONE 0x08
#define CPCAP_BATT_IRQ_MACRO   0x10
#define CPCAP_BATT_IRQ_LOWBPHL 0x20

static int cpcap_batt_ioctl(struct inode *inode,
			    struct file *file,
			    unsigned int cmd,
			    unsigned long arg);
static unsigned int cpcap_batt_poll(struct file *file, poll_table *wait);
static int cpcap_batt_open(struct inode *inode, struct file *file);
static ssize_t cpcap_batt_read(struct file *file, char *buf, size_t count,
			       loff_t *ppos);
static int cpcap_batt_probe(struct platform_device *pdev);
static int cpcap_batt_remove(struct platform_device *pdev);
static int cpcap_batt_resume(struct platform_device *pdev);
#ifdef CONFIG_MOT_CHARGING_DIS
static ssize_t cpcap_battery_show_property(struct device *dev,
					  struct device_attribute *attr,
					  char *buf);
static ssize_t cpcap_battery_store_property(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count);
#endif
struct cpcap_batt_ps {
	struct power_supply batt;
	struct power_supply ac;
	struct power_supply usb;
	struct cpcap_device *cpcap;
	struct cpcap_batt_data batt_state;
	struct cpcap_batt_ac_data ac_state;
	struct cpcap_batt_usb_data usb_state;
	struct cpcap_adc_request req;
	struct mutex lock;
	char irq_status;
	char data_pending;
	wait_queue_head_t wait;
	char async_req_pending;
	unsigned long last_run_time;
	bool no_update;
	bool disable_charging;
};

static const struct file_operations batt_fops = {
	.owner = THIS_MODULE,
	.open = cpcap_batt_open,
	//No longer used...
	//	.ioctl = cpcap_batt_ioctl,
	.read = cpcap_batt_read,
	.poll = cpcap_batt_poll,
};

static struct miscdevice batt_dev = {
	.minor	= MISC_DYNAMIC_MINOR,
	.name	= "cpcap_batt",
	.fops	= &batt_fops,
};

static enum power_supply_property cpcap_batt_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_CHARGE_COUNTER
};

static enum power_supply_property cpcap_batt_ac_props[] =
{
	POWER_SUPPLY_PROP_ONLINE
};

static enum power_supply_property cpcap_batt_usb_props[] =
{
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_MODEL_NAME
};

static struct platform_driver cpcap_batt_driver = {
	.probe		= cpcap_batt_probe,
	.remove		= cpcap_batt_remove,
	.resume		= cpcap_batt_resume,
	.driver		= {
		.name	= "cpcap_battery",
		.owner	= THIS_MODULE,
	},
};

static struct cpcap_batt_ps *cpcap_batt_sply;

void cpcap_batt_irq_hdlr(enum cpcap_irqs irq, void *data)
{
	struct cpcap_batt_ps *sply = data;

	mutex_lock(&sply->lock);

	sply->data_pending = 1;

	switch (irq) {
	case CPCAP_IRQ_LOWBPH:
		sply->irq_status |= CPCAP_BATT_IRQ_LOWBPHL;
		cpcap_irq_unmask(sply->cpcap, irq);
		break;

	case CPCAP_IRQ_BATTDETB:
		sply->irq_status |= CPCAP_BATT_IRQ_BATTDET;
		cpcap_irq_unmask(sply->cpcap, irq);
		break;

	case CPCAP_IRQ_VBUSOV:
		sply->irq_status |=  CPCAP_BATT_IRQ_OV;
		cpcap_irq_unmask(sply->cpcap, irq);
		break;

	case CPCAP_IRQ_CC_CAL:
		sply->irq_status |= CPCAP_BATT_IRQ_CC_CAL;
		cpcap_irq_unmask(sply->cpcap, irq);
		break;

	case CPCAP_IRQ_UC_PRIMACRO_7:
	case CPCAP_IRQ_UC_PRIMACRO_8:
	case CPCAP_IRQ_UC_PRIMACRO_9:
	case CPCAP_IRQ_UC_PRIMACRO_10:
	case CPCAP_IRQ_UC_PRIMACRO_11:
		sply->irq_status |= CPCAP_BATT_IRQ_MACRO;
		break;
	default:
		break;
	}

	mutex_unlock(&sply->lock);

	wake_up_interruptible(&sply->wait);
}

void cpcap_batt_adc_hdlr(struct cpcap_device *cpcap, void *data)
{
	struct cpcap_batt_ps *sply = data;
	mutex_lock(&sply->lock);

	sply->async_req_pending = 0;

	sply->data_pending = 1;

	sply->irq_status |= CPCAP_BATT_IRQ_ADCDONE;

	mutex_unlock(&sply->lock);

	wake_up_interruptible(&sply->wait);
}

static int cpcap_batt_open(struct inode *inode, struct file *file)
{
	file->private_data = cpcap_batt_sply;
	return 0;
}

static unsigned int cpcap_batt_poll(struct file *file, poll_table *wait)
{
	struct cpcap_batt_ps *sply = file->private_data;
	unsigned int ret = 0;

	poll_wait(file, &sply->wait, wait);

	if (sply->data_pending)
		ret = (POLLIN | POLLRDNORM);

	return ret;
}

static ssize_t cpcap_batt_read(struct file *file,
			       char *buf, size_t count, loff_t *ppos)
{
	struct cpcap_batt_ps *sply = file->private_data;
	int ret = -EFBIG;
	unsigned long long temp;

	if (count >= sizeof(char)) {
		mutex_lock(&sply->lock);
		if (!copy_to_user((void *)buf, (void *)&sply->irq_status,
				  sizeof(sply->irq_status)))
			ret = sizeof(sply->irq_status);
		else
			ret = -EFAULT;
		sply->data_pending = 0;
		temp = sched_clock();
		do_div(temp, NSEC_PER_SEC);
		sply->last_run_time = (unsigned long)temp;

		sply->irq_status = 0;
		mutex_unlock(&sply->lock);
	}

	return ret;
}

static int cpcap_batt_ioctl(struct inode *inode,
			    struct file *file,
			    unsigned int cmd,
			    unsigned long arg)
{
	int ret = 0;
	int i;
	struct cpcap_batt_ps *sply = file->private_data;
	struct cpcap_adc_request *req_async = &sply->req;
	struct cpcap_adc_request req;
	struct cpcap_adc_us_request req_us;
	struct spi_device *spi = sply->cpcap->spi;
	struct cpcap_platform_data *data = spi->controller_data;

	switch (cmd) {
	case CPCAP_IOCTL_BATT_DISPLAY_UPDATE:
		if (sply->no_update)
			return 0;
		if (copy_from_user((void *)&sply->batt_state,
				   (void *)arg, sizeof(struct cpcap_batt_data)))
			return -EFAULT;
		power_supply_changed(&sply->batt);

		if (data->batt_changed)
			data->batt_changed(&sply->batt, &sply->batt_state);
		break;

	case CPCAP_IOCTL_BATT_ATOD_ASYNC:
		mutex_lock(&sply->lock);
		if (!sply->async_req_pending) {
			if (copy_from_user((void *)&req_us, (void *)arg,
					   sizeof(struct cpcap_adc_us_request)
					   )) {
				mutex_unlock(&sply->lock);
				return -EFAULT;
			}

			req_async->format = req_us.format;
			req_async->timing = req_us.timing;
			req_async->type = req_us.type;
			req_async->callback = cpcap_batt_adc_hdlr;
			req_async->callback_param = sply;

			ret = cpcap_adc_async_read(sply->cpcap, req_async);
			if (!ret)
				sply->async_req_pending = 1;
		} else {
			ret = -EAGAIN;
		}
		mutex_unlock(&sply->lock);

		break;

	case CPCAP_IOCTL_BATT_ATOD_SYNC:
		if (copy_from_user((void *)&req_us, (void *)arg,
				   sizeof(struct cpcap_adc_us_request)))
			return -EFAULT;

		req.format = req_us.format;
		req.timing = req_us.timing;
		req.type = req_us.type;

		ret = cpcap_adc_sync_read(sply->cpcap, &req);

		if (ret)
			return ret;

		req_us.status = req.status;
		for (i = 0; i < CPCAP_ADC_BANK0_NUM; i++)
			req_us.result[i] = req.result[i];

		if (copy_to_user((void *)arg, (void *)&req_us,
				 sizeof(struct cpcap_adc_us_request)))
			return -EFAULT;
		break;

	case CPCAP_IOCTL_BATT_ATOD_READ:
		req_us.format = req_async->format;
		req_us.timing = req_async->timing;
		req_us.type = req_async->type;
		req_us.status = req_async->status;
		for (i = 0; i < CPCAP_ADC_BANK0_NUM; i++)
			req_us.result[i] = req_async->result[i];

		if (copy_to_user((void *)arg, (void *)&req_us,
				 sizeof(struct cpcap_adc_us_request)))
			return -EFAULT;
		break;

	default:
		return -ENOTTY;
		break;
	}

	return ret;
}

static int cpcap_batt_ac_get_property(struct power_supply *psy,
				      enum power_supply_property psp,
				      union power_supply_propval *val)
{
	int ret = 0;
	struct cpcap_batt_ps *sply = container_of(psy, struct cpcap_batt_ps,
						 ac);

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		if (sply->disable_charging)
			val->intval = 0;
		else
			val->intval = sply->ac_state.online;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static char *cpcap_batt_usb_models[] = {
	"none", "usb", "factory"
};

static int cpcap_batt_usb_get_property(struct power_supply *psy,
				       enum power_supply_property psp,
				       union power_supply_propval *val)
{
	int ret = 0;
	struct cpcap_batt_ps *sply = container_of(psy, struct cpcap_batt_ps,
						 usb);

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = sply->usb_state.online;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		if (sply->disable_charging)
			val->intval = 0;
		else
			val->intval = sply->usb_state.current_now;
		break;
	case POWER_SUPPLY_PROP_MODEL_NAME:
		val->strval = cpcap_batt_usb_models[sply->usb_state.model];
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int cpcap_batt_get_property(struct power_supply *psy,
				   enum power_supply_property psp,
				   union power_supply_propval *val)
{
	int ret = 0;
	struct cpcap_batt_ps *sply = container_of(psy, struct cpcap_batt_ps,
						  batt);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = sply->batt_state.status;
		break;

	case POWER_SUPPLY_PROP_HEALTH:
		val->intval = sply->batt_state.health;
		break;

	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = sply->batt_state.present;
		break;

	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = POWER_SUPPLY_TECHNOLOGY_LION;
		break;

	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = sply->batt_state.batt_capacity_one;
		break;

	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = sply->batt_state.batt_volt;
		break;

	case POWER_SUPPLY_PROP_TEMP:
		val->intval = sply->batt_state.batt_temp;
		break;

	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
		val->intval = sply->batt_state.batt_full_capacity;
		break;

	case POWER_SUPPLY_PROP_CHARGE_COUNTER:
		val->intval = sply->batt_state.batt_capacity_one;
		break;

	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}
#ifdef CONFIG_MOT_CHARGING_DIS
#define CPCAP_BATTERY_ATTR(_name)					\
{									\
	.attr = { .name = #_name, .mode = 0666 },	\
	.show = cpcap_battery_show_property,				\
	.store = cpcap_battery_store_property,				\
}

static struct device_attribute cpcap_battery_attrs[] = {
	CPCAP_BATTERY_ATTR(disable_charging),
};

enum cpcap_supply_property {
	/* Properties of type `int' */
	CPCAP_SUPPLY_DISABLE_CHRG = 0,
};

static int cpcap_battery_create_attrs(struct device *dev)
{
	int i, rc;

	for (i = 0; i < ARRAY_SIZE(cpcap_battery_attrs); i++) {
		rc = device_create_file(dev, &cpcap_battery_attrs[i]);
		if (rc)
			goto mxc_attrs_failed;
	}

	goto succeed;

mxc_attrs_failed:
	while (i--)
		device_remove_file(dev, &cpcap_battery_attrs[i]);

succeed:
	return rc;
}

static void cpcap_battery_destroy_attrs(struct device *dev)
{
    int i;
    for (i = 0; i < ARRAY_SIZE(cpcap_battery_attrs); i++)
	device_remove_file(dev, &cpcap_battery_attrs[i]);
}

static ssize_t cpcap_battery_show_property(struct device *dev,
					  struct device_attribute *attr,
					  char *buf) {
	const ptrdiff_t off = attr - cpcap_battery_attrs;

	if (off == CPCAP_SUPPLY_DISABLE_CHRG)
		return sprintf(buf, "%d\n", cpcap_batt_sply->disable_charging);
	else
		return 0;
}

static ssize_t cpcap_battery_store_property(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned long val = 0;
	ssize_t ret = count;
	const ptrdiff_t off = attr - cpcap_battery_attrs;

	if (off == CPCAP_SUPPLY_DISABLE_CHRG) {
		strict_strtoul(buf, 10, &val);
		cpcap_batt_sply->disable_charging = (val > 0);
		if (cpcap_batt_sply->ac_state.online)
			power_supply_changed(&cpcap_batt_sply->ac);
		if (cpcap_batt_sply->usb_state.online)
			power_supply_changed(&cpcap_batt_sply->usb);
	}
	return ret;
}
#endif
static int cpcap_batt_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct cpcap_batt_ps *sply;

	if (pdev->dev.platform_data == NULL) {
		dev_err(&pdev->dev, "no platform_data\n");
		ret = -EINVAL;
		goto prb_exit;
	}

	sply = kzalloc(sizeof(struct cpcap_batt_ps), GFP_KERNEL);
	if (sply == NULL) {
		ret = -ENOMEM;
		goto prb_exit;
	}

	sply->cpcap = pdev->dev.platform_data;
	mutex_init(&sply->lock);
	init_waitqueue_head(&sply->wait);

	sply->disable_charging = 0;
	sply->batt_state.status	= POWER_SUPPLY_STATUS_UNKNOWN;
	sply->batt_state.health	= POWER_SUPPLY_HEALTH_GOOD;
	sply->batt_state.present = 1;
	sply->batt_state.capacity = 100;	/* Percentage */
	sply->batt_state.batt_volt = 4200000;	/* uV */
	sply->batt_state.batt_temp = 230;	/* tenths of degrees Celsius */
	sply->batt_state.batt_full_capacity = 0;
	sply->batt_state.batt_capacity_one = 100;

	sply->ac_state.online = 0;

	sply->usb_state.online = 0;
	sply->usb_state.current_now = 0;
	sply->usb_state.model = CPCAP_BATT_USB_MODEL_NONE;

	sply->batt.properties = cpcap_batt_props;
	sply->batt.num_properties = ARRAY_SIZE(cpcap_batt_props);
	sply->batt.get_property = cpcap_batt_get_property;
	sply->batt.name = "battery";
	sply->batt.type = POWER_SUPPLY_TYPE_BATTERY;

	sply->ac.properties = cpcap_batt_ac_props;
	sply->ac.num_properties = ARRAY_SIZE(cpcap_batt_ac_props);
	sply->ac.get_property = cpcap_batt_ac_get_property;
	sply->ac.name = "ac";
	sply->ac.type = POWER_SUPPLY_TYPE_MAINS;

	sply->usb.properties = cpcap_batt_usb_props;
	sply->usb.num_properties = ARRAY_SIZE(cpcap_batt_usb_props);
	sply->usb.get_property = cpcap_batt_usb_get_property;
	sply->usb.name = "usb";
	sply->usb.type = POWER_SUPPLY_TYPE_USB;

	sply->no_update = false;

	ret = power_supply_register(&pdev->dev, &sply->ac);
	if (ret)
		goto prb_exit;
	ret = power_supply_register(&pdev->dev, &sply->batt);
	if (ret)
		goto unregac_exit;
	ret = power_supply_register(&pdev->dev, &sply->usb);
	if (ret)
		goto unregbatt_exit;
	platform_set_drvdata(pdev, sply);
#ifdef CONFIG_MOT_CHARGING_DIS
	cpcap_battery_create_attrs(sply->batt.dev);
#endif
	sply->cpcap->battdata = sply;
	cpcap_batt_sply = sply;

	ret = misc_register(&batt_dev);
	if (ret)
		goto unregusb_exit;

	ret = cpcap_irq_register(sply->cpcap, CPCAP_IRQ_VBUSOV,
				 cpcap_batt_irq_hdlr, sply);
	if (ret)
		goto unregmisc_exit;
	ret = cpcap_irq_register(sply->cpcap, CPCAP_IRQ_LOWBPH,
				 cpcap_batt_irq_hdlr, sply);
	if (ret)
		goto unregirq_exit;
	ret = cpcap_irq_register(sply->cpcap, CPCAP_IRQ_BATTDETB,
				 cpcap_batt_irq_hdlr, sply);
	if (ret)
		goto unregirq_exit;
	ret = cpcap_irq_register(sply->cpcap, CPCAP_IRQ_CC_CAL,
				 cpcap_batt_irq_hdlr, sply);
	if (ret)
		goto unregirq_exit;

	ret = cpcap_irq_register(sply->cpcap, CPCAP_IRQ_UC_PRIMACRO_7,
				 cpcap_batt_irq_hdlr, sply);
	cpcap_irq_mask(sply->cpcap, CPCAP_IRQ_UC_PRIMACRO_7);

	if (ret)
		goto unregirq_exit;

	ret = cpcap_irq_register(sply->cpcap, CPCAP_IRQ_UC_PRIMACRO_8,
				 cpcap_batt_irq_hdlr, sply);
	cpcap_irq_mask(sply->cpcap, CPCAP_IRQ_UC_PRIMACRO_8);

	if (ret)
		goto unregirq_exit;

	ret = cpcap_irq_register(sply->cpcap, CPCAP_IRQ_UC_PRIMACRO_9,
				 cpcap_batt_irq_hdlr, sply);
	cpcap_irq_mask(sply->cpcap, CPCAP_IRQ_UC_PRIMACRO_9);

	if (ret)
		goto unregirq_exit;

	ret = cpcap_irq_register(sply->cpcap, CPCAP_IRQ_UC_PRIMACRO_10,
				 cpcap_batt_irq_hdlr, sply);
	cpcap_irq_mask(sply->cpcap, CPCAP_IRQ_UC_PRIMACRO_10);

	if (ret)
		goto unregirq_exit;

	ret = cpcap_irq_register(sply->cpcap, CPCAP_IRQ_UC_PRIMACRO_11,
				 cpcap_batt_irq_hdlr, sply);
	cpcap_irq_mask(sply->cpcap, CPCAP_IRQ_UC_PRIMACRO_11);

	if (ret)
		goto unregirq_exit;

	goto prb_exit;

unregirq_exit:
	cpcap_irq_free(sply->cpcap, CPCAP_IRQ_LOWBPH);
	cpcap_irq_free(sply->cpcap, CPCAP_IRQ_VBUSOV);
	cpcap_irq_free(sply->cpcap, CPCAP_IRQ_BATTDETB);
	cpcap_irq_free(sply->cpcap, CPCAP_IRQ_CC_CAL);
	cpcap_irq_free(sply->cpcap, CPCAP_IRQ_UC_PRIMACRO_7);
	cpcap_irq_free(sply->cpcap, CPCAP_IRQ_UC_PRIMACRO_8);
	cpcap_irq_free(sply->cpcap, CPCAP_IRQ_UC_PRIMACRO_9);
	cpcap_irq_free(sply->cpcap, CPCAP_IRQ_UC_PRIMACRO_10);
	cpcap_irq_free(sply->cpcap, CPCAP_IRQ_UC_PRIMACRO_11);
unregmisc_exit:
#ifdef CONFIG_MOT_CHARGING_DIS
	cpcap_battery_destroy_attrs(sply->batt.dev);
#endif
	misc_deregister(&batt_dev);
unregusb_exit:
	power_supply_unregister(&sply->usb);
unregbatt_exit:
	power_supply_unregister(&sply->batt);
unregac_exit:
	power_supply_unregister(&sply->ac);
prb_exit:
	return ret;
}

static int cpcap_batt_remove(struct platform_device *pdev)
{
	struct cpcap_batt_ps *sply = platform_get_drvdata(pdev);

	power_supply_unregister(&sply->batt);
	power_supply_unregister(&sply->ac);
	power_supply_unregister(&sply->usb);
#ifdef CONFIG_MOT_CHARGING_DIS
	cpcap_battery_destroy_attrs(sply->batt.dev);
#endif
	misc_deregister(&batt_dev);
	cpcap_irq_free(sply->cpcap, CPCAP_IRQ_VBUSOV);
	cpcap_irq_free(sply->cpcap, CPCAP_IRQ_BATTDETB);
	cpcap_irq_free(sply->cpcap, CPCAP_IRQ_CC_CAL);
	cpcap_irq_free(sply->cpcap, CPCAP_IRQ_UC_PRIMACRO_7);
	cpcap_irq_free(sply->cpcap, CPCAP_IRQ_UC_PRIMACRO_8);
	cpcap_irq_free(sply->cpcap, CPCAP_IRQ_UC_PRIMACRO_9);
	cpcap_irq_free(sply->cpcap, CPCAP_IRQ_UC_PRIMACRO_10);
	cpcap_irq_free(sply->cpcap, CPCAP_IRQ_UC_PRIMACRO_11);
	sply->cpcap->battdata = NULL;
	kfree(sply);

	return 0;
}

static int cpcap_batt_resume(struct platform_device *pdev)
{
	struct cpcap_batt_ps *sply = platform_get_drvdata(pdev);
	unsigned long cur_time;
	unsigned long long temp;

	temp = sched_clock();
	do_div(temp, NSEC_PER_SEC);
	cur_time = ((unsigned long)temp);
	if ((cur_time - sply->last_run_time) < 0)
		sply->last_run_time = 0;

	if ((cur_time - sply->last_run_time) > 50) {
		mutex_lock(&sply->lock);
		sply->data_pending = 1;
		sply->irq_status |= CPCAP_BATT_IRQ_MACRO;

		mutex_unlock(&sply->lock);

		wake_up_interruptible(&sply->wait);
	}

	return 0;
}

void cpcap_batt_set_ac_prop(struct cpcap_device *cpcap, int online)
{
	struct cpcap_batt_ps *sply = cpcap->battdata;
	struct spi_device *spi = cpcap->spi;
	struct cpcap_platform_data *data = spi->controller_data;

	if (sply != NULL) {
		sply->ac_state.online = online;
		power_supply_changed(&sply->ac);

		if (data->ac_changed)
			data->ac_changed(&sply->ac, &sply->ac_state);
	}
}
EXPORT_SYMBOL(cpcap_batt_set_ac_prop);

void cpcap_batt_set_usb_prop_online(struct cpcap_device *cpcap, int online,
				    enum cpcap_batt_usb_model model)
{
	struct cpcap_batt_ps *sply = cpcap->battdata;
	struct spi_device *spi = cpcap->spi;
	struct cpcap_platform_data *data = spi->controller_data;

	if (sply != NULL) {
		sply->usb_state.online = online;
		sply->usb_state.model = model;
		power_supply_changed(&sply->usb);

		if (data->usb_changed)
			data->usb_changed(&sply->usb, &sply->usb_state);
	}
}
EXPORT_SYMBOL(cpcap_batt_set_usb_prop_online);

void cpcap_batt_set_usb_prop_curr(struct cpcap_device *cpcap, unsigned int curr)
{
	struct cpcap_batt_ps *sply = cpcap->battdata;
	struct spi_device *spi = cpcap->spi;
	struct cpcap_platform_data *data = spi->controller_data;

	if (sply != NULL) {
		sply->usb_state.current_now = curr;
		power_supply_changed(&sply->usb);

		if (data->usb_changed)
			data->usb_changed(&sply->usb, &sply->usb_state);
	}
}
EXPORT_SYMBOL(cpcap_batt_set_usb_prop_curr);

/*
 * Debugfs interface to test how system works with different values of
 * the battery properties. Once the propety value is set through the
 * debugfs, updtes from the drivers will be discarded.
 */
#ifdef CONFIG_DEBUG_FS

static int cpcap_batt_debug_set(void *prop, u64 val)
{
	int data = (int)val;
	enum power_supply_property psp = (enum power_supply_property)prop;
	struct cpcap_batt_ps *sply = cpcap_batt_sply;
	bool changed = true;
	sply->no_update = true;

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		sply->batt_state.status = data;
		break;

	case POWER_SUPPLY_PROP_HEALTH:
		sply->batt_state.health = data;
		break;

	case POWER_SUPPLY_PROP_PRESENT:
		sply->batt_state.present = data;
		break;

	case POWER_SUPPLY_PROP_CAPACITY:
		sply->batt_state.capacity = data;
		break;

	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		sply->batt_state.batt_volt = data;
		break;

	case POWER_SUPPLY_PROP_TEMP:
		sply->batt_state.batt_temp = data;
		break;

	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
		sply->batt_state.batt_full_capacity = data;
		break;

	case POWER_SUPPLY_PROP_CHARGE_COUNTER:
		sply->batt_state.batt_capacity_one = data;
		break;

	default:
		changed = false;
		break;
	}

	if (changed)
		power_supply_changed(&sply->batt);

	return 0;
}

static int cpcap_batt_debug_get(void *prop, u64 *val)
{
	enum power_supply_property psp = (enum power_supply_property)prop;
	struct cpcap_batt_ps *sply = cpcap_batt_sply;

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		*val = sply->batt_state.status;
		break;

	case POWER_SUPPLY_PROP_HEALTH:
		*val = sply->batt_state.health;
		break;

	case POWER_SUPPLY_PROP_PRESENT:
		*val = sply->batt_state.present;
		break;

	case POWER_SUPPLY_PROP_CAPACITY:
		*val = sply->batt_state.capacity;
		break;

	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		*val = sply->batt_state.batt_volt;
		break;

	case POWER_SUPPLY_PROP_TEMP:
		*val = sply->batt_state.batt_temp;
		break;

	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
		*val = sply->batt_state.batt_full_capacity;
		break;

	case POWER_SUPPLY_PROP_CHARGE_COUNTER:
		*val = sply->batt_state.batt_capacity_one;
		break;

	default:
		break;
	}

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(cpcap_battery_fops, cpcap_batt_debug_get,
			cpcap_batt_debug_set, "%llu\n");

static int __init cpcap_batt_debug_init(void)
{
	struct dentry *dent = debugfs_create_dir("battery", 0);
	int            ret  = 0;

	if (!IS_ERR(dent)) {
		debugfs_create_file("status", 0666, dent,
		  (void *)POWER_SUPPLY_PROP_STATUS, &cpcap_battery_fops);
		debugfs_create_file("health", 0666, dent,
		  (void *)POWER_SUPPLY_PROP_HEALTH, &cpcap_battery_fops);
		debugfs_create_file("present", 0666, dent,
		  (void *)POWER_SUPPLY_PROP_PRESENT, &cpcap_battery_fops);
		debugfs_create_file("voltage", 0666, dent,
		  (void *)POWER_SUPPLY_PROP_VOLTAGE_NOW, &cpcap_battery_fops);
		debugfs_create_file("capacity", 0666, dent,
		  (void *)POWER_SUPPLY_PROP_CAPACITY, &cpcap_battery_fops);
		debugfs_create_file("temp", 0666, dent,
		  (void *)POWER_SUPPLY_PROP_TEMP, &cpcap_battery_fops);
		debugfs_create_file("charge_full_design", 0666, dent,
		  (void *)POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN, &cpcap_battery_fops);
		debugfs_create_file("charge_counter", 0666, dent,
		  (void *)POWER_SUPPLY_PROP_CHARGE_COUNTER, &cpcap_battery_fops);
	} else {
		ret = PTR_ERR(dent);
	}

	return ret;
}

late_initcall(cpcap_batt_debug_init);

#endif /* CONFIG_DEBUG_FS */

static int __init cpcap_batt_init(void)
{
	return platform_driver_register(&cpcap_batt_driver);
}
subsys_initcall(cpcap_batt_init);

static void __exit cpcap_batt_exit(void)
{
	platform_driver_unregister(&cpcap_batt_driver);
}
module_exit(cpcap_batt_exit);

MODULE_ALIAS("platform:cpcap_batt");
MODULE_DESCRIPTION("CPCAP BATTERY driver");
MODULE_AUTHOR("Motorola");
MODULE_LICENSE("GPL");
