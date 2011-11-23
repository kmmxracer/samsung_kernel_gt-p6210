/*
 * Driver for SR200PC20 from Samsung Electronics
 *
 * Copyright (c) 2011, Samsung Electronics. All rights reserved
 * Author: dongseong.lim
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 */

#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/version.h>
#include <linux/vmalloc.h>
#include <media/v4l2-device.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-i2c-drv.h>
#ifdef CONFIG_VIDEO_SAMSUNG_V4L2
#include <linux/videodev2_samsung.h>
#endif
#include <media/sr200pc20_platform.h>
#ifdef CONFIG_VIDEO_SR200PC20_P2
#include "sr200pc20-p2.h"
#elif defined(CONFIG_VIDEO_SR200PC20_P4W)
#include "sr200pc20-p4w.h"
#else
#include "sr200pc20.h"
#endif

static const struct sr200pc20_fps sr200pc20_framerates[] = {
	{ I_FPS_0,	FRAME_RATE_AUTO },
	{ I_FPS_7,	FRAME_RATE_7 },
	{ I_FPS_10,	10 },
	{ I_FPS_12,	12 },
	{ I_FPS_15,	FRAME_RATE_15 },
	{ I_FPS_25,	FRAME_RATE_25 },
};

static const struct sr200pc20_regs reg_datas = {
	.ev = {
		SR200PC20_REGSET(GET_EV_INDEX(EV_MINUS_4),
					front_ev_minus_4_regs),
		SR200PC20_REGSET(GET_EV_INDEX(EV_MINUS_3),
					front_ev_minus_3_regs),
		SR200PC20_REGSET(GET_EV_INDEX(EV_MINUS_2),
					front_ev_minus_2_regs),
		SR200PC20_REGSET(GET_EV_INDEX(EV_MINUS_1),
					front_ev_minus_1_regs),
		SR200PC20_REGSET(GET_EV_INDEX(EV_DEFAULT),
					front_ev_default_regs),
		SR200PC20_REGSET(GET_EV_INDEX(EV_PLUS_1), front_ev_plus_1_regs),
		SR200PC20_REGSET(GET_EV_INDEX(EV_PLUS_2), front_ev_plus_2_regs),
		SR200PC20_REGSET(GET_EV_INDEX(EV_PLUS_3), front_ev_plus_3_regs),
		SR200PC20_REGSET(GET_EV_INDEX(EV_PLUS_4), front_ev_plus_4_regs),
	},
	.blur = {
		SR200PC20_REGSET(BLUR_LEVEL_0, front_vt_pretty_default),
		SR200PC20_REGSET(BLUR_LEVEL_1, front_vt_pretty_1),
		SR200PC20_REGSET(BLUR_LEVEL_2, front_vt_pretty_2),
		SR200PC20_REGSET(BLUR_LEVEL_3, front_vt_pretty_3),
	},
	.fps = {
		SR200PC20_REGSET(I_FPS_0, front_fps_auto_regs),
		SR200PC20_REGSET(I_FPS_7, front_fps_7_regs),
		SR200PC20_REGSET(I_FPS_10, front_fps_10_regs),
		SR200PC20_REGSET(I_FPS_15, front_fps_15_regs),
		SR200PC20_REGSET(I_FPS_25, front_fps_24_regs),
	},
	.preview_start = SR200PC20_REGSET_TABLE(front_preview_camera_regs),
	.capture_start = SR200PC20_REGSET_TABLE(front_snapshot_normal_regs),
	.init = SR200PC20_REGSET_TABLE(front_init_regs),
	.init_vt = SR200PC20_REGSET_TABLE(front_init_vt_regs),
	.init_recording = SR200PC20_REGSET_TABLE(front_init_recording_regs),
	.dtp_on = SR200PC20_REGSET_TABLE(front_pattern_on_regs),
	.dtp_off = SR200PC20_REGSET_TABLE(front_pattern_off_regs),
};

