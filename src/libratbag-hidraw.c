/*
 * Copyright © 2015 Red Hat, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <libudev.h>
#include <linux/hidraw.h>
#include <string.h>

#include "libratbag-hidraw.h"
#include "libratbag-private.h"

/* defined in include/linux.hid.h in the kernel, but not exported */
#ifndef HID_MAX_BUFFER_SIZE
#define HID_MAX_BUFFER_SIZE	4096		/* 4kb */
#endif

#define HID_REPORT_ID		0b10000100
#define HID_COLLECTION		0b10100000
#define HID_USAGE_PAGE		0b00000100
#define HID_USAGE		0b00001000

#define HID_PHYSICAL		0
#define HID_APPLICATION		1
#define HID_LOGICAL		2

#define HID_KEY_RESERVED			0x00	/* Reserved (no event indicated) */
#define HID_KEY_ERRORROLLOVER			0x01	/* ErrorRollOver */
#define HID_KEY_POSTFAIL			0x02	/* POSTFail */
#define HID_KEY_ERRORUNDEFINE			0x03	/* ErrorUndefine */
#define HID_KEY_A				0x04	/* a and A */
#define HID_KEY_B				0x05	/* b and B */
#define HID_KEY_C				0x06	/* c and C */
#define HID_KEY_D				0x07	/* d and D */
#define HID_KEY_E				0x08	/* e and E */
#define HID_KEY_F				0x09	/* f and F */
#define HID_KEY_G				0x0A	/* g and G */
#define HID_KEY_H				0x0B	/* h and H */
#define HID_KEY_I				0x0C	/* i and I */
#define HID_KEY_J				0x0D	/* j and J */
#define HID_KEY_K				0x0E	/* k and K */
#define HID_KEY_L				0x0F	/* l and L */
#define HID_KEY_M				0x10	/* m and M */
#define HID_KEY_N				0x11	/* n and N */
#define HID_KEY_O				0x12	/* o and O */
#define HID_KEY_P				0x13	/* p and P */
#define HID_KEY_Q				0x14	/* q and Q */
#define HID_KEY_R				0x15	/* r and R */
#define HID_KEY_S				0x16	/* s and S */
#define HID_KEY_T				0x17	/* t and T */
#define HID_KEY_U				0x18	/* u and U */
#define HID_KEY_V				0x19	/* v and V */
#define HID_KEY_W				0x1A	/* w and W */
#define HID_KEY_X				0x1B	/* x and X */
#define HID_KEY_Y				0x1C	/* y and Y */
#define HID_KEY_Z				0x1D	/* z and Z */
#define HID_KEY_1				0x1E	/* 1 and ! */
#define HID_KEY_2				0x1F	/* 2 and @ */
#define HID_KEY_3				0x20	/* 3 and # */
#define HID_KEY_4				0x21	/* 4 and $ */
#define HID_KEY_5				0x22	/* 5 and % */
#define HID_KEY_6				0x23	/* 6 and ^ */
#define HID_KEY_7				0x24	/* 7 and & */
#define HID_KEY_8				0x25	/* 8 and * */
#define HID_KEY_9				0x26	/* 9 and ( */
#define HID_KEY_0				0x27	/* 0 and ) */
#define HID_KEY_RETURN_ENTER			0x28	/* Return (ENTER) */
#define HID_KEY_ESCAPE				0x29	/* ESCAPE */
#define HID_KEY_DELETE_BACKSPACE		0x2A	/* DELETE (Backspace) */
#define HID_KEY_TAB				0x2B	/* Tab */
#define HID_KEY_SPACEBAR			0x2C	/* Spacebar */
#define HID_KEY_MINUS_AND_UNDERSCORE		0x2D	/* - and (underscore) */
#define HID_KEY_EQUAL_AND_PLUS			0x2E	/* = and + */
#define HID_KEY_CLOSE_BRACKET			0x2F	/* [ and { */
#define HID_KEY_OPEN_BRACKET			0x30	/* ] and } */
#define HID_KEY_BACK_SLASH_AND_PIPE		0x31	/* \ and | */
#define HID_KEY_NON_US_HASH_AND_TILDE		0x32	/* Non-US # and ~ */
#define HID_KEY_SEMICOLON_AND_COLON		0x33	/* ; and : */
#define HID_KEY_QUOTE_AND_DOUBLEQUOTE		0x34	/* ' and " */
#define HID_KEY_GRAVE_ACCENT_AND_TILDE		0x35	/* Grave Accent and Tilde */
#define HID_KEY_COMMA_AND_LESSER_THAN		0x36	/* Keyboard, and < */
#define HID_KEY_PERIOD_AND_GREATER_THAN		0x37	/* . and > */
#define HID_KEY_SLASH_AND_QUESTION_MARK		0x38	/* / and ? */
#define HID_KEY_CAPS_LOCK			0x39	/* Caps Lock */
#define HID_KEY_F1				0x3A	/* F1 */
#define HID_KEY_F2				0x3B	/* F2 */
#define HID_KEY_F3				0x3C	/* F3 */
#define HID_KEY_F4				0x3D	/* F4 */
#define HID_KEY_F5				0x3E	/* F5 */
#define HID_KEY_F6				0x3F	/* F6 */
#define HID_KEY_F7				0x40	/* F7 */
#define HID_KEY_F8				0x41	/* F8 */
#define HID_KEY_F9				0x42	/* F9 */
#define HID_KEY_F10				0x43	/* F10 */
#define HID_KEY_F11				0x44	/* F11 */
#define HID_KEY_F12				0x45	/* F12 */
#define HID_KEY_PRINTSCREEN			0x46	/* PrintScreen */
#define HID_KEY_SCROLL_LOCK			0x47	/* Scroll Lock */
#define HID_KEY_PAUSE				0x48	/* Pause */
#define HID_KEY_INSERT				0x49	/* Insert */
#define HID_KEY_HOME				0x4A	/* Home */
#define HID_KEY_PAGEUP				0x4B	/* PageUp */
#define HID_KEY_DELETE_FORWARD			0x4C	/* Delete Forward */
#define HID_KEY_END				0x4D	/* End */
#define HID_KEY_PAGEDOWN			0x4E	/* PageDown */
#define HID_KEY_RIGHTARROW			0x4F	/* RightArrow */
#define HID_KEY_LEFTARROW			0x50	/* LeftArrow */
#define HID_KEY_DOWNARROW			0x51	/* DownArrow */
#define HID_KEY_UPARROW				0x52	/* UpArrow */
#define HID_KEY_KEYPAD_NUM_LOCK_AND_CLEAR	0x53	/* Keypad Num Lock and Clear */
#define HID_KEY_KEYPAD_SLASH			0x54	/* Keypad / */
#define HID_KEY_KEYPAD_ASTERISK			0x55	/* Keypad * */
#define HID_KEY_KEYPAD_MINUS			0x56	/* Keypad - */
#define HID_KEY_KEYPAD_PLUS			0x57	/* Keypad + */
#define HID_KEY_KEYPAD_ENTER			0x58	/* Keypad ENTER */
#define HID_KEY_KEYPAD_1_AND_END		0x59	/* Keypad 1 and End */
#define HID_KEY_KEYPAD_2_AND_DOWN_ARROW		0x5A	/* Keypad 2 and Down Arrow */
#define HID_KEY_KEYPAD_3_AND_PAGEDN		0x5B	/* Keypad 3 and PageDn */
#define HID_KEY_KEYPAD_4_AND_LEFT_ARROW		0x5C	/* Keypad 4 and Left Arrow */
#define HID_KEY_KEYPAD_5			0x5D	/* Keypad 5 */
#define HID_KEY_KEYPAD_6_AND_RIGHT_ARROW	0x5E	/* Keypad 6 and Right Arrow */
#define HID_KEY_KEYPAD_7_AND_HOME		0x5F	/* Keypad 7 and Home */
#define HID_KEY_KEYPAD_8_AND_UP_ARROW		0x60	/* Keypad 8 and Up Arrow */
#define HID_KEY_KEYPAD_9_AND_PAGEUP		0x61	/* Keypad 9 and PageUp */
#define HID_KEY_KEYPAD_0_AND_INSERT		0x62	/* Keypad 0 and Insert */
#define HID_KEY_KEYPAD_PERIOD_AND_DELETE	0x63	/* Keypad . and Delete */
#define HID_KEY_NON_US_BACKSLASH_AND_PIPE	0x64	/* Non-US \ and | */
#define HID_KEY_APPLICATION			0x65	/* Application */
#define HID_KEY_POWER				0x66	/* Power */
#define HID_KEY_KEYPAD_EQUAL			0x67	/* Keypad = */
#define HID_KEY_F13				0x68	/* F13 */
#define HID_KEY_F14				0x69	/* F14 */
#define HID_KEY_F15				0x6A	/* F15 */
#define HID_KEY_F16				0x6B	/* F16 */
#define HID_KEY_F17				0x6C	/* F17 */
#define HID_KEY_F18				0x6D	/* F18 */
#define HID_KEY_F19				0x6E	/* F19 */
#define HID_KEY_F20				0x6F	/* F20 */
#define HID_KEY_F21				0x70	/* F21 */
#define HID_KEY_F22				0x71	/* F22 */
#define HID_KEY_F23				0x72	/* F23 */
#define HID_KEY_F24				0x73	/* F24 */
#define HID_KEY_EXECUTE				0x74	/* Execute */
#define HID_KEY_HELP				0x75	/* Help */
#define HID_KEY_MENU				0x76	/* Menu */
#define HID_KEY_SELECT				0x77	/* Select */
#define HID_KEY_STOP				0x78	/* Stop */
#define HID_KEY_AGAIN				0x79	/* Again */
#define HID_KEY_UNDO				0x7A	/* Undo */
#define HID_KEY_CUT				0x7B	/* Cut */
#define HID_KEY_COPY				0x7C	/* Copy */
#define HID_KEY_PASTE				0x7D	/* Paste */
#define HID_KEY_FIND				0x7E	/* Find */
#define HID_KEY_MUTE				0x7F	/* Mute */
#define HID_KEY_VOLUME_UP			0x80	/* Volume Up */
#define HID_KEY_VOLUME_DOWN			0x81	/* Volume Down */
#define HID_KEY_LOCKING_CAPS_LOCK		0x82	/* Locking Caps Lock */
#define HID_KEY_LOCKING_NUM_LOCK		0x83	/* Locking Num Lock */
#define HID_KEY_LOCKING_SCROLL_LOCK		0x84	/* Locking Scroll Lock */
#define HID_KEY_KEYPAD_COMMA			0x85	/* Keypad Comma */
#define HID_KEY_KEYPAD_EQUAL_SIGN		0x86	/* Keypad Equal Sign */
#define HID_KEY_KANJI1				0x87	/* Kanji1 */
#define HID_KEY_KANJI2				0x88	/* Kanji2 */
#define HID_KEY_KANJI3				0x89	/* Kanji3 */
#define HID_KEY_KANJI4				0x8A	/* Kanji4 */
#define HID_KEY_KANJI5				0x8B	/* Kanji5 */
#define HID_KEY_KANJI6				0x8C	/* Kanji6 */
#define HID_KEY_KANJI7				0x8D	/* Kanji7 */
#define HID_KEY_KANJI8				0x8E	/* Kanji8 */
#define HID_KEY_KANJI9				0x8F	/* Kanji9 */
#define HID_KEY_LANG1				0x90	/* LANG1 */
#define HID_KEY_LANG2				0x91	/* LANG2 */
#define HID_KEY_LANG3				0x92	/* LANG3 */
#define HID_KEY_LANG4				0x93	/* LANG4 */
#define HID_KEY_LANG5				0x94	/* LANG5 */
#define HID_KEY_LANG6				0x95	/* LANG6 */
#define HID_KEY_LANG7				0x96	/* LANG7 */
#define HID_KEY_LANG8				0x97	/* LANG8 */
#define HID_KEY_LANG9				0x98	/* LANG9 */
#define HID_KEY_ALTERNATE_ERASE			0x99	/* Alternate Erase */
#define HID_KEY_SYSREQ_ATTENTION		0x9A	/* SysReq/Attention */
#define HID_KEY_CANCEL				0x9B	/* Cancel */
#define HID_KEY_CLEAR				0x9C	/* Clear */
#define HID_KEY_PRIOR				0x9D	/* Prior */
#define HID_KEY_RETURN				0x9E	/* Return */
#define HID_KEY_SEPARATOR			0x9F	/* Separator */
#define HID_KEY_OUT				0xA0	/* Out */
#define HID_KEY_OPER				0xA1	/* Oper */
#define HID_KEY_CLEAR_AGAIN			0xA2	/* Clear/Again */
#define HID_KEY_CRSEL_PROPS			0xA3	/* CrSel/Props */
#define HID_KEY_EXSEL				0xA4	/* ExSel */
/* RESERVED					0xA5-DF	*/ /* Reserved */
#define HID_KEY_LEFTCONTROL			0xE0	/* LeftControl */
#define HID_KEY_LEFTSHIFT			0xE1	/* LeftShift */
#define HID_KEY_LEFTALT				0xE2	/* LeftAlt */
#define HID_KEY_LEFT_GUI			0xE3	/* Left GUI */
#define HID_KEY_RIGHTCONTROL			0xE4	/* RightControl */
#define HID_KEY_RIGHTSHIFT			0xE5	/* RightShift */
#define HID_KEY_RIGHTALT			0xE6	/* RightAlt */
#define HID_KEY_RIGHT_GUI			0xE7	/* Right GUI */

