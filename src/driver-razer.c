/*
 * Copyright © 2026 Dennis Schweizer
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

/*
 * Driver for Razer mice speaking the "Razer report" control protocol.
 *
 * The protocol is a 90 byte HID feature report on report id 0. Every command
 * is answered by reading the same feature report back. The wire format was
 * cross-checked against OpenRazer's kernel driver and verified against a
 * DeathAdder V3 (1532:00b2, firmware 1.2) before this driver was written.
 *
 * Finding the control interface
 * -----------------------------
 * The feature report is declared in the vendor usage page 0xff00, but that
 * page sits *inside* the Generic Desktop/Mouse application collection:
 *
 *     05 01 09 02 a1 01                  Generic Desktop, Mouse, Application
 *       [...]
 *       06 00 ff 09 02                   usage page 0xff00, usage 0x02
 *       15 00 25 01 75 08 95 5a b1 01    feature, 0x5a = 90 bytes
 *     c0
 *
 * ratbag_hidraw_has_vendor_page() cannot see this: the descriptor parser
 * records the usage page of the first application collection only, so the
 * interface is filed under Generic Desktop and the vendor page is never
 * indexed. The remaining two interfaces of the device are keyboard
 * collections (usage 0x06), which is what razer_test_hidraw() tells apart.
 *
 * The descriptor check comes first and nothing is sent before it passes:
 * a request to the keyboard collections stalls the control transfer, the
 * device resets and udev re-adds it, which loops. A firmware request then
 * confirms that the one remaining candidate really speaks the protocol.
 *
 * No lighting
 * -----------
 * The DeathAdder V3 has no addressable lighting zone, only a DPI indicator
 * LED with no documented protocol; OpenRazer creates poll_rate, dpi and
 * dpi_stages for 0x00b2 and no LED attribute at all. This driver reports
 * zero LEDs. Models that do have zones need a device data key and a led
 * count in ratbag_device_init_profiles(), not a restructuring.
 *
 * Two details differ from OpenRazer and are deliberate here:
 *
 *   - DPI stage ids are 1-based. The device reports stages as id 1..5, so
 *     this driver writes them back the same way. OpenRazer writes 0-based
 *     ids when setting stages.
 *
 *   - The last two bytes of a stage record are not reserved. On the
 *     DeathAdder V3 they hold a third copy of the stage's DPI value.
 *     OpenRazer zeroes them; this driver preserves what the device
 *     reported and only rewrites the field for stages it actually changes.
 */

#include "config.h"

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "libratbag-private.h"
#include "libratbag-hidraw.h"
#include "libratbag-data.h"

#define RAZER_REPORT_LEN		90
#define RAZER_REPORT_ID			0x00
/* buffer handed to the hidraw helpers: report id byte + report */
#define RAZER_BUF_LEN			(RAZER_REPORT_LEN + 1)

/*
 * Transaction id. Razer uses this to group request and response; the value
 * is model specific. 0x1f covers the DeathAdder V3 generation, older models
 * use 0xff. Kept in the device data so other models can be added without
 * touching this file.
 */
#define RAZER_DEFAULT_TRANSACTION_ID	0x1f

/* the application collection the control interface reports for report id 0 */
#define HID_USAGE_PAGE_GENERIC_DESKTOP	0x01
#define HID_USAGE_MOUSE			0x02

#define RAZER_NUM_PROFILES		1
#define RAZER_MAX_DPI_STAGES		5
#define RAZER_STAGE_RECORD_LEN		7

/* variable storage selectors */
#define RAZER_NOSTORE			0x00
#define RAZER_VARSTORE			0x01

/* command classes */
#define RAZER_CLASS_MISC		0x00
#define RAZER_CLASS_DPI			0x04

/* command ids */
#define RAZER_CMD_GET_FIRMWARE		0x81
#define RAZER_CMD_GET_SERIAL		0x82
#define RAZER_CMD_SET_POLL_RATE2	0x40
#define RAZER_CMD_GET_POLL_RATE2	0xc0
#define RAZER_CMD_SET_DPI		0x05
#define RAZER_CMD_GET_DPI		0x85
#define RAZER_CMD_SET_DPI_STAGES	0x06
#define RAZER_CMD_GET_DPI_STAGES	0x86