#ifdef CONFIG_LOAD_FILE
static int loadFile(void)
{
	struct file *fp = NULL;
	struct test *nextBuf = NULL;

	u8 *nBuf = NULL;
	size_t file_size = 0, max_size = 0, testBuf_size = 0;
	ssize_t nread = 0;
	s32 check = 0, starCheck = 0;
	s32 tmp_large_file = 0;
	s32 i = 0;
	int ret = 0;
	loff_t pos;

	mm_segment_t fs = get_fs();
	set_fs(get_ds());

	BUG_ON(testBuf);

	fp = filp_open(TUNING_FILE_PATH, O_RDONLY, 0);
	if (IS_ERR(fp)) {
		cam_err("file open error\n");
		return PTR_ERR(fp);
	}

	file_size = (size_t) fp->f_path.dentry->d_inode->i_size;
	max_size = file_size;

	cam_dbg("file_size = %d\n", file_size);

	nBuf = kmalloc(file_size, GFP_ATOMIC);
	if (nBuf == NULL) {
		cam_dbg("Fail to 1st get memory\n");
		nBuf = vmalloc(file_size);
		if (nBuf == NULL) {
			cam_err("ERR: nBuf Out of Memory\n");
			ret = -ENOMEM;
			goto error_out;
		}
		tmp_large_file = 1;
	}

	testBuf_size = sizeof(struct test) * file_size;
	if (tmp_large_file) {
		testBuf = (struct test *)vmalloc(testBuf_size);
		large_file = 1;
	} else {
		testBuf = kmalloc(testBuf_size, GFP_ATOMIC);
		if (testBuf == NULL) {
			cam_dbg("Fail to get mem(%d bytes)\n", testBuf_size);
			testBuf = (struct test *)vmalloc(testBuf_size);
			large_file = 1;
		}
	}
	if (testBuf == NULL) {
		cam_err("ERR: Out of Memory\n");
		ret = -ENOMEM;
		goto error_out;
	}

	pos = 0;
	memset(nBuf, 0, file_size);
	memset(testBuf, 0, file_size * sizeof(struct test));

	nread = vfs_read(fp, (char __user *)nBuf, file_size, &pos);
	if (nread != file_size) {
		cam_err("failed to read file ret = %d\n", nread);
		ret = -1;
		goto error_out;
	}

	set_fs(fs);

	i = max_size;

	printk("i = %d\n", i);

	while (i) {
		testBuf[max_size - i].data = *nBuf;
		if (i != 1) {
			testBuf[max_size - i].nextBuf = &testBuf[max_size - i + 1];
		} else {
			testBuf[max_size - i].nextBuf = NULL;
			break;
		}
		i--;
		nBuf++;
	}

	i = max_size;
	nextBuf = &testBuf[0];

#if 1
	while (i - 1) {
		if (!check && !starCheck) {
			if (testBuf[max_size - i].data == '/') {
				if (testBuf[max_size-i].nextBuf != NULL) {
					if (testBuf[max_size-i].nextBuf->data
								== '/') {
						check = 1;/* when find '//' */
						i--;
					} else if (testBuf[max_size-i].nextBuf->data == '*') {
						starCheck = 1;/* when find '/ *' */
						i--;
					}
				} else
					break;
			}
			if (!check && !starCheck) {
				/* ignore '\t' */
				if (testBuf[max_size - i].data != '\t') {
					nextBuf->nextBuf = &testBuf[max_size-i];
					nextBuf = &testBuf[max_size - i];
				}
			}
		} else if (check && !starCheck) {
			if (testBuf[max_size - i].data == '/') {
				if(testBuf[max_size-i].nextBuf != NULL) {
					if (testBuf[max_size-i].nextBuf->data == '*') {
						starCheck = 1; /* when find '/ *' */
						check = 0;
						i--;
					}
				} else
					break;
			}

			 /* when find '\n' */
			if (testBuf[max_size - i].data == '\n' && check) {
				check = 0;
				nextBuf->nextBuf = &testBuf[max_size - i];
				nextBuf = &testBuf[max_size - i];
			}

		} else if (!check && starCheck) {
			if (testBuf[max_size - i].data == '*') {
				if (testBuf[max_size-i].nextBuf != NULL) {
					if (testBuf[max_size-i].nextBuf->data == '/') {
						starCheck = 0; /* when find '* /' */
						i--;
					}
				} else
					break;
			}
		}

		i--;

		if (i < 2) {
			nextBuf = NULL;
			break;
		}

		if (testBuf[max_size - i].nextBuf == NULL) {
			nextBuf = NULL;
			break;
		}
	}
#endif

#if 0 // for print
	printk("i = %d\n", i);
	nextBuf = &testBuf[0];
	while (1) {
		//printk("sdfdsf\n");
		if (nextBuf->nextBuf == NULL)
			break;
		printk("%c", nextBuf->data);
		nextBuf = nextBuf->nextBuf;
	}
#endif

error_out:

	if (nBuf)
		tmp_large_file ? vfree(nBuf) : kfree(nBuf);
	if (fp)
		filp_close(fp, current->files);
	return ret;
}
#endif

static int sr200pc20_i2c_read_byte(struct i2c_client *client,
                                     unsigned short subaddr,
                                     unsigned short *data)
{
	unsigned char buf[2] = {0,};
	struct i2c_msg msg = {client->addr, 0, 1, buf};
	int err = 0;

	if(!client->adapter) {
		cam_err("ERROR! can't search i2c client adapter\n");
		return -EIO;
	}

	buf[0] = (unsigned char)subaddr;

	err = i2c_transfer(client->adapter, &msg, 1);
	if(err < 0) {
		cam_err(" ERROR! %d register read failed\n",subaddr);
		return -EIO;
	}

	msg.flags = I2C_M_RD;
	msg.len = 1;

	err = i2c_transfer(client->adapter, &msg, 1);
	if(err < 0) {
		cam_err(" ERROR! %d register read failed\n",subaddr);
		return -EIO;
	}

	*data = (unsigned short)buf[0];

	return 0;
}

static int sr200pc20_i2c_write_byte(struct i2c_client *client,
                                     unsigned short subaddr,
                                     unsigned short data)
{
	unsigned char buf[2] = {0,};
	struct i2c_msg msg = {client->addr, 0, 2, buf};
	int err = 0;

	if(!client->adapter) {
		cam_err(" ERROR! can't search i2c client adapter\n");
		return -EIO;
	}

	buf[0] = subaddr & 0xFF;
	buf[1] = data & 0xFF;

	err = i2c_transfer(client->adapter, &msg, 1);

	return (err == 1)? 0 : -EIO;
}

static int sr200pc20_i2c_read_word(struct i2c_client *client,
                                     unsigned short subaddr,
                                     unsigned short *data)
{
	unsigned char buf[4];
	struct i2c_msg msg = {client->addr, 0, 2, buf};
	int err = 0;

	if(!client->adapter) {
		cam_err(" ERROR! can't search i2c client adapter\n");
		return -EIO;
	}

	buf[0] = subaddr>> 8;
	buf[1] = subaddr & 0xff;

	err = i2c_transfer(client->adapter, &msg, 1);
	if(err < 0) {
		cam_err(" ERROR! %d register read failed\n", subaddr);
		return -EIO;
	}

	msg.flags = I2C_M_RD;
	msg.len = 2;

	err = i2c_transfer(client->adapter, &msg, 1);
	if(err < 0) {
		cam_err(" ERROR! %d register read failed\n", subaddr);
		return -EIO;
	}

	*data = ((buf[0] << 8) | buf[1]);

	return 0;
}

static int sr200pc20_i2c_write_word(struct i2c_client *client,
                                     unsigned short subaddr,
                                     unsigned short data)
{
	unsigned char buf[4];
	struct i2c_msg msg = {client->addr, 0, 4, buf};
	int err = 0;

	if(!client->adapter) {
		cam_err(" ERROR! can't search i2c client adapter\n");
		return -EIO;
	}

	buf[0] = subaddr >> 8;
	buf[1] = subaddr & 0xFF;
	buf[2] = data >> 8;
	buf[3] = data & 0xFF;

	err = i2c_transfer(client->adapter, &msg, 1);

	return (err == 1)? 0 : -EIO;
}

static int sr200pc20_i2c_set_data_burst(struct v4l2_subdev *sd,
					regs_short_t reg_buffer[],
					u32 num_of_regs)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	u16 subaddr, data_value;
	int i, err = 0;

	for(i = 0; i < num_of_regs; i++) {
		subaddr = reg_buffer[i].subaddr;
		data_value = reg_buffer[i].value;

		switch(subaddr) {
		case DELAY_SEQ:
			debug_msleep(data_value * 10);
			break;
		default:
			err = sr200pc20_i2c_write_byte(client, subaddr, data_value);
			if(err < 0) {
				cam_err("i2c write fail\n");
				return -EIO;
			}
			break;
		}
	}

	return 0;
}

#ifdef CONFIG_LOAD_FILE
static int sr200pc20_write_regs_from_sd(struct v4l2_subdev *sd, u8 s_name[])
{

	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct test *tempData = NULL;

	int ret = -EAGAIN;
	regs_short_t temp;
	u32 delay = 0;
	u8 data[11];
	s32 searched = 0, pair_cnt = 0, brace_cnt = 0;
	size_t size = strlen(s_name);
	s32 i;

	cam_trace("E size = %d, string = %s\n", size, s_name);
	tempData = &testBuf[0];
	while (!searched) {
		searched = 1;
		for (i = 0; i < size; i++) {
			if (tempData->data != s_name[i]) {
				searched = 0;
				break;
			}
			tempData = tempData->nextBuf;
		}
		tempData = tempData->nextBuf;
	}
	/* structure is get..*/

	while (1) {
		if (tempData->data == '{') {
			dbg_setfile("%s: found big_brace start\n", __func__);
			tempData = tempData->nextBuf;
			break;
		} else
			tempData = tempData->nextBuf;
	}

	while (1) {
		while (1) {
			if (tempData->data == '{') {
				/* dbg_setfile("%s: found small_brace start\n", __func__); */
				tempData = tempData->nextBuf;
				break;
			} else if (tempData->data == '}') {
				dbg_setfile("%s: found big_brace end\n", __func__);
				return 0;
			} else
				tempData = tempData->nextBuf;
		}
		
		searched = 0;
		pair_cnt = 0;
		while (1) {
			if (tempData->data == 'x') {
				/* get 10 strings.*/
				data[0] = '0';
				for (i = 1; i < 4; i++) {
					data[i] = tempData->data;
					tempData = tempData->nextBuf;
				}
				data[i] = '\0';
				/* dbg_setfile("read HEX: %s\n", data); */
				if (pair_cnt == 0) {
					temp.subaddr = simple_strtoul(data, NULL, 16);
					pair_cnt++;
				} else if (pair_cnt == 1) {
					temp.value = simple_strtoul(data, NULL, 16);
					pair_cnt++;
				}
			} else if (tempData->data == '}') {
				/* dbg_setfile("%s: found small_brace end\n", __func__); */
				tempData = tempData->nextBuf;
				/* searched = 1; */
				break;
			} else
				tempData = tempData->nextBuf;

			if (tempData->nextBuf == NULL)
				return -1;
		}

		if (searched)
			break;

		if ((temp.subaddr & 0xFF) == 0xFF) {
			delay = (temp.value & 0xFF) * 10;
			debug_msleep(delay);
			continue;
		}

		/* cam_err("Write: 0x%02X, 0x%02X\n",
				(u8)(temp.subaddr), (u8)(temp.value)); */
		ret = sr200pc20_i2c_write_byte(client, temp.subaddr, temp.value);

		/* In error circumstances */
		/* Give second shot */
		if (unlikely(ret)) {
			dev_info(&client->dev,
					"sr200pc20 i2c retry one more time\n");
			ret = sr200pc20_i2c_write_byte(client, temp.subaddr, temp.value);

			/* Give it one more shot */
			if (unlikely(ret)) {
				dev_info(&client->dev,
						"sr200pc20 i2c retry twice\n");
				ret = sr200pc20_i2c_write_byte(client, temp.subaddr, temp.value);
			}
		}
	}

	return ret;
}
#endif