static int
ratbag_hidraw_parse_report_descriptor(struct ratbag_device *device)
{
	int rc, desc_size = 0;
	struct ratbag_hidraw *hidraw = &device->hidraw;
	struct hidraw_report_descriptor report_desc = {0};
	unsigned int i, j;
	unsigned int usage_page, usage;

	hidraw->num_reports = 0;

	rc = ioctl(hidraw->fd, HIDIOCGRDESCSIZE, &desc_size);
	if (rc < 0)
		return rc;

	report_desc.size = desc_size;
	rc = ioctl(hidraw->fd, HIDIOCGRDESC, &report_desc);
	if (rc < 0)
		return rc;

	i = 0;
	usage_page = 0;
	usage = 0;
	while (i < report_desc.size) {
		uint8_t value = report_desc.value[i];
		uint8_t hid = value & 0xfc;
		uint8_t size = value & 0x3;
		unsigned content = 0;

		if (size == 3)
			size = 4;

		if (i + size >= report_desc.size)
			return -EPROTO;

		for (j = 0; j < size; j++)
			content |= report_desc.value[i + j + 1] << (j * 8);

		switch (hid) {
		case HID_REPORT_ID:
			if (hidraw->reports) {
				log_debug(device->ratbag, "report ID %02x\n", content);
				hidraw->reports[hidraw->num_reports].report_id = content;
				hidraw->reports[hidraw->num_reports].usage_page = usage_page;
				hidraw->reports[hidraw->num_reports].usage = usage;
			}
			hidraw->num_reports++;
			break;
		case HID_COLLECTION:
			if (content == HID_APPLICATION &&
			    hidraw->reports &&
			    !hidraw->num_reports &&
			    !hidraw->reports[0].report_id) {
				hidraw->reports[hidraw->num_reports].usage_page = usage_page;
				hidraw->reports[hidraw->num_reports].usage = usage;
			}
			break;
		case HID_USAGE_PAGE:
			usage_page = content;
			break;
		case HID_USAGE:
			usage = content;
			break;
		}

		i += 1 + size;
	}

	return 0;
}

