#pragma once

#include "kifu-source.h"
#include "kifu-source-internal.h"

#include <kifu-client.h>

#include <graphics/graphics.h>
#include <graphics/image-file.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum kifu_backend_state {
	KIFU_BACKEND_STATE_IDLE,
	KIFU_BACKEND_STATE_POLLING,
	KIFU_BACKEND_STATE_STALE,
	KIFU_BACKEND_STATE_ERROR,
};

struct kifu_snapshot {
	uint32_t width;
	uint32_t height;
	uint32_t request_interval_ms;
	uint32_t request_timeout_ms;
	bool enabled;
	char *backend_address;
	char *capture_source;
	uint64_t revision;
};

struct kifu_source {
	obs_source_t *source;
	struct kifu_snapshot snapshot;
	pthread_mutex_t mutex;
	pthread_t worker_thread;
	bool worker_thread_started;
	bool worker_stop_requested;
	struct kifu_client *client;
	char *client_backend_address;
	gs_texrender_t *capture_texrender;
	gs_stagesurf_t *capture_stagesurface;
	uint32_t capture_width;
	uint32_t capture_height;
	uint32_t capture_color_format;
	uint8_t *capture_frame_bytes;
	size_t capture_frame_size;
	uint64_t capture_frame_revision;
	uint8_t *inference_frame_bytes;
	size_t inference_frame_size;
	uint32_t inference_frame_width;
	uint32_t inference_frame_height;
	uint64_t inference_frame_revision;
	gs_texture_t *preview_textures[2];
	uint32_t preview_texture_width[2];
	uint32_t preview_texture_height[2];
	struct gs_image_file logo_image;
	bool logo_image_loaded;
	bool logo_load_attempted;
	bool logo_load_warning_logged;
	bool logo_has_seen_detection;
	uint64_t logo_last_detection_ns;
	uint64_t logo_boot_grace_until_ns;
	uint64_t logo_hidden_since_ns;
	bool logo_visible;
	uint64_t logo_visible_since_ns;
	struct kifu_dice_result latest_dice[KIFU_MAX_DICE_RESULTS];
	uint32_t latest_dice_count;
	uint64_t latest_dice_frame_revision;
	enum kifu_capture_failure_reason last_capture_failure_reason;
	enum kifu_backend_state backend_state;
	uint64_t backend_request_sequence;
	char backend_message[256];
};

static const uint32_t KIFU_DEFAULT_WIDTH = 240U;
static const uint32_t KIFU_DEFAULT_HEIGHT = 120U;
static const uint32_t KIFU_DEFAULT_REQUEST_INTERVAL_MS = 500U;
static const uint32_t KIFU_DEFAULT_REQUEST_TIMEOUT_MS = 500U;
static const uint32_t KIFU_CAPTURE_FRAME_WIDTH = 640U;
static const char *KIFU_DEFAULT_BACKEND_ADDRESS = "127.0.0.1:50051";
static const uint64_t KIFU_LOGO_IDLE_DELAY_NS = 3ULL * 10ULL * 1000000000ULL;
static const uint64_t KIFU_LOGO_MIN_VISIBLE_NS = 10ULL * 1000000000ULL;