static int sr200pc20_set_from_table(struct v4l2_subdev *sd,
				const char *setting_name,
				const struct sr200pc20_regset_table *table,
				int table_size, int index)
{
	int err = 0;

	cam_dbg("%s: set %s index %d\n",
		__func__, setting_name, index);
	if ((index < 0) || (index >= table_size)) {
		cam_err("%s: ERROR, index(%d) out of range[0:%d]"
			"for table for %s\n", __func__, index,
			table_size, setting_name);
		return -EINVAL;
	}

	table += index;
	if (unlikely(!table->reg)) {
		cam_err("%s: ERROR, reg = NULL\n", __func__);
		return -EFAULT;
	}

#ifdef CONFIG_LOAD_FILE
	cam_dbg("%s: \"%s\", reg_name=%s\n", __func__, setting_name,
						table->name);
	return sr200pc20_write_regs_from_sd(sd, table->name);
#else
	err = sr200pc20_i2c_set_data_burst(sd, table->reg, table->array_size);
	if (unlikely(err < 0)) {
		cam_err("%s: ERROR, fail to write regs(%s), err=%d\n",
				__func__, setting_name, err);
		return -EIO;
	}

	return 0;
#endif
}

static int sr200pc20_get_exif(struct v4l2_subdev *sd)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct sr200pc20_state *state = to_state(sd);
	u16 iso_gain_table[] = {10, 18, 23, 28};
	u16 iso_table[] = {0, 50, 100, 200, 400};
	u16 gain = 0, val = 0;
	s32 index = 0;
	u16 read_value1 = 0, read_value2 = 0, read_value3 = 0;
	u32 exp_time;

	state->exif.exp_time_den = 0;
	state->exif.iso = 0;

	/* Get exposure-time */
	sr200pc20_i2c_write_byte(client, 0x03, 0x20);
	sr200pc20_i2c_read_byte(client, 0x80, &read_value1);
	sr200pc20_i2c_read_byte(client, 0x81, &read_value2);
	sr200pc20_i2c_read_byte(client, 0x82, &read_value3);

	exp_time = ((read_value1 << 19) + (read_value2 << 11) + \
					(read_value3 << 3)) / 24;
	state->exif.exp_time_den = 1000 / exp_time;
	cam_dbg("exposure time=%dms\n", exp_time);

	/* Get ISO */
	/*
	val = 0;
	sr200pc20_read_reg(sd, REG_PAGE_ISO, REG_ADDR_ISO, &val);
	cam_dbg("val = %d\n", val);
	gain = val * 10 / 256;
	for (index = 0; index < ARRAY_SIZE(iso_gain_table); index++) {
		if (gain < iso_gain_table[index])
			break;
	}
	state->exif.iso = iso_table[index];*/
	state->exif.iso = 100;

	cam_dbg("%s: gain=%d, exp_time_den=%d, ISO=%d\n",
		__func__, gain, state->exif.exp_time_den, state->exif.iso);
	return 0;
}

#ifdef SUPPORT_FACTORY_TEST
static int sr200pc20_check_dataline(struct v4l2_subdev *sd, s32 val)
{
	struct sr200pc20_state *state = to_state(sd);
	int err = -EIO;

	cam_info("DTP %s\n", val ? "ON" : "OFF");

	if (val)
		err = sr200pc20_set_from_table(sd, "dtp_on",
				&state->regs->dtp_on, 1, 0);
	else
		err = sr200pc20_set_from_table(sd, "dtp_off",
				&state->regs->dtp_off, 1, 0);

	CHECK_ERR_N_MSG(err, "fail to DTP setting\n");
	return 0;
}
#endif

static int sr200pc20_check_sensor_status(struct v4l2_subdev *sd)
{

	/*struct i2c_client *client = v4l2_get_subdevdata(sd);*/
	u16 val_1 = 0, val_2 = 0;
	int err = -EINVAL;

#if 1 /* DSLIM */
	cam_warn("check_sensor_status: WARNING, Not implemented!!\n\n");
	return 0;
#else

	err = sr200pc20_read_reg(sd, 0x7000, 0x0132, &val_1);
	CHECK_ERR(err);
	err = sr200pc20_read_reg(sd, 0xD000, 0x1002, &val_2);
	CHECK_ERR(err);

	cam_dbg("read val1=0x%x, val2=0x%x\n", val_1, val_2);

	if ((val_1 != 0xAAAA) || (val_2 != 0))
		goto error_occur;

	cam_info("Sensor ESD Check: not detected\n");
	return 0;
#endif
error_occur:
	cam_err("%s: ERROR, ESD Shock detected!\n\n", __func__);
	return -ERESTART;
}

