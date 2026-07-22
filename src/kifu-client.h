#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum kifu_frame_format {
	KIFU_FRAME_FORMAT_UNSPECIFIED = 0,
	KIFU_FRAME_FORMAT_RGB24 = 1,
	KIFU_FRAME_FORMAT_RGBA32 = 2,
} kifu_frame_format_t;

typedef enum kifu_result_status {
	KIFU_RESULT_STATUS_UNSPECIFIED = 0,
	KIFU_RESULT_STATUS_OK = 1,
	KIFU_RESULT_STATUS_STALE = 2,
	KIFU_RESULT_STATUS_TIMEOUT = 3,
	KIFU_RESULT_STATUS_ERROR = 4,
} kifu_result_status_t;

enum { KIFU_MAX_DICE_RESULTS = 8 };

struct kifu_bounding_box {
	float x;
	float y;
	float width;
	float height;
};

struct kifu_dice_result {
	float confidence;
	struct kifu_bounding_box box;
	float center_x;
	float center_y;
};

struct kifu_submit_request {
	const char *request_id;
	const char *source_id;
	int64_t capture_time_unix_ms;
	const uint8_t *frame_data;
	size_t frame_data_size;
	kifu_frame_format_t frame_format;
	int32_t width;
	int32_t height;
	bool allow_stale_result;
	uint32_t timeout_ms;
};

struct kifu_submit_result {
	kifu_result_status_t status;
	int64_t processed_time_unix_ms;
	uint32_t detections_count;
	uint32_t dice_count;
	struct kifu_dice_result dice[KIFU_MAX_DICE_RESULTS];
	char error_message[256];
};

struct kifu_client;

struct kifu_client *kifu_client_create(const char *backend_address);
void kifu_client_destroy(struct kifu_client *client);
bool kifu_client_is_ready(const struct kifu_client *client);
const char *kifu_client_last_error(const struct kifu_client *client);
bool kifu_client_submit_frame(struct kifu_client *client,
			      const struct kifu_submit_request *request,
			      struct kifu_submit_result *response);

#ifdef __cplusplus
}
#endif