/*
 * TI LM3697 Backlight Driver
 *
 * Copyright 2015 Texas Instruments
 *
 * Author: Milo Kim <milo.kim@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/mfd/ti-lmu.h>
#include <linux/mfd/ti-lmu-register.h>
#include <linux/module.h>

#include "ti-lmu-backlight.h"

#define LM3697_BL_MAX_STRINGS		3
#define LM3697_MAX_BRIGHTNESS		2047

static int lm3697_bl_init(struct ti_lmu_bl_chip *chip)
{
	/* Configure ramp selection for each bank */
	return ti_lmu_update_bits(chip->lmu, LM3697_REG_RAMP_CONF,
				  LM3697_RAMP_MASK, LM3697_RAMP_EACH);
}

static int lm3697_bl_enable(struct ti_lmu_bl *lmu_bl, int enable)
{
	return ti_lmu_update_bits(lmu_bl->chip->lmu, LM3697_REG_ENABLE,
				  BIT(lmu_bl->bank_id),
				  enable << lmu_bl->bank_id);
}

static int lm3697_bl_set_brightness(struct ti_lmu_bl *lmu_bl, int brightness)
{
	int ret;
	u8 data;
	u8 reg_lsb[] = { LM3697_REG_BRT_A_LSB, LM3697_REG_BRT_B_LSB, };
	u8 reg_msb[] = { LM3697_REG_BRT_A_MSB, LM3697_REG_BRT_B_MSB, };

	data = brightness & LM3697_BRT_LSB_MASK;
	ret = ti_lmu_update_bits(lmu_bl->chip->lmu, reg_lsb[lmu_bl->bank_id],
				 LM3697_BRT_LSB_MASK, data);
	if (ret)
		return ret;

	data = (brightness >> LM3697_BRT_MSB_SHIFT) & 0xFF;
	return ti_lmu_write_byte(lmu_bl->chip->lmu, reg_msb[lmu_bl->bank_id],
				 data);
}

static int lm3697_bl_set_ctrl_mode(struct ti_lmu_bl *lmu_bl)
{
	int bank_id = lmu_bl->bank_id;

	if (lmu_bl->mode == BL_PWM_BASED)
		return ti_lmu_update_bits(lmu_bl->chip->lmu,
					  LM3697_REG_PWM_CFG,
					  BIT(bank_id), 1 << bank_id);

	return 0;
}

static int lm3697_bl_string_configure(struct ti_lmu_bl *lmu_bl)
{
	struct ti_lmu *lmu = lmu_bl->chip->lmu;
	int bank_id = lmu_bl->bank_id;
	int is_detected = 0;
	int i, ret;

	/* Assign control bank from backlight string configuration */
	for (i = 0; i < LM3697_BL_MAX_STRINGS; i++) {
		if (test_bit(i, &lmu_bl->bl_string)) {
			ret = ti_lmu_update_bits(lmu,
						 LM3697_REG_HVLED_OUTPUT_CFG,
						 BIT(i), bank_id << i);
			if (ret)
				return ret;

			is_detected = 1;
		}
	}

	if (!is_detected) {
		dev_err(lmu_bl->chip->dev, "No backlight string found\n");
		return -EINVAL;
	}

	return 0;
}

static int lm3697_bl_set_current_limit(struct ti_lmu_bl *lmu_bl)
{
	u8 reg[] = { LM3697_REG_IMAX_A, LM3697_REG_IMAX_B, };

	return ti_lmu_write_byte(lmu_bl->chip->lmu, reg[lmu_bl->bank_id],
				 lmu_bl->imax);
}

static int lm3697_bl_set_ramp(struct ti_lmu_bl *lmu_bl)
{
	int ret, index;
	u8 reg;

	index = ti_lmu_backlight_get_ramp_index(lmu_bl, BL_RAMP_UP);
	if (index > 0) {
		if (lmu_bl->bank_id == 0)
			reg = LM3697_REG_BL0_RAMPUP;
		else
			reg = LM3697_REG_BL1_RAMPUP;

		ret = ti_lmu_update_bits(lmu_bl->chip->lmu, reg,
					 LM3697_BL_RAMPUP_MASK,
					 index << LM3697_BL_RAMPUP_SHIFT);
		if (ret)
			return ret;
	}

	index = ti_lmu_backlight_get_ramp_index(lmu_bl, BL_RAMP_DOWN);
	if (index > 0) {
		if (lmu_bl->bank_id == 0)
			reg = LM3697_REG_BL0_RAMPDN;
		else
			reg = LM3697_REG_BL1_RAMPDN;

		ret = ti_lmu_update_bits(lmu_bl->chip->lmu, reg,
					 LM3697_BL_RAMPDN_MASK,
					 index << LM3697_BL_RAMPDN_SHIFT);
		if (ret)
			return ret;
	}

	return 0;
}

static int lm3697_bl_configure(struct ti_lmu_bl *lmu_bl)
{
	int ret;

	ret = lm3697_bl_set_ctrl_mode(lmu_bl);
	if (ret)
		return ret;

	ret = lm3697_bl_string_configure(lmu_bl);
	if (ret)
		return ret;

	ret = lm3697_bl_set_current_limit(lmu_bl);
	if (ret)
		return ret;

	ret = lm3697_bl_set_ramp(lmu_bl);
	if (ret)
		return ret;

	return 0;
}

/* Backlight ramp up/down time. Unit is msec. */
static const int lm3697_ramp_table[] = {
	   2, 250, 500, 1000, 2000, 4000, 8000, 16000,
};

static const struct ti_lmu_bl_ops lm3697_lmu_ops = {
	.init			= lm3697_bl_init,
	.configure		= lm3697_bl_configure,
	.update_brightness	= lm3697_bl_set_brightness,
	.bl_enable		= lm3697_bl_enable,
	.hwmon_notifier_used	= true,
	.max_brightness		= LM3697_MAX_BRIGHTNESS,
	.ramp_table		= lm3697_ramp_table,
	.size_ramp		= ARRAY_SIZE(lm3697_ramp_table),
};

/* LM3697 backlight of_device_id */
TI_LMU_BL_OF_DEVICE(lm3697, "ti,lm3697-backlight");

/* LM3697 backlight platform driver */
TI_LMU_BL_PLATFORM_DRIVER(lm3697, "lm3697-backlight");

MODULE_DESCRIPTION("TI LM3697 Backlight Driver");
MODULE_AUTHOR("Milo Kim");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:lm3697-backlight");