static inline int sr200pc20_check_esd(struct v4l2_subdev *sd)
{
	int err = -EINVAL;

	err = sr200pc20_check_sensor_status(sd);
	CHECK_ERR(err);

	return 0;
}

static int sr200pc20_set_preview_start(struct v4l2_subdev *sd)
{
	struct sr200pc20_state *state = to_state(sd);
	int err = -EINVAL;

	cam_info("set_preview_start\n");

	err = sr200pc20_set_from_table(sd, "preview_start",
			&state->regs->preview_start, 1, 0);
#ifdef SUPPORT_FACTORY_TEST
	if (state->check_dataline)
		err = sr200pc20_check_dataline(sd, 1);
#endif
	CHECK_ERR_N_MSG(err, "fail to make preview\n")

	return 0;
}

static int sr200pc20_set_preview_stop(struct v4l2_subdev *sd)
{
	int err = 0;
	cam_info("do nothing.\n");

	return err;
}

static int sr200pc20_set_capture_start(struct v4l2_subdev *sd)
{
	struct sr200pc20_state *state = to_state(sd);
	int err = -EINVAL;

	cam_info("set_capture_start\n");

	err = sr200pc20_set_from_table(sd, "capture_start",
				&state->regs->capture_start, 1, 0);
	CHECK_ERR_N_MSG(err, "failed to make capture\n");

	sr200pc20_get_exif(sd);

	return err;

}

static int sr200pc20_set_sensor_mode(struct v4l2_subdev *sd, s32 val)
{
	struct sr200pc20_state *state = to_state(sd);

	switch (val) {
	case SENSOR_MOVIE:
		if (state->vt_mode) {
			state->sensor_mode = SENSOR_CAMERA;
			cam_warn("%s: WARNING, Not support movie in vt mode\n",
							__func__);
			break;
		}
		/* We do not break. */
	case SENSOR_CAMERA:
		state->sensor_mode = val;
		break;
	default:
		cam_err("%s: ERROR: Not support mode.(%d)\n",
						__func__, val);
		return -EINVAL;
	}

	return 0;
}

static int sr200pc20_g_fmt(struct v4l2_subdev *sd, struct v4l2_format *fmt)
{
	cam_trace("E\n");
	return 0;
}

static int sr200pc20_enum_framesizes(struct v4l2_subdev *sd, \
					struct v4l2_frmsizeenum *fsize)
{
	struct sr200pc20_state *state = to_state(sd);

	cam_trace("E\n");

	/*
	 * Return the actual output settings programmed to the camera
	 */
	if (state->req_fmt.priv == V4L2_PIX_FMT_MODE_CAPTURE) {
		fsize->discrete.width = state->capture_frmsizes.width;
		fsize->discrete.height = state->capture_frmsizes.height;
	} else {
		fsize->discrete.width = state->preview_frmsizes.width;
		fsize->discrete.height = state->preview_frmsizes.height;
	}

	cam_info("enum_framesizes: width - %d , height - %d\n",
		fsize->discrete.width, fsize->discrete.height);

	return 0;
}

static int sr200pc20_try_fmt(struct v4l2_subdev *sd, struct v4l2_format *fmt)
{
	int err = 0;

	cam_trace("E\n");

	return err;
}

static int sr200pc20_s_fmt(struct v4l2_subdev *sd, struct v4l2_format *fmt)
{
	struct sr200pc20_state *state = to_state(sd);
	u32 *width = NULL, *height = NULL;

	cam_trace("E\n");
	/*
	 * Just copying the requested format as of now.
	 * We need to check here what are the formats the camera support, and
	 * set the most appropriate one according to the request from FIMC
	 */
	memcpy(&state->req_fmt, &fmt->fmt.pix, sizeof(fmt->fmt.pix));

	switch (state->req_fmt.priv) {
	case V4L2_PIX_FMT_MODE_PREVIEW:
		width = &state->preview_frmsizes.width;
		height = &state->preview_frmsizes.height;
		break;

	case V4L2_PIX_FMT_MODE_CAPTURE:
		width = &state->capture_frmsizes.width;
		height = &state->capture_frmsizes.height;
		break;

	default:
		cam_err("%s: ERROR, inavlid FMT Mode(%d)\n",
						__func__, state->req_fmt.priv);
		return -EINVAL;
	}

	if ((*width != state->req_fmt.width) ||
		(*height != state->req_fmt.height)) {
		cam_err("%s: ERROR, Invalid size. width= %d, height= %d\n",
			__func__, state->req_fmt.width, state->req_fmt.height);
	}

	return 0;
}

static int sr200pc20_set_frame_rate(struct v4l2_subdev *sd, u32 fps)
{
	struct sr200pc20_state *state = to_state(sd);
	int err = -EIO;
	int i = 0, fps_index = -1;

	cam_info("set frame rate %d\n\n", fps);

	if (state->sensor_mode == SENSOR_MOVIE) {
		cam_warn("%s: WARNING, recording mode supports fixed 25 fps.\n",
						__func__);
		return 0;
	}

	for (i = 0; i < ARRAY_SIZE(sr200pc20_framerates); i++) {
		if (fps == sr200pc20_framerates[i].fps) {
			fps_index = sr200pc20_framerates[i].index;;
			break;
		}
	}

	if (unlikely(fps_index < 0)) {
		cam_err("%s: WARNING, Not supported FPS(%d)\n", __func__, fps);
		return 0;
	}

	err = sr200pc20_set_from_table(sd, "fps",
				state->regs->fps,
				ARRAY_SIZE(state->regs->fps), fps_index);

	CHECK_ERR_N_MSG(err, "fail to set framerate\n")
	return 0;
}