/* response status codes */
#define RAZER_STATUS_NEW		0x00
#define RAZER_STATUS_BUSY		0x01
#define RAZER_STATUS_SUCCESS		0x02
#define RAZER_STATUS_FAILURE		0x03
#define RAZER_STATUS_TIMEOUT		0x04
#define RAZER_STATUS_NOT_SUPPORTED	0x05

/* the device needs a moment before the answer can be read back */
#define RAZER_WAIT_US			1000
#define RAZER_RETRY_WAIT_US		10000
#define RAZER_NUM_RETRIES		5

struct razer_report {
	uint8_t status;
	uint8_t transaction_id;
	uint16_t remaining_packets;	/* big endian */
	uint8_t protocol_type;
	uint8_t data_size;
	uint8_t command_class;
	uint8_t command_id;
	uint8_t arguments[80];
	uint8_t crc;
	uint8_t reserved;
} __attribute__((packed));

_Static_assert(sizeof(struct razer_report) == RAZER_REPORT_LEN,
	       "struct razer_report must match the 90 byte wire format");

struct razer_data {
	uint8_t transaction_id;
	unsigned int stage_count;
	/*
	 * Trailing per-stage field as read from the device. Observed to
	 * mirror the DPI, but its meaning is not documented, so it is
	 * carried over untouched for stages that stay unchanged.
	 */
	uint16_t stage_trailer[RAZER_MAX_DPI_STAGES];
};

/*
 * The DeathAdder V3 reports rates in a bitmask-like encoding rather than as
 * a divisor. Highest rate is the lowest bit.
 */
static const struct razer_rate_map {
	unsigned int hz;
	uint8_t code;
} razer_rate_map[] = {
	{ 8000, 0x01 },
	{ 4000, 0x02 },
	{ 2000, 0x04 },
	{ 1000, 0x08 },
	{  500, 0x10 },
	{  250, 0x20 },
	{  125, 0x40 },
};

static uint8_t
razer_rate_to_code(unsigned int hz)
{
	for (size_t i = 0; i < ARRAY_LENGTH(razer_rate_map); i++) {
		if (razer_rate_map[i].hz == hz)
			return razer_rate_map[i].code;
	}
	return 0;
}

static unsigned int
razer_code_to_rate(uint8_t code)
{
	for (size_t i = 0; i < ARRAY_LENGTH(razer_rate_map); i++) {
		if (razer_rate_map[i].code == code)
			return razer_rate_map[i].hz;
	}
	return 0;
}

static struct razer_report
razer_init_report(uint8_t transaction_id, uint8_t command_class,
		  uint8_t command_id, uint8_t data_size)
{
	struct razer_report report;

	memset(&report, 0, sizeof(report));
	report.status = RAZER_STATUS_NEW;
	report.transaction_id = transaction_id;
	report.command_class = command_class;
	report.command_id = command_id;
	report.data_size = data_size;

	return report;
}

/* simple XOR over bytes 2..87, the two trailing bytes are excluded */
static uint8_t
razer_crc(const struct razer_report *report)
{
	const uint8_t *bytes = (const uint8_t *)report;
	uint8_t crc = 0;

	for (unsigned int i = 2; i < 88; i++)
		crc ^= bytes[i];

	return crc;
}

static const char *
razer_status_string(uint8_t status)
{
	switch (status) {
	case RAZER_STATUS_NEW:		return "new command";
	case RAZER_STATUS_BUSY:		return "busy";
	case RAZER_STATUS_SUCCESS:	return "success";
	case RAZER_STATUS_FAILURE:	return "failure";
	case RAZER_STATUS_TIMEOUT:	return "timeout";
	case RAZER_STATUS_NOT_SUPPORTED:return "not supported";
	default:			return "unknown";
	}
}

/**
 * Send one command and read its answer back.
 *
 * @return 0 on success or a negative errno
 */
