#include "kifu-source-private.h"

#include <plugin-support.h>
#include <util/platform.h>

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

static const char *backend_state_name(enum kifu_backend_state state)
{
	switch (state) {
	case KIFU_BACKEND_STATE_POLLING:
		return "polling";
	case KIFU_BACKEND_STATE_STALE:
		return "stale";
	case KIFU_BACKEND_STATE_ERROR:
		return "error";
	case KIFU_BACKEND_STATE_IDLE:
	default:
		return "idle";
	}
}

static void backend_state_set(struct kifu_source *context, enum kifu_backend_state state, const char *format, ...)
{
	va_list args;
	char message[sizeof(context->backend_message)];

	pthread_mutex_lock(&context->mutex);
	context->backend_state = state;
	va_start(args, format);
	vsnprintf(message, sizeof(message), format, args);
	va_end(args);
	strncpy(context->backend_message, message, sizeof(context->backend_message) - 1U);
	context->backend_message[sizeof(context->backend_message) - 1U] = '\0';
	pthread_mutex_unlock(&context->mutex);

	obs_log(LOG_INFO, "backend state -> %s: %s", backend_state_name(state), context->backend_message);
}

static void free_backend_client(struct kifu_source *context)
{
	if (context->client != NULL) {
		kifu_client_destroy(context->client);
		context->client = NULL;
	}

	if (context->client_backend_address != NULL) {
		bfree(context->client_backend_address);
		context->client_backend_address = NULL;
	}
}

static bool ensure_backend_client(struct kifu_source *context, const struct kifu_snapshot *snapshot)
{
	const bool client_matches = context->client != NULL && context->client_backend_address != NULL &&
					strcmp(context->client_backend_address, snapshot->backend_address) == 0;

	if (client_matches && kifu_client_is_ready(context->client)) {
		return true;
	}

	free_backend_client(context);
	context->client = kifu_client_create(snapshot->backend_address);
	if (context->client == NULL) {
		backend_state_set(context, KIFU_BACKEND_STATE_ERROR, "failed to create backend client");
		return false;
	}

	context->client_backend_address = dup_or_empty(snapshot->backend_address);
	if (!kifu_client_is_ready(context->client)) {
		backend_state_set(context, KIFU_BACKEND_STATE_ERROR, "backend client not ready: %s", kifu_client_last_error(context->client));
		return false;
	}

	obs_log(LOG_INFO, "backend client connected to %s", snapshot->backend_address);
	return true;
}

static void *kifu_backend_worker(void *data)
{
	struct kifu_source *context = data;

	for (;;) {
		struct kifu_snapshot snapshot;
		bool stop_requested;
		enum kifu_capture_failure_reason capture_failure_reason;

		memset(&snapshot, 0, sizeof(snapshot));

		pthread_mutex_lock(&context->mutex);
		stop_requested = context->worker_stop_requested;
		snapshot.width = context->snapshot.width;
		snapshot.height = context->snapshot.height;
		snapshot.request_interval_ms = context->snapshot.request_interval_ms;
		snapshot.request_timeout_ms = context->snapshot.request_timeout_ms;
		snapshot.enabled = context->snapshot.enabled;
		snapshot.revision = context->snapshot.revision;
		snapshot.backend_address = dup_or_empty(context->snapshot.backend_address);
		snapshot.capture_source = dup_or_empty(context->snapshot.capture_source);
		capture_failure_reason = context->last_capture_failure_reason;
		pthread_mutex_unlock(&context->mutex);
		if (stop_requested) {
			free_snapshot_strings(&snapshot);
			break;
		}

		if (!snapshot.enabled) {
			backend_state_set(context, KIFU_BACKEND_STATE_IDLE, "source disabled");
			os_sleep_ms(100U);
			free_snapshot_strings(&snapshot);
			continue;
		}

		if (snapshot.backend_address == NULL || snapshot.backend_address[0] == '\0') {
			backend_state_set(context, KIFU_BACKEND_STATE_ERROR, "backend address missing");
			obs_log(LOG_WARNING, "backend polling skipped: backend address missing");
			os_sleep_ms(snapshot.request_interval_ms);
			free_snapshot_strings(&snapshot);
			continue;
		}

		if (!ensure_backend_client(context, &snapshot)) {
			os_sleep_ms(snapshot.request_interval_ms);
			free_snapshot_strings(&snapshot);
			continue;
		}

		pthread_mutex_lock(&context->mutex);
		context->backend_request_sequence += 1;
		const uint64_t request_sequence = context->backend_request_sequence;
		pthread_mutex_unlock(&context->mutex);

		uint8_t *frame_bytes = NULL;
		size_t frame_size = 0U;
		int32_t frame_width = 0;
		int32_t frame_height = 0;
		uint64_t frame_revision = 0U;
		kifu_frame_format_t frame_format = KIFU_FRAME_FORMAT_RGB24;
		bool using_capture_source = false;
		char request_id[64];
		struct kifu_submit_request request = {0};
		struct kifu_submit_result result = {0};

		pthread_mutex_lock(&context->mutex);
		if (context->capture_frame_bytes != NULL && context->capture_frame_size > 0U) {
			frame_size = context->capture_frame_size;
			frame_width = (int32_t)context->capture_width;
			frame_height = (int32_t)context->capture_height;
			frame_revision = context->capture_frame_revision;
			frame_bytes = bzalloc(frame_size);
			if (frame_bytes != NULL) {
				memcpy(frame_bytes, context->capture_frame_bytes, frame_size);
				using_capture_source = true;
				frame_format = KIFU_FRAME_FORMAT_RGB24;
			}
		}
		pthread_mutex_unlock(&context->mutex);

		if (!using_capture_source) {
			pthread_mutex_lock(&context->mutex);
			clear_latest_dice_locked(context);
			clear_inference_frame_locked(context);
			pthread_mutex_unlock(&context->mutex);
			if (snapshot.capture_source == NULL || snapshot.capture_source[0] == '\0') {
				backend_state_set(context, KIFU_BACKEND_STATE_STALE, "capture source is not selected");
			} else {
				backend_state_set(context,
						  KIFU_BACKEND_STATE_STALE,
						  "capture source frame unavailable (%s)",
						  kifu_capture_failure_reason_name(capture_failure_reason));
			}
			os_sleep_ms(snapshot.request_interval_ms);
			free_snapshot_strings(&snapshot);
			continue;
		}

		snprintf(request_id, sizeof(request_id), "kifu-%llu", (unsigned long long)request_sequence);
		request.request_id = request_id;
		request.source_id = using_capture_source && snapshot.capture_source != NULL && snapshot.capture_source[0] != '\0'
					    ? snapshot.capture_source
					    : obs_source_get_name(context->source);
		request.capture_time_unix_ms = (int64_t)(os_gettime_ns() / 1000000ULL);
		request.frame_data = frame_bytes;
		request.frame_data_size = frame_size;
		request.frame_format = frame_format;
		request.width = frame_width;
		request.height = frame_height;
		request.allow_stale_result = true;
		request.timeout_ms = snapshot.request_timeout_ms;

		backend_state_set(context,
				  KIFU_BACKEND_STATE_POLLING,
				  "submitting %s to %s (%s)",
				  request.request_id,
				  snapshot.backend_address,
				  request.source_id);

		if (!kifu_client_submit_frame(context->client, &request, &result)) {
			pthread_mutex_lock(&context->mutex);
			clear_latest_dice_locked(context);
			clear_inference_frame_locked(context);
			pthread_mutex_unlock(&context->mutex);
			backend_state_set(context, KIFU_BACKEND_STATE_ERROR, "%s", result.error_message[0] != '\0' ? result.error_message : kifu_client_last_error(context->client));
			bfree(frame_bytes);
			os_sleep_ms(snapshot.request_interval_ms);
			free_snapshot_strings(&snapshot);
			continue;
		}

		if (result.status == KIFU_RESULT_STATUS_OK) {
			pthread_mutex_lock(&context->mutex);
			context->latest_dice_count = result.dice_count;
			if (context->latest_dice_count > KIFU_MAX_DICE_RESULTS) {
				context->latest_dice_count = KIFU_MAX_DICE_RESULTS;
			}
			for (uint32_t i = 0; i < context->latest_dice_count; ++i) {
				context->latest_dice[i] = result.dice[i];
			}
			if (context->latest_dice_count > 0U) {
				const bool first_detection = !context->logo_has_seen_detection;
				context->logo_has_seen_detection = true;
				context->logo_last_detection_ns = os_gettime_ns();
				if (first_detection) {
					context->logo_visible = false;
					context->logo_visible_since_ns = 0U;
					context->logo_hidden_since_ns = 0U;
				}
			}
			clear_inference_frame_locked(context);
			if (frame_bytes != NULL && frame_size > 0U) {
				context->inference_frame_bytes = bzalloc(frame_size);
				if (context->inference_frame_bytes != NULL) {
					memcpy(context->inference_frame_bytes, frame_bytes, frame_size);
					context->inference_frame_size = frame_size;
					context->inference_frame_width = (uint32_t)frame_width;
					context->inference_frame_height = (uint32_t)frame_height;
					context->inference_frame_revision = frame_revision;
				}
			}
			context->latest_dice_frame_revision = frame_revision;
			pthread_mutex_unlock(&context->mutex);
			backend_state_set(context, KIFU_BACKEND_STATE_IDLE, "ok: %u detections / %u dice", result.detections_count, result.dice_count);
		} else if (result.status == KIFU_RESULT_STATUS_STALE) {
			pthread_mutex_lock(&context->mutex);
			clear_latest_dice_locked(context);
			clear_inference_frame_locked(context);
			pthread_mutex_unlock(&context->mutex);
			backend_state_set(context, KIFU_BACKEND_STATE_STALE, "stale: %u detections / %u dice", result.detections_count, result.dice_count);
		} else if (result.status == KIFU_RESULT_STATUS_TIMEOUT) {
			pthread_mutex_lock(&context->mutex);
			clear_latest_dice_locked(context);
			clear_inference_frame_locked(context);
			pthread_mutex_unlock(&context->mutex);
			backend_state_set(context, KIFU_BACKEND_STATE_STALE, "backend timeout");
		} else {
			pthread_mutex_lock(&context->mutex);
			clear_latest_dice_locked(context);
			clear_inference_frame_locked(context);
			pthread_mutex_unlock(&context->mutex);
			backend_state_set(context, KIFU_BACKEND_STATE_ERROR, "%s", result.error_message[0] != '\0' ? result.error_message : "backend returned an error");
		}

		bfree(frame_bytes);

		os_sleep_ms(snapshot.request_interval_ms);
		free_snapshot_strings(&snapshot);
	}

	backend_state_set(context, KIFU_BACKEND_STATE_IDLE, "worker stopped");
	return NULL;
}

void kifu_backend_free_client(struct kifu_source *context)
{
	free_backend_client(context);
}

void kifu_backend_start_worker(struct kifu_source *context)
{
	if (context->worker_thread_started) {
		return;
	}

	context->worker_stop_requested = false;
	if (pthread_create(&context->worker_thread, NULL, kifu_backend_worker, context) == 0) {
		context->worker_thread_started = true;
		obs_log(LOG_INFO, "backend worker started");
	} else {
		backend_state_set(context, KIFU_BACKEND_STATE_ERROR, "failed to start worker");
		obs_log(LOG_WARNING, "failed to start backend worker");
	}
}

void kifu_backend_stop_worker(struct kifu_source *context)
{
	if (!context->worker_thread_started) {
		return;
	}

	context->worker_stop_requested = true;
	pthread_join(context->worker_thread, NULL);
	context->worker_thread_started = false;
	obs_log(LOG_INFO, "backend worker stopped");
}