static int sr200pc20_g_parm(struct v4l2_subdev *sd, struct v4l2_streamparm *parms)
{
	int err = 0;

	cam_trace("E\n");

	return err;
}

static int sr200pc20_s_parm(struct v4l2_subdev *sd, struct v4l2_streamparm *parms)
{
	int err = 0;
	u32 fps = 0;
	struct sr200pc20_state *state = to_state(sd);

	if (!state->vt_mode)
		return 0;

	cam_trace("E\n");

	fps = parms->parm.capture.timeperframe.denominator /
			parms->parm.capture.timeperframe.numerator;

	if (fps != state->set_fps) {
		if (fps < 0 && fps > 15) {
			cam_err("%s: ERROR, invalid frame rate %d\n",
						__func__, fps);
			fps = 15;
		}
		state->req_fps = fps;

		if (state->initialized) {
			err = sr200pc20_set_frame_rate(sd, state->req_fps);
			if (err >= 0)
				state->set_fps = state->req_fps;
		}

	}

	return err;
}

static int sr200pc20_control_stream(struct v4l2_subdev *sd, u32 cmd)
{
	struct sr200pc20_state *state = to_state(sd);
	int err = -EINVAL;

	if (unlikely(cmd != STREAM_STOP))
		return 0;

	cam_info("STREAM STOP!!\n");
	err = 0;

	CHECK_ERR_N_MSG(err, "failed to stop stream\n");
#ifndef CONFIG_VIDEO_IMPROVE_STREAMOFF
	debug_msleep(state->pdata->streamoff_delay);
#endif
	return 0;
}

static int sr200pc20_init(struct v4l2_subdev *sd, u32 val)
{
	struct sr200pc20_state *state = to_state(sd);
	int err = -EINVAL;

	cam_trace("E\n");

	/* set initial regster value */
	if (state->sensor_mode == SENSOR_CAMERA) {
		if (!state->vt_mode) {
			cam_info("load camera common setting\n");
			err = sr200pc20_set_from_table(sd, "init",
					&state->regs->init, 1, 0);
		} else {
			cam_info("load camera WIFI VT call setting\n");
			err = sr200pc20_set_from_table(sd, "init_vt",
					&state->regs->init_vt, 1, 0);
			}
	} else {
			cam_info("load recording setting\n");
			err = sr200pc20_set_from_table(sd, "init_recording",
					&state->regs->init_recording, 1, 0);
	}
	CHECK_ERR_N_MSG(err, "failed to init. err=%d\n", err);

	/* We stop stream-output from sensor when starting camera. */
	if (unlikely(state->pdata->is_mipi)) {
		err = sr200pc20_control_stream(sd, STREAM_STOP);
		CHECK_ERR(err);
	}

	if (state->vt_mode && (state->req_fps != state->set_fps)) {
		err = sr200pc20_set_frame_rate(sd, state->req_fps);
		CHECK_ERR(err);
		state->set_fps = state->req_fps;
	}

	state->initialized = 1;

	return 0;
}

/*
 * s_config subdev ops
 * With camera device, we need to re-initialize
 * every single opening time therefor,
 * it is not necessary to be initialized on probe time.
 * except for version checking
 * NOTE: version checking is optional
 */
static int sr200pc20_s_config(struct v4l2_subdev *sd,
		int irq, void *platform_data)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct sr200pc20_state *state = to_state(sd);
#ifdef CONFIG_LOAD_FILE
	int err = 0;
#endif

	cam_trace("E\n");

	state->initialized = 0;
	state->req_fps = state->set_fps = 8;
	state->sensor_mode = SENSOR_CAMERA;
	state->regs = &reg_datas;

	if (!platform_data) {
		cam_err("%s: ERROR, no platform data\n", __func__);
		return -ENODEV;
	}
	state->pdata = platform_data;

	/*
	 * Assign default format and resolution
	 * Use configured default information in platform data
	 * or without them, use default information in driver
	 */
	if (!(state->pdata->default_width && state->pdata->default_height)) {
		state->default_frmsizes.width = DEFAULT_PREVIEW_WIDTH;
		state->default_frmsizes.height = DEFAULT_PREVIEW_HEIGHT;
	} else {
		state->default_frmsizes.width = state->pdata->default_width;
		state->default_frmsizes.height = state->pdata->default_height;
	}

	state->preview_frmsizes.width = state->default_frmsizes.width;
	state->preview_frmsizes.height = state->default_frmsizes.height;
	state->capture_frmsizes.width = DEFAULT_CAPTURE_WIDTH;
	state->capture_frmsizes.height = DEFAULT_CAPTURE_HEIGHT;

	cam_dbg("Default preview_width: %d , preview_height: %d, "
		"capture_width: %d, capture_height: %d",
		state->preview_frmsizes.width, state->preview_frmsizes.height,
		state->capture_frmsizes.width, state->capture_frmsizes.height);

	state->req_fmt.width = state->preview_frmsizes.width;
	state->req_fmt.height = state->preview_frmsizes.height;
	if (!state->pdata->pixelformat)
		state->req_fmt.pixelformat = DEFAULT_FMT;
	else
		state->req_fmt.pixelformat = state->pdata->pixelformat;

