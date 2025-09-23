// SPDX-License-Identifier: GPL-3
/*
 * HP OMEN FourZone RGB Keyboard Driver
 *
 * A clean, lightweight Linux kernel driver for HP OMEN laptop RGB keyboard lighting.
 * Provides full control over 4-zone RGB lighting with brightness control.
 *
 * Author: alessandromrc
 * Based on reverse engineering of HP's Windows WMI interface
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/platform_device.h>
#include <linux/acpi.h>
#include <linux/wmi.h>
#include <linux/string.h>

MODULE_AUTHOR("alessandromrc");
MODULE_DESCRIPTION("HP OMEN FourZone RGB Keyboard Lighting Driver");
MODULE_VERSION("1.0");
MODULE_LICENSE("GPL");

#define HPWMI_BIOS_GUID "5FB7F034-2C63-45e9-BE91-3D44E2C707E4"

enum hp_wmi_commandtype
{
	HPWMI_GET_PLATFORM_INFO = 1,
	HPWMI_FOURZONE_COLOR_GET = 2,
	HPWMI_FOURZONE_COLOR_SET = 3,
	HPWMI_STATUS = 4,
	HPWMI_SET_BRIGHTNESS = 5,
	HPWMI_SET_LIGHTBAR_COLORS = 11,
};

enum hp_wmi_command
{
	HPWMI_READ = 0x01,
	HPWMI_WRITE = 0x02,
	HPWMI_FOURZONE = 0x020009, /* Main lighting command */
	HPWMI_GAMING = 0x020008,	 /* Gaming command */
};

struct bios_args
{
	u32 signature;
	u32 command;
	u32 commandtype;
	u32 datasize;
	u8 data[128];
};

struct bios_return
{
	u32 sigpass;
	u32 return_code;
};

enum hp_return_value
{
	HPWMI_RET_WRONG_SIGNATURE = 0x02,
	HPWMI_RET_UNKNOWN_COMMAND = 0x03,
	HPWMI_RET_UNKNOWN_CMDTYPE = 0x04,
	HPWMI_RET_INVALID_PARAMETERS = 0x05,
};

static inline int encode_outsize_for_pvsz(int outsize)
{
	if (outsize > 4096)
		return -EINVAL;
	if (outsize > 1024)
		return 5;
	if (outsize > 128)
		return 4;
	if (outsize > 4)
		return 3;
	if (outsize > 0)
		return 2;
	return 1;
}

static int hp_wmi_perform_query(int query, enum hp_wmi_command command,
																void *buffer, int insize, int outsize)
{
	int mid;
	struct bios_return *bios_return;
	int actual_outsize;
	union acpi_object *obj;
	struct bios_args args = {
			.signature = 0x55434553,
			.command = command,
			.commandtype = query,
			.datasize = insize,
			.data = {0},
	};
	struct acpi_buffer input = {sizeof(struct bios_args), &args};
	struct acpi_buffer output = {ACPI_ALLOCATE_BUFFER, NULL};
	int ret = 0;

	mid = encode_outsize_for_pvsz(outsize);
	if (WARN_ON(mid < 0))
		return mid;
	if (WARN_ON(insize > sizeof(args.data)))
		return -EINVAL;
	memcpy(&args.data[0], buffer, insize);

	wmi_evaluate_method(HPWMI_BIOS_GUID, 0, mid, &input, &output);
	obj = output.pointer;
	if (!obj)
		return -EINVAL;
	if (obj->type != ACPI_TYPE_BUFFER)
	{
		ret = -EINVAL;
		goto out_free;
	}

	bios_return = (struct bios_return *)obj->buffer.pointer;
	ret = bios_return->return_code;
	if (ret)
	{
		if (ret != HPWMI_RET_UNKNOWN_COMMAND &&
				ret != HPWMI_RET_UNKNOWN_CMDTYPE)
			pr_warn("query 0x%x returned error 0x%x\n", query, ret);
		goto out_free;
	}

	if (!outsize)
		goto out_free;

	actual_outsize = min(outsize, (int)(obj->buffer.length - sizeof(*bios_return)));
	memcpy(buffer, obj->buffer.pointer + sizeof(*bios_return), actual_outsize);
	memset(buffer + actual_outsize, 0, outsize - actual_outsize);

out_free:
	kfree(obj);
	return ret;
}

#define ZONE_COUNT 4

struct color_platform
{
	u8 blue;
	u8 green;
	u8 red;
} __packed;

struct platform_zone
{
	u8 offset; /* position in returned buffer */
	struct device_attribute *attr;
	struct color_platform colors;
};

/* Global brightness control */
static int global_brightness = 100;											 /* Store brightness as percentage */
static struct platform_zone original_colors[ZONE_COUNT]; /* Store original colors */

static struct device_attribute *zone_dev_attrs;
static struct attribute **zone_attrs;
static struct platform_zone *zone_data;

static struct attribute_group zone_attribute_group = {
		.name = "rgb_zones",
};

static int parse_rgb(const char *buf, struct platform_zone *zone)
{
	unsigned long rgb;
	int ret;
	union color_union
	{
		struct color_platform cp;
		int package;
	} repackager;

	ret = kstrtoul(buf, 16, &rgb);
	if (ret)
		return ret;
	if (rgb > 0xFFFFFF)
		return -EINVAL;

	repackager.package = rgb;
	pr_debug("hp-wmi: r:%d g:%d b:%d\n",
					 repackager.cp.red, repackager.cp.green, repackager.cp.blue);
	zone->colors = repackager.cp;
	return 0;
}

static struct platform_zone *match_zone(struct device_attribute *attr)
{
	u8 zone;
	for (zone = 0; zone < ZONE_COUNT; zone++)
	{
		if ((struct device_attribute *)zone_data[zone].attr == attr)
			return &zone_data[zone];
	}
	return NULL;
}

static int fourzone_update_led(struct platform_zone *zone, enum hp_wmi_command rw)
{
	u8 state[128];
	int ret = hp_wmi_perform_query(HPWMI_FOURZONE_COLOR_GET, HPWMI_FOURZONE,
																 &state, sizeof(state), sizeof(state));
	if (ret)
	{
		pr_warn("fourzone_color_get returned error 0x%x\n", ret);
		return ret <= 0 ? ret : -EINVAL;
	}

	if (rw == HPWMI_WRITE)
	{
		/* Zones start at offset 25 */
		state[zone->offset + 0] = zone->colors.red;
		state[zone->offset + 1] = zone->colors.green;
		state[zone->offset + 2] = zone->colors.blue;

		ret = hp_wmi_perform_query(HPWMI_FOURZONE_COLOR_SET, HPWMI_FOURZONE,
															 &state, sizeof(state), sizeof(state));
		if (ret)
			pr_warn("fourzone_color_set returned error 0x%x\n", ret);
		return ret;
	}
	else
	{
		zone->colors.red = state[zone->offset + 0];
		zone->colors.green = state[zone->offset + 1];
		zone->colors.blue = state[zone->offset + 2];
	}
	return 0;
}

static ssize_t zone_show(struct device *dev, struct device_attribute *attr,
												 char *buf)
{
	struct platform_zone *target_zone = match_zone(attr);
	int ret;
	if (target_zone == NULL)
		return sprintf(buf, "red: -1, green: -1, blue: -1\n");
	ret = fourzone_update_led(target_zone, HPWMI_READ);
	if (ret)
		return sprintf(buf, "red: -1, green: -1, blue: -1\n");
	return sprintf(buf, "red: %d, green: %d, blue: %d\n",
								 target_zone->colors.red,
								 target_zone->colors.green, target_zone->colors.blue);
}

static ssize_t zone_set(struct device *dev, struct device_attribute *attr,
												const char *buf, size_t count)
{
	struct platform_zone *target_zone = match_zone(attr);
	int ret;
	if (target_zone == NULL)
	{
		pr_err("hp-wmi: invalid target zone\n");
		return 1;
	}
	ret = parse_rgb(buf, target_zone);
	if (ret)
		return ret;

	/* Store the new color as the original color */
	int zone_idx = target_zone - zone_data;
	original_colors[zone_idx].colors.red = target_zone->colors.red;
	original_colors[zone_idx].colors.green = target_zone->colors.green;
	original_colors[zone_idx].colors.blue = target_zone->colors.blue;

	/* Apply current brightness to the new color */
	target_zone->colors.red = (target_zone->colors.red * global_brightness) / 100;
	target_zone->colors.green = (target_zone->colors.green * global_brightness) / 100;
	target_zone->colors.blue = (target_zone->colors.blue * global_brightness) / 100;

	ret = fourzone_update_led(target_zone, HPWMI_WRITE);
	return ret ? ret : count;
}

/* Brightness control - scales all zone colors */

static ssize_t brightness_show(struct device *dev, struct device_attribute *attr,
															 char *buf)
{
	return sprintf(buf, "%d\n", global_brightness);
}

static ssize_t brightness_set(struct device *dev, struct device_attribute *attr,
															const char *buf, size_t count)
{
	unsigned long level;
	int ret;

	if (kstrtoul(buf, 10, &level))
		return -EINVAL;
	if (level > 100)
		level = 100;

	global_brightness = level;

	/* Read current colors from hardware and apply brightness scaling */
	for (int zone = 0; zone < ZONE_COUNT; zone++)
	{
		/* Read current colors from hardware */
		ret = fourzone_update_led(&zone_data[zone], HPWMI_READ);
		if (ret)
			return ret;

		/* Store the original colors */
		original_colors[zone].colors.red = zone_data[zone].colors.red;
		original_colors[zone].colors.green = zone_data[zone].colors.green;
		original_colors[zone].colors.blue = zone_data[zone].colors.blue;

		/* Scale the colors by brightness */
		zone_data[zone].colors.red = (zone_data[zone].colors.red * level) / 100;
		zone_data[zone].colors.green = (zone_data[zone].colors.green * level) / 100;
		zone_data[zone].colors.blue = (zone_data[zone].colors.blue * level) / 100;

		ret = fourzone_update_led(&zone_data[zone], HPWMI_WRITE);
		if (ret)
			return ret;
	}

	return count;
}

static DEVICE_ATTR(brightness, 0644, brightness_show, brightness_set);

static ssize_t all_show(struct device *dev, struct device_attribute *attr,
												char *buf)
{
	int ret;
	ret = fourzone_update_led(&zone_data[0], HPWMI_READ);
	if (ret)
		return sprintf(buf, "red: -1, green: -1, blue: -1\n");
	return sprintf(buf, "red: %d, green: %d, blue: %d\n",
								 zone_data[0].colors.red,
								 zone_data[0].colors.green, zone_data[0].colors.blue);
}

static ssize_t all_set(struct device *dev, struct device_attribute *attr,
											 const char *buf, size_t count)
{
	struct platform_zone temp;
	int ret;
	u8 z;

	ret = parse_rgb(buf, &temp);
	if (ret)
		return ret;

	for (z = 0; z < ZONE_COUNT; z++)
	{
		/* Store the new color as the original color */
		original_colors[z].colors = temp.colors;

		/* Apply current brightness to the new color */
		zone_data[z].colors.red = (temp.colors.red * global_brightness) / 100;
		zone_data[z].colors.green = (temp.colors.green * global_brightness) / 100;
		zone_data[z].colors.blue = (temp.colors.blue * global_brightness) / 100;

		ret = fourzone_update_led(&zone_data[z], HPWMI_WRITE);
		if (ret)
			return ret;
	}

	return count;
}