static int
razer_transact_n(struct ratbag_device *device,
		 struct razer_report *request,
		 struct razer_report *response,
		 int retries)
{
	uint8_t buf[RAZER_BUF_LEN];
	uint8_t status = RAZER_STATUS_NEW;
	int rc = -ETIMEDOUT;

	request->crc = razer_crc(request);

	for (int retry = 0; retry < retries; retry++) {
		memset(buf, 0, sizeof(buf));
		memcpy(buf + 1, request, RAZER_REPORT_LEN);

		rc = ratbag_hidraw_set_feature_report(device, RAZER_REPORT_ID,
						      buf, sizeof(buf));
		if (rc < 0)
			goto retry;

		usleep(RAZER_WAIT_US);

		memset(buf, 0, sizeof(buf));
		rc = ratbag_hidraw_get_feature_report(device, RAZER_REPORT_ID,
						      buf, sizeof(buf));
		if (rc < 0)
			goto retry;
		if (rc < (int)RAZER_BUF_LEN) {
			rc = -EIO;
			goto retry;
		}

		memcpy(response, buf + 1, RAZER_REPORT_LEN);

		/* the answer has to belong to the command we just sent */
		if (response->command_class != request->command_class ||
		    response->command_id != request->command_id) {
			rc = -EINVAL;
			goto retry;
		}

		status = response->status;

		/* some commands answer 'busy' but have taken effect anyway */
		if (status == RAZER_STATUS_SUCCESS || status == RAZER_STATUS_BUSY)
			return 0;

		switch (status) {
		case RAZER_STATUS_NOT_SUPPORTED:
			/* retrying will not make the device grow the feature */
			log_debug(device->ratbag,
				  "razer: command %02x/%02x not supported\n",
				  request->command_class, request->command_id);
			return -ENOTSUP;
		case RAZER_STATUS_FAILURE:
			rc = -EINVAL;
			break;
		case RAZER_STATUS_TIMEOUT:
			rc = -ETIMEDOUT;
			break;
		default:
			rc = -EIO;
			break;
		}

retry:
		log_debug(device->ratbag,
			  "razer: command %02x/%02x failed (%d, status %s), %d retries left\n",
			  request->command_class, request->command_id, rc,
			  razer_status_string(status),
			  retries - retry - 1);
		usleep(RAZER_RETRY_WAIT_US);
	}

	return rc;
}

static int
razer_transact(struct ratbag_device *device,
	       struct razer_report *request,
	       struct razer_report *response)
{
	return razer_transact_n(device, request, response, RAZER_NUM_RETRIES);
}

static int
razer_get_firmware_version(struct ratbag_device *device,
			   unsigned int *major, unsigned int *minor)
{
	struct razer_data *drv_data = ratbag_get_drv_data(device);
	struct razer_report request, response;
	int rc;

	request = razer_init_report(drv_data->transaction_id,
				    RAZER_CLASS_MISC, RAZER_CMD_GET_FIRMWARE, 0x02);

	rc = razer_transact(device, &request, &response);
	if (rc)
		return rc;

	*major = response.arguments[0];
	*minor = response.arguments[1];

	return 0;
}

static int
razer_get_report_rate(struct ratbag_device *device, unsigned int *hz)
{
	struct razer_data *drv_data = ratbag_get_drv_data(device);
	struct razer_report request, response;
	unsigned int rate;
	int rc;

	request = razer_init_report(drv_data->transaction_id,
				    RAZER_CLASS_MISC, RAZER_CMD_GET_POLL_RATE2, 0x01);

	rc = razer_transact(device, &request, &response);
	if (rc)
		return rc;

	/*
	 * The device echoes data_size 1 but puts the answer in
	 * arguments[1], so the declared size cannot be used here.
	 */
	rate = razer_code_to_rate(response.arguments[1]);
	if (!rate) {
		log_debug(device->ratbag,
			  "razer: unknown report rate code 0x%02x\n",
			  response.arguments[1]);
		return -EINVAL;
	}

	*hz = rate;

	return 0;
}

static int
razer_set_report_rate(struct ratbag_device *device, unsigned int hz)
{
	struct razer_data *drv_data = ratbag_get_drv_data(device);
	struct razer_report request, response;
	uint8_t code;
	int rc;

	code = razer_rate_to_code(hz);
	if (!code) {
		log_error(device->ratbag,
			  "razer: unsupported report rate %u Hz\n", hz);
		return -EINVAL;
	}

	/*
	 * Razer sends this command twice, once with arguments[0] == 0 and
	 * once with 1. Both are needed for the change to stick.
	 */
	for (uint8_t pass = 0; pass <= 1; pass++) {
		request = razer_init_report(drv_data->transaction_id,
					    RAZER_CLASS_MISC,
					    RAZER_CMD_SET_POLL_RATE2, 0x02);
		request.arguments[0] = pass;
		request.arguments[1] = code;

		rc = razer_transact(device, &request, &response);
		if (rc)
			return rc;
	}

	return 0;
}

struct razer_stage {
	uint8_t id;		/* 1-based */
	uint16_t dpi_x;
	uint16_t dpi_y;
	uint16_t trailer;
};

static int
razer_get_dpi_stages(struct ratbag_device *device,
		     struct razer_stage stages[RAZER_MAX_DPI_STAGES],
		     unsigned int *count, unsigned int *active)
{
	struct razer_data *drv_data = ratbag_get_drv_data(device);
	struct razer_report request, response;
	unsigned int n;
	int rc;

	request = razer_init_report(drv_data->transaction_id,
				    RAZER_CLASS_DPI, RAZER_CMD_GET_DPI_STAGES, 0x26);
	request.arguments[0] = RAZER_VARSTORE;

	rc = razer_transact(device, &request, &response);
	if (rc)
		return rc;

	*active = response.arguments[1];
	n = response.arguments[2];

	if (n == 0 || n > RAZER_MAX_DPI_STAGES) {
		log_error(device->ratbag,
			  "razer: device reports %u dpi stages, expected 1..%d\n",
			  n, RAZER_MAX_DPI_STAGES);
		return -EINVAL;
	}

	for (unsigned int i = 0; i < n; i++) {
		const uint8_t *rec = &response.arguments[3 + i * RAZER_STAGE_RECORD_LEN];

		stages[i].id = rec[0];
		stages[i].dpi_x = (rec[1] << 8) | rec[2];
		stages[i].dpi_y = (rec[3] << 8) | rec[4];
		stages[i].trailer = (rec[5] << 8) | rec[6];
	}

	*count = n;

	return 0;
}

static int
razer_set_dpi_stages(struct ratbag_device *device,
		     const struct razer_stage stages[RAZER_MAX_DPI_STAGES],
		     unsigned int count, unsigned int active)
{
	struct razer_data *drv_data = ratbag_get_drv_data(device);
	struct razer_report request, response;
	uint8_t data_size;

	assert(count > 0 && count <= RAZER_MAX_DPI_STAGES);

	data_size = 3 + count * RAZER_STAGE_RECORD_LEN;

	request = razer_init_report(drv_data->transaction_id,
				    RAZER_CLASS_DPI, RAZER_CMD_SET_DPI_STAGES,
				    data_size);
	request.arguments[0] = RAZER_VARSTORE;
	request.arguments[1] = active;
	request.arguments[2] = count;

	for (unsigned int i = 0; i < count; i++) {
		uint8_t *rec = &request.arguments[3 + i * RAZER_STAGE_RECORD_LEN];

		rec[0] = stages[i].id;
		rec[1] = stages[i].dpi_x >> 8;
		rec[2] = stages[i].dpi_x & 0xff;
		rec[3] = stages[i].dpi_y >> 8;
		rec[4] = stages[i].dpi_y & 0xff;
		rec[5] = stages[i].trailer >> 8;
		rec[6] = stages[i].trailer & 0xff;
	}

	return razer_transact(device, &request, &response);
}

static int
razer_set_active_dpi(struct ratbag_device *device,
		     unsigned int dpi_x, unsigned int dpi_y)
{
	struct razer_data *drv_data = ratbag_get_drv_data(device);
	struct razer_report request, response;

	request = razer_init_report(drv_data->transaction_id,
				    RAZER_CLASS_DPI, RAZER_CMD_SET_DPI, 0x07);
	request.arguments[0] = RAZER_VARSTORE;
	request.arguments[1] = dpi_x >> 8;
	request.arguments[2] = dpi_x & 0xff;
	request.arguments[3] = dpi_y >> 8;
	request.arguments[4] = dpi_y & 0xff;

	return razer_transact(device, &request, &response);
}

/**
 * Pick the interface that speaks the control protocol: the mouse collection,
 * never the keyboard ones. See "Finding the control interface" at the top of
 * this file for why ratbag_hidraw_has_vendor_page() is no help and why the
 * descriptor is checked before anything is sent.
 */
static int
razer_test_hidraw(struct ratbag_device *device)
{
	struct razer_data *drv_data = ratbag_get_drv_data(device);
	struct razer_report request, response;

	if (ratbag_hidraw_get_usage_page(device, 0) != HID_USAGE_PAGE_GENERIC_DESKTOP ||
	    ratbag_hidraw_get_usage(device, 0) != HID_USAGE_MOUSE)
		return 0;

	request = razer_init_report(drv_data->transaction_id,
				    RAZER_CLASS_MISC, RAZER_CMD_GET_FIRMWARE, 0x02);

	return razer_transact_n(device, &request, &response, 1) == 0;
}