static int
ratbag_open_hidraw_node(struct ratbag_device *device, struct udev_device *hidraw_udev)
{
	struct hidraw_devinfo info;
	struct ratbag_device *tmp_device;
	int fd, res;
	const char *devnode;
	const char *sysname;
	size_t reports_size;

	device->hidraw.fd = -1;

	sysname = udev_device_get_sysname(hidraw_udev);
	if (!strneq("hidraw", sysname, 6))
		return -ENODEV;

	list_for_each(tmp_device, &device->ratbag->devices, link) {
		if (tmp_device->hidraw.sysname &&
		    streq(tmp_device->hidraw.sysname, sysname)) {
			return -ENODEV;
		}
	}

	devnode = udev_device_get_devnode(hidraw_udev);
	fd = ratbag_open_path(device, devnode, O_RDWR);
	if (fd < 0)
		goto err;

	/* Get Raw Info */
	res = ioctl(fd, HIDIOCGRAWINFO, &info);
	if (res < 0) {
		log_error(device->ratbag,
			  "error while getting info from device");
		goto err;
	}

	/* check basic matching between the hidraw node and the ratbag device */
	if (info.bustype != device->ids.bustype ||
	    (info.vendor & 0xFFFF )!= device->ids.vendor ||
	    (info.product & 0xFFFF) != device->ids.product) {
		errno = ENODEV;
		goto err;
	}

	log_debug(device->ratbag,
		  "%s is device '%s'.\n",
		  device->name,
		  udev_device_get_devnode(hidraw_udev));

	device->hidraw.fd = fd;

	/* parse first to count the number of reports */
	res = ratbag_hidraw_parse_report_descriptor(device);
	if (res) {
		log_error(device->ratbag,
			  "Error while parsing the report descriptor: '%s' (%d)\n",
			  strerror(-res),
			  res);
		device->hidraw.fd = -1;
		goto err;
	}

	if (device->hidraw.num_reports)
		reports_size = device->hidraw.num_reports * sizeof(struct ratbag_hid_report);
	else
		reports_size = sizeof(struct ratbag_hid_report);

	device->hidraw.reports = zalloc(reports_size);
	ratbag_hidraw_parse_report_descriptor(device);

	device->hidraw.sysname = strdup_safe(sysname);
	return 0;

err:
	if (fd >= 0)
		ratbag_close_fd(device, fd);
	return -errno;
}

static int
ratbag_find_hidraw_node(struct ratbag_device *device,
			int (*match)(struct ratbag_device *device),
			int use_usb_parent)
{
	struct ratbag *ratbag = device->ratbag;
	struct udev_enumerate *e;
	struct udev_list_entry *entry;
	const char *path;
	struct udev_device *hid_udev;
	struct udev_device *parent_udev;
	struct udev *udev = ratbag->udev;
	int rc = -ENODEV;
	int matched;

	assert(match);

	hid_udev = udev_device_get_parent_with_subsystem_devtype(device->udev_device, "hid", NULL);

	if (!hid_udev)
		return -ENODEV;

	if (use_usb_parent && device->ids.bustype == BUS_USB) {
		/* using the parent usb_device to match siblings */
		parent_udev = udev_device_get_parent(hid_udev);
		if (!streq("uhid", udev_device_get_sysname(parent_udev)))
			parent_udev = udev_device_get_parent_with_subsystem_devtype(hid_udev,
										    "usb",
										    "usb_device");
	} else {
		parent_udev = hid_udev;
	}

	e = udev_enumerate_new(udev);
	udev_enumerate_add_match_subsystem(e, "hidraw");
	udev_enumerate_add_match_parent(e, parent_udev);
	udev_enumerate_scan_devices(e);
	udev_list_entry_foreach(entry, udev_enumerate_get_list_entry(e)) {
		_cleanup_udev_device_unref_ struct udev_device *udev_device;

		path = udev_list_entry_get_name(entry);
		udev_device = udev_device_new_from_syspath(udev, path);
		if (!udev_device)
			continue;

		rc = ratbag_open_hidraw_node(device, udev_device);
		if (rc)
			goto skip;

		matched = match(device);
		if (matched == 1) {
			rc = 0;
			goto out;
		}

skip:
		ratbag_close_hidraw(device);
		rc = -ENODEV;
	}

out:
	udev_enumerate_unref(e);

	return rc;
}

int
ratbag_find_hidraw(struct ratbag_device *device, int (*match)(struct ratbag_device *device))
{
	return ratbag_find_hidraw_node(device, match, true);
}

static int
hidraw_match_all(struct ratbag_device *device)
{
	return 1;
}

int
ratbag_open_hidraw(struct ratbag_device *device)
{
	return ratbag_find_hidraw_node(device, hidraw_match_all, false);
}

static struct ratbag_hid_report *
ratbag_hidraw_get_report(struct ratbag_device *device, unsigned int report_id)
{
	unsigned i;

	if (report_id == 0) {
		if (device->hidraw.reports[0].report_id == report_id)
			return &device->hidraw.reports[0];
		else
			return NULL;
	}

	for (i = 0; i < device->hidraw.num_reports; i++) {
		if (device->hidraw.reports[i].report_id == report_id)
			return &device->hidraw.reports[i];
	}

	return NULL;
}


int
ratbag_hidraw_has_report(struct ratbag_device *device, unsigned int report_id)
{
	return ratbag_hidraw_get_report(device, report_id) != NULL;
}

unsigned int
ratbag_hidraw_get_usage_page(struct ratbag_device *device, unsigned int report_id)
{
	struct ratbag_hid_report *report;

	report = ratbag_hidraw_get_report(device, report_id);

	if (!report)
		return 0;

	return report->usage_page;
}

unsigned int
ratbag_hidraw_get_usage(struct ratbag_device *device, unsigned int report_id)
{
	struct ratbag_hid_report *report;

	report = ratbag_hidraw_get_report(device, report_id);

	if (!report)
		return 0;

	return report->usage;
}

void
ratbag_close_hidraw(struct ratbag_device *device)
{
	if (device->hidraw.fd < 0)
		return;

	if (device->hidraw.sysname) {
		free(device->hidraw.sysname);
		device->hidraw.sysname = NULL;
	}

	ratbag_close_fd(device, device->hidraw.fd);
	device->hidraw.fd = -1;

	if (device->hidraw.reports) {
		free(device->hidraw.reports);
		device->hidraw.reports = NULL;
	}
}

int
ratbag_hidraw_raw_request(struct ratbag_device *device, unsigned char reportnum,
			  uint8_t *buf, size_t len, unsigned char rtype, int reqtype)
{
	uint8_t tmp_buf[HID_MAX_BUFFER_SIZE];
	int rc;

	if (len < 1 || len > HID_MAX_BUFFER_SIZE || !buf || device->hidraw.fd < 0)
		return -EINVAL;

	if (rtype != HID_FEATURE_REPORT)
		return -ENOTSUP;

	switch (reqtype) {
	case HID_REQ_GET_REPORT:
		memset(tmp_buf, 0, len);
		tmp_buf[0] = reportnum;

		rc = ioctl(device->hidraw.fd, HIDIOCGFEATURE(len), tmp_buf);
		if (rc < 0)
			return -errno;

		log_buf_raw(device->ratbag, "feature get:   ", tmp_buf, (unsigned)rc);

		memcpy(buf, tmp_buf, rc);
		return rc;
	case HID_REQ_SET_REPORT:
		buf[0] = reportnum;

		log_buf_raw(device->ratbag, "feature set:   ", buf, len);
		rc = ioctl(device->hidraw.fd, HIDIOCSFEATURE(len), buf);
		if (rc < 0)
			return -errno;

		return rc;
	}

	return -EINVAL;
}

int
ratbag_hidraw_output_report(struct ratbag_device *device, uint8_t *buf, size_t len)
{
	int rc;

	if (len < 1 || len > HID_MAX_BUFFER_SIZE || !buf || device->hidraw.fd < 0)
		return -EINVAL;

	log_buf_raw(device->ratbag, "output report: ", buf, len);

	rc = write(device->hidraw.fd, buf, len);

	if (rc < 0)
		return -errno;

	if (rc != (int)len)
		return -EIO;

	return 0;
}

int
ratbag_hidraw_read_input_report(struct ratbag_device *device, uint8_t *buf, size_t len)
{
	int rc;
	struct pollfd fds;

	if (len < 1 || !buf || device->hidraw.fd < 0)
		return -EINVAL;

	fds.fd = device->hidraw.fd;
	fds.events = POLLIN;

	rc = poll(&fds, 1, 1000);
	if (rc == -1)
		return -errno;

	if (rc == 0)
		return -ETIMEDOUT;

	rc = read(device->hidraw.fd, buf, len);

	if (rc > 0)
		log_buf_raw(device->ratbag, "input report:  ", buf, rc);

	return rc >= 0 ? rc : -errno;
}