#ifdef CONFIG_LOAD_FILE
	err = loadFile();
	CHECK_ERR_N_MSG(err, "failed to load file ERR=%d\n", err)
#endif

	return 0;
}

static int sr200pc20_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct sr200pc20_state *state = to_state(sd);
	int err = 0;

	cam_info("s_stream: mode = %d\n", enable);

	switch (enable) {
	case STREAM_MODE_CAM_OFF:
		if (state->sensor_mode == SENSOR_CAMERA) {
#ifdef SUPPORT_FACTORY_TEST
			if (state->check_dataline)
				err = sr200pc20_check_dataline(sd, 0);
			else
#endif
				if (unlikely(state->pdata->is_mipi))
					err = sr200pc20_control_stream(sd,
							STREAM_STOP);
		}
		break;

	case STREAM_MODE_CAM_ON:
		if ((state->sensor_mode == SENSOR_CAMERA)
		    && (state->req_fmt.priv == V4L2_PIX_FMT_MODE_CAPTURE))
			err = sr200pc20_set_capture_start(sd);
		else
			err = sr200pc20_set_preview_start(sd);
		break;

	case STREAM_MODE_MOVIE_ON:
		cam_dbg("%s: do nothing(movie on)!!\n", __func__);
		break;

	case STREAM_MODE_MOVIE_OFF:
		cam_dbg("%s: do nothing(movie off)!!\n", __func__);
		break;

	default:
		cam_err("%s: ERROR, Invalid stream mode %d\n",
						__func__, enable);
		err = -EINVAL;
		break;
	}

	CHECK_ERR_N_MSG(err, "stream on(off) fail")

	return 0;
}

static int sr200pc20_set_exposure(struct v4l2_subdev *sd, s32 val)
{
	struct sr200pc20_state *state = to_state(sd);
	int err = -EINVAL;

	cam_info("set_exposure: val=%d\n", val);

#ifdef SUPPORT_FACTORY_TEST
	if (state->check_dataline)
		return 0;
#endif
	if ((val < EV_MINUS_4) || (val >= EV_MAX_V4L2)) {
		cam_err("%s: ERROR, invalid value(%d)\n", __func__, val);
		return -EINVAL;
	}

	err = sr200pc20_set_from_table(sd, "ev", state->regs->ev,
		ARRAY_SIZE(state->regs->ev), GET_EV_INDEX(val));
	CHECK_ERR_N_MSG(err, "i2c_write for set brightness\n")

	return 0;
}

static int sr200pc20_set_blur(struct v4l2_subdev *sd, s32 val)
{
	struct sr200pc20_state *state = to_state(sd);
	int err = -EINVAL;

	cam_info("set_blur: val=%d\n", val);

#ifdef SUPPORT_FACTORY_TEST
	if (state->check_dataline)
		return 0;
#endif
	if (unlikely(val < BLUR_LEVEL_0 || val >= BLUR_LEVEL_MAX)) {
		cam_err("%s: ERROR, Invalid blur(%d)\n", __func__, val);
		return -EINVAL;
	}

	err = sr200pc20_set_from_table(sd, "blur", state->regs->blur,
		ARRAY_SIZE(state->regs->blur), val);
	CHECK_ERR_N_MSG(err, "i2c_write for set blur\n")

	return 0;
}

static int sr200pc20_g_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct sr200pc20_state *state = to_state(sd);
	int err = 0;

	cam_dbg("g_ctrl: id = %d\n", ctrl->id - V4L2_CID_PRIVATE_BASE);

	switch (ctrl->id) {
	case V4L2_CID_CAMERA_EXIF_EXPTIME:
		ctrl->value = state->exif.exp_time_den;
		break;
	case V4L2_CID_CAMERA_EXIF_ISO:
		ctrl->value = state->exif.iso;
		break;
	default:
		cam_err("%s: ERROR, no such control id %d\n",
			__func__, ctrl->id - V4L2_CID_PRIVATE_BASE);
		break;
	}

	return err;
}