/*
 * Buttons are reported so that they show up in the UI, but this driver does
 * not remap them - the request was to leave everything except DPI alone.
 * No action type is enabled, so libratbag refuses writes for them.
 */
static void
razer_init_button(struct ratbag_button *button, size_t button_count)
{
	struct ratbag_button_action actions[8] = {
		BUTTON_ACTION_BUTTON(1),
		BUTTON_ACTION_BUTTON(2),
		BUTTON_ACTION_BUTTON(3),
		BUTTON_ACTION_BUTTON(4),
		BUTTON_ACTION_BUTTON(5),
		BUTTON_ACTION_BUTTON(6),
		BUTTON_ACTION_BUTTON(7),
		BUTTON_ACTION_BUTTON(8),
	};

	if (button->index >= ARRAY_LENGTH(actions))
		return;

	/* the last button is the DPI cycle button underneath the mouse */
	if (button_count >= 1 && button->index == button_count - 1) {
		actions[button->index].type = RATBAG_BUTTON_ACTION_TYPE_SPECIAL;
		actions[button->index].action.special =
			RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_CYCLE_UP;
	}

	ratbag_button_set_action(button, &actions[button->index]);
}

static int
razer_read_settings(struct ratbag_device *device)
{
	struct razer_data *drv_data = ratbag_get_drv_data(device);
	struct razer_stage stages[RAZER_MAX_DPI_STAGES] = {0};
	struct ratbag_profile *profile;
	struct ratbag_resolution *resolution;
	unsigned int count = 0, active = 0, hz = 0;
	int rc;

	rc = razer_get_dpi_stages(device, stages, &count, &active);
	if (rc)
		return rc;

	if (count != drv_data->stage_count) {
		log_error(device->ratbag,
			  "razer: device reports %u dpi stages, %u were configured\n",
			  count, drv_data->stage_count);
		return -EINVAL;
	}

	ratbag_device_for_each_profile(device, profile) {
		ratbag_profile_for_each_resolution(profile, resolution) {
			const struct razer_stage *stage = &stages[resolution->index];

			resolution->dpi_x = stage->dpi_x;
			resolution->dpi_y = stage->dpi_y;
			/* stage ids are 1-based, resolution indices are not */
			resolution->is_active = (stage->id == active);
			resolution->is_default = resolution->is_active;

			drv_data->stage_trailer[resolution->index] = stage->trailer;
		}

		rc = razer_get_report_rate(device, &hz);
		if (rc == 0)
			profile->hz = hz;
		else
			log_debug(device->ratbag,
				  "razer: could not read the report rate\n");
	}

	return 0;
}