static int fourzone_setup(struct platform_device *dev)
{
	u8 zone;
	char buffer[10];
	char *name;

	zone_dev_attrs = kcalloc(ZONE_COUNT + 2, sizeof(struct device_attribute),
													 GFP_KERNEL);
	if (!zone_dev_attrs)
		return -ENOMEM;

	zone_attrs = kcalloc(ZONE_COUNT + 2, sizeof(struct attribute *),
											 GFP_KERNEL);
	if (!zone_attrs)
		return -ENOMEM;

	zone_data = kcalloc(ZONE_COUNT, sizeof(struct platform_zone),
											GFP_KERNEL);
	if (!zone_data)
		return -ENOMEM;

	for (u8 zone = 0; zone < ZONE_COUNT; zone++)
	{
		zone_data[zone].offset = 25 + (zone * 3);
		int ret = fourzone_update_led(&zone_data[zone], HPWMI_READ);
		if (ret)
			return ret;

		/* Store original colors */
		original_colors[zone].colors.red = zone_data[zone].colors.red;
		original_colors[zone].colors.green = zone_data[zone].colors.green;
		original_colors[zone].colors.blue = zone_data[zone].colors.blue;
	}

	for (zone = 0; zone < ZONE_COUNT; zone++)
	{
		sprintf(buffer, "zone%02hhX", zone);
		name = kstrdup(buffer, GFP_KERNEL);
		if (!name)
			return -ENOMEM;

		sysfs_attr_init(&zone_dev_attrs[zone].attr);
		zone_dev_attrs[zone].attr.name = name;
		zone_dev_attrs[zone].attr.mode = 0644;
		zone_dev_attrs[zone].show = zone_show;
		zone_dev_attrs[zone].store = zone_set;
		zone_data[zone].offset = 25 + (zone * 3);
		zone_attrs[zone] = &zone_dev_attrs[zone].attr;
		zone_data[zone].attr = &zone_dev_attrs[zone];
	}

	sysfs_attr_init(&zone_dev_attrs[ZONE_COUNT].attr);
	zone_dev_attrs[ZONE_COUNT].attr.name = "all";
	zone_dev_attrs[ZONE_COUNT].attr.mode = 0644;
	zone_dev_attrs[ZONE_COUNT].show = all_show;
	zone_dev_attrs[ZONE_COUNT].store = all_set;
	zone_attrs[ZONE_COUNT] = &zone_dev_attrs[ZONE_COUNT].attr;

	zone_attrs[ZONE_COUNT + 1] = &dev_attr_brightness.attr;

	zone_attribute_group.attrs = zone_attrs;
	return sysfs_create_group(&dev->dev.kobj, &zone_attribute_group);
}

static struct platform_device *hp_wmi_platform_dev;

static int __init hp_wmi_bios_setup(struct platform_device *device)
{
	return fourzone_setup(device);
}

static struct platform_driver hp_wmi_driver = {
		.driver = {
				.name = "omen-rgb-keyboard",
		},
		.remove = NULL,
};

static int __init hp_wmi_init(void)
{
	int bios_capable = wmi_has_guid(HPWMI_BIOS_GUID);
	int err;
	if (!bios_capable)
		return -ENODEV;

	hp_wmi_platform_dev = platform_device_register_simple("omen-rgb-keyboard", -1, NULL, 0);
	if (IS_ERR(hp_wmi_platform_dev))
		return PTR_ERR(hp_wmi_platform_dev);

	err = platform_driver_probe(&hp_wmi_driver, hp_wmi_bios_setup);
	if (err)
	{
		platform_device_unregister(hp_wmi_platform_dev);
		return err;
	}
	return 0;
}
module_init(hp_wmi_init);

static void __exit hp_wmi_exit(void)
{
	if (hp_wmi_platform_dev)
	{
		platform_device_unregister(hp_wmi_platform_dev);
		platform_driver_unregister(&hp_wmi_driver);
	}
}
module_exit(hp_wmi_exit);