static int sr200pc20_s_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	/* struct i2c_client *client = v4l2_get_subdevdata(sd); */
	struct sr200pc20_state *state = to_state(sd);
	int err = 0;

	cam_info("s_ctrl: id = %d, value=%d\n",
		ctrl->id - V4L2_CID_PRIVATE_BASE, ctrl->value);

	if ((ctrl->id != V4L2_CID_CAMERA_CHECK_DATALINE)
	    && (ctrl->id != V4L2_CID_CAMERA_SENSOR_MODE)
	    && ((ctrl->id != V4L2_CID_CAMERA_VT_MODE))
	    && (!state->initialized)) {
		cam_warn("%s: WARNING, camera not initialized\n", __func__);
		return 0;
	}

	switch (ctrl->id) {
	case V4L2_CID_CAMERA_BRIGHTNESS:
		err = sr200pc20_set_exposure(sd, ctrl->value);
		cam_dbg("V4L2_CID_CAMERA_BRIGHTNESS [%d]\n", ctrl->value);
		break;

	case V4L2_CID_CAMERA_VGA_BLUR:
		err = sr200pc20_set_blur(sd, ctrl->value);
		cam_dbg("V4L2_CID_CAMERA_VGA_BLUR [%d]\n", ctrl->value);
		break;

	case V4L2_CID_CAMERA_VT_MODE:
		state->vt_mode = ctrl->value;
		break;

	case V4L2_CID_CAMERA_SENSOR_MODE:
		err = sr200pc20_set_sensor_mode(sd, ctrl->value);
		cam_dbg("sensor_mode = %d\n", ctrl->value);
		break;

	case V4L2_CID_CAMERA_CHECK_ESD:
		err = sr200pc20_check_esd(sd);
		break;

#ifdef SUPPORT_FACTORY_TEST
	case V4L2_CID_CAMERA_CHECK_DATALINE:
		state->check_dataline = ctrl->value;
		cam_dbg("check_dataline = %d\n", state->check_dataline);
		err = 0;
		break;
#endif
	default:
		cam_err("%s: ERROR, Not supported ctrl-ID(%d)\n",
			__func__, ctrl->id - V4L2_CID_PRIVATE_BASE);
		/* no errors return.*/
		break;
	}

	cam_trace("X\n");
	return 0;
}

static const struct v4l2_subdev_core_ops sr200pc20_core_ops = {
	.init = sr200pc20_init,		/* initializing API */
	.s_config = sr200pc20_s_config,	/* Fetch platform data */
	.g_ctrl = sr200pc20_g_ctrl,
	.s_ctrl = sr200pc20_s_ctrl,
};

static const struct v4l2_subdev_video_ops sr200pc20_video_ops = {
	/*.s_crystal_freq = sr200pc20_s_crystal_freq,*/
	.g_fmt	= sr200pc20_g_fmt,
	.s_fmt	= sr200pc20_s_fmt,
	.s_stream = sr200pc20_s_stream,
	.enum_framesizes = sr200pc20_enum_framesizes,
	/*.enum_frameintervals = sr200pc20_enum_frameintervals,*/
	/*.enum_fmt = sr200pc20_enum_fmt,*/
	.try_fmt = sr200pc20_try_fmt,
	.g_parm	= sr200pc20_g_parm,
	.s_parm	= sr200pc20_s_parm,
};

static const struct v4l2_subdev_ops sr200pc20_ops = {
	.core = &sr200pc20_core_ops,
	.video = &sr200pc20_video_ops,
};

ssize_t sr200pc20_camera_type_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	char *cam_type = "SILICONFILE_SR200PC20";
	cam_info("%s\n", __func__);

	return sprintf(buf, "%s\n", cam_type);
}

static DEVICE_ATTR(camera_type, S_IRUGO, sr200pc20_camera_type_show, NULL);

/*
 * sr200pc20_probe
 * Fetching platform data is being done with s_config subdev call.
 * In probe routine, we just register subdev device
 */
static int sr200pc20_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	struct sr200pc20_state *state = NULL;
	struct v4l2_subdev *sd = NULL;

	cam_trace("E\n");

	state = kzalloc(sizeof(struct sr200pc20_state), GFP_KERNEL);
	if (state == NULL)
		return -ENOMEM;

	sd = &state->sd;
	strcpy(sd->name, SR200PC20_DRIVER_NAME);

	/* Registering subdev */
	v4l2_i2c_subdev_init(sd, client, &sr200pc20_ops);

	if (device_create_file(&client->dev, &dev_attr_camera_type) < 0) {
		cam_warn("%s: WARNING, failed to create device file, %s\n",
			__func__, dev_attr_camera_type.attr.name);
	}

	cam_dbg("%s %s: driver probed!!\n", dev_driver_string(&client->dev),
		dev_name(&client->dev));
	return 0;
}

static int sr200pc20_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct sr200pc20_state *state = to_state(sd);

	cam_trace("E\n");

	state->initialized = 0;

	device_remove_file(&client->dev, &dev_attr_camera_type);
	v4l2_device_unregister_subdev(sd);
	kfree(to_state(sd));

#ifdef CONFIG_LOAD_FILE
	if (testBuf) {
		large_file ? vfree(testBuf) : kfree(testBuf);
		large_file = 0;
		testBuf = NULL;
	}
#endif

	cam_dbg("%s %s: driver removed!!\n", dev_driver_string(&client->dev),
			dev_name(&client->dev));
	return 0;
}

static const struct i2c_device_id sr200pc20_id[] = {
	{ SR200PC20_DRIVER_NAME, 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, sr200pc20_id);

static struct v4l2_i2c_driver_data v4l2_i2c_data = {
	.name = SR200PC20_DRIVER_NAME,
	.probe = sr200pc20_probe,
	.remove = sr200pc20_remove,
	.id_table = sr200pc20_id,
};

MODULE_DESCRIPTION("SR200PC20 ISP driver");
MODULE_AUTHOR("DongSeong Lim<dongseong.lim@samsung.com>");
MODULE_LICENSE("GPL");