static int
razer_probe(struct ratbag_device *device)
{
	struct razer_data *drv_data;
	struct ratbag_profile *profile;
	struct ratbag_resolution *resolution;
	struct ratbag_button *button;
	const struct dpi_range *dpirange;
	const struct dpi_list *dpilist;
	unsigned int button_count, stage_count;
	unsigned int major = 0, minor = 0;
	int rc;

	static const unsigned int report_rates[] = {
		125, 250, 500, 1000, 2000, 4000, 8000,
	};

	/*
	 * The transaction id has to be known before probing the interfaces,
	 * because razer_test_hidraw() talks to the device to find the right
	 * one.
	 */
	drv_data = zalloc(sizeof(*drv_data));
	ratbag_set_drv_data(device, drv_data);

	rc = ratbag_device_data_razer_get_transaction_id(device->data);
	drv_data->transaction_id = (rc == -1) ? RAZER_DEFAULT_TRANSACTION_ID
					      : (uint8_t)rc;

	rc = ratbag_find_hidraw(device, razer_test_hidraw);
	if (rc) {
		log_debug(device->ratbag,
			  "razer: no control interface found on this device\n");
		goto err;
	}

	rc = ratbag_device_data_razer_get_dpi_stage_count(device->data);
	stage_count = (rc == -1) ? RAZER_MAX_DPI_STAGES : (unsigned int)rc;
	if (stage_count == 0 || stage_count > RAZER_MAX_DPI_STAGES) {
		log_error(device->ratbag,
			  "razer: DpiStages must be 1..%d, got %u\n",
			  RAZER_MAX_DPI_STAGES, stage_count);
		rc = -EINVAL;
		goto err;
	}
	drv_data->stage_count = stage_count;

	rc = ratbag_device_data_razer_get_button_count(device->data);
	button_count = (rc == -1) ? 0 : (unsigned int)rc;

	ratbag_device_init_profiles(device, RAZER_NUM_PROFILES, stage_count,
				    button_count, 0);

	dpirange = ratbag_device_data_razer_get_dpi_range(device->data);
	dpilist = ratbag_device_data_razer_get_dpi_list(device->data);

	ratbag_device_for_each_profile(device, profile) {
		profile->is_active = true;

		ratbag_profile_set_report_rate_list(profile, report_rates,
						    ARRAY_LENGTH(report_rates));

		ratbag_profile_for_each_resolution(profile, resolution) {
			ratbag_resolution_set_cap(resolution,
						  RATBAG_RESOLUTION_CAP_SEPARATE_XY_RESOLUTION);

			if (dpirange)
				ratbag_resolution_set_dpi_list_from_range(resolution,
									  dpirange->min,
									  dpirange->max);
			if (dpilist)
				ratbag_resolution_set_dpi_list(resolution,
							       (unsigned int *)dpilist->entries,
							       dpilist->nentries);
		}

		ratbag_profile_for_each_button(profile, button)
			razer_init_button(button, button_count);
	}

	rc = razer_get_firmware_version(device, &major, &minor);
	if (rc == 0) {
		_cleanup_free_ char *fw = asprintf_safe("%u.%u", major, minor);
		ratbag_device_set_firmware_version(device, fw);
	}

	rc = razer_read_settings(device);
	if (rc) {
		log_error(device->ratbag,
			  "razer: failed to read the device settings\n");
		goto err;
	}

	return 0;

err:
	free(drv_data);
	ratbag_set_drv_data(device, NULL);
	return rc;
}

static int
razer_commit(struct ratbag_device *device)
{
	struct razer_data *drv_data = ratbag_get_drv_data(device);
	struct ratbag_profile *profile;
	struct ratbag_resolution *resolution;
	int rc;

	ratbag_device_for_each_profile(device, profile) {
		struct razer_stage stages[RAZER_MAX_DPI_STAGES] = {0};
		struct ratbag_resolution *active_res = NULL;
		bool resolutions_dirty = false;

		if (!profile->dirty)
			continue;

		ratbag_profile_for_each_resolution(profile, resolution) {
			struct razer_stage *stage = &stages[resolution->index];

			stage->id = resolution->index + 1;
			stage->dpi_x = resolution->dpi_x;
			stage->dpi_y = resolution->dpi_y;

			/*
			 * The trailing field mirrors the DPI on every stage
			 * the device reported. Track that for stages we
			 * change and keep the original value otherwise.
			 */
			stage->trailer = resolution->dirty
					       ? resolution->dpi_x
					       : drv_data->stage_trailer[resolution->index];

			if (resolution->dirty)
				resolutions_dirty = true;

			if (resolution->is_active)
				active_res = resolution;
		}

		if (resolutions_dirty) {
			unsigned int active = active_res ? active_res->index + 1 : 1;

			rc = razer_set_dpi_stages(device, stages,
						  drv_data->stage_count, active);
			if (rc) {
				log_error(device->ratbag,
					  "razer: failed to write the dpi stages\n");
				return rc;
			}

			for (unsigned int i = 0; i < drv_data->stage_count; i++)
				drv_data->stage_trailer[i] = stages[i].trailer;

			/*
			 * Writing the stages stores them, but does not switch
			 * the sensor over. Apply the active stage explicitly
			 * so the change is felt right away.
			 */
			if (active_res) {
				rc = razer_set_active_dpi(device,
							  active_res->dpi_x,
							  active_res->dpi_y);
				if (rc) {
					log_error(device->ratbag,
						  "razer: failed to apply the active dpi\n");
					return rc;
				}
			}
		}

		if (profile->rate_dirty) {
			rc = razer_set_report_rate(device, profile->hz);
			if (rc) {
				log_error(device->ratbag,
					  "razer: failed to write the report rate\n");
				return rc;
			}
		}
	}

	return 0;
}

static void
razer_remove(struct ratbag_device *device)
{
	ratbag_close_hidraw(device);
	free(ratbag_get_drv_data(device));
}

struct ratbag_driver razer_driver = {
	.name = "Razer",
	.id = "razer",
	.probe = razer_probe,
	.remove = razer_remove,
	.commit = razer_commit,
};
