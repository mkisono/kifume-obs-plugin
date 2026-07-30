#include "kifu-source-private.h"

#include <plugin-support.h>
#include <util/platform.h>

#include <string.h>

struct capture_source_list_context {
	obs_property_t *property;
	obs_source_t *current_source;
	const char *selected_name;
	bool selected_found;
};

static bool add_video_source_to_property(void *data, obs_source_t *source)
{
	struct capture_source_list_context *context = data;
	if (context == NULL || context->property == NULL || source == NULL) {
		return true;
	}

	if (source == context->current_source) {
		return true;
	}

	if ((obs_source_get_output_flags(source) & OBS_SOURCE_VIDEO) == 0U) {
		return true;
	}

	const char *name = obs_source_get_name(source);
	if (name == NULL || name[0] == '\0') {
		return true;
	}

	obs_property_list_add_string(context->property, name, name);
	if (context->selected_name != NULL && strcmp(context->selected_name, name) == 0) {
		context->selected_found = true;
	}

	return true;
}

static obs_property_t *add_capture_source_property(obs_properties_t *properties, struct kifu_source *context)
{
	obs_property_t *capture_source_list = obs_properties_add_list(
		properties,
		"capture_source",
		"Capture Source",
		OBS_COMBO_TYPE_LIST,
		OBS_COMBO_FORMAT_STRING);
	if (capture_source_list == NULL) {
		return NULL;
	}

	obs_property_list_add_string(capture_source_list, "(None)", "");

	const char *selected_name = "";
	if (context != NULL) {
		pthread_mutex_lock(&context->mutex);
		if (context->snapshot.capture_source != NULL) {
			selected_name = context->snapshot.capture_source;
		}
		pthread_mutex_unlock(&context->mutex);
	}

	struct capture_source_list_context list_context = {
		.property = capture_source_list,
		.current_source = context != NULL ? context->source : NULL,
		.selected_name = selected_name,
		.selected_found = (selected_name == NULL || selected_name[0] == '\0'),
	};

	obs_enum_sources(add_video_source_to_property, &list_context);

	if (!list_context.selected_found &&
	    selected_name != NULL &&
	    selected_name[0] != '\0') {
		char unavailable_label[512];
		snprintf(unavailable_label,
				 sizeof(unavailable_label),
				 "%s (unavailable)",
				 selected_name);
		obs_property_list_add_string(capture_source_list, unavailable_label, selected_name);
	}

	obs_property_set_long_description(
		capture_source_list,
		"Select a video source from the current OBS source list."
		" Width/height and backend timing are managed by saved settings and are not editable here.");

	return capture_source_list;
}

void clear_latest_dice_locked(struct kifu_source *context)
{
	context->latest_dice_count = 0U;
	for (uint32_t slot = 0U; slot < 2U; ++slot) {
		context->latest_dice_valid[slot] = false;
	}
	context->latest_dice_frame_revision = 0U;
}

void clear_inference_frame_locked(struct kifu_source *context)
{
	if (context->inference_frame_bytes != NULL) {
		bfree(context->inference_frame_bytes);
		context->inference_frame_bytes = NULL;
	}
	context->inference_frame_size = 0U;
	context->inference_frame_width = 0U;
	context->inference_frame_height = 0U;
	context->inference_frame_revision = 0U;
}

char *dup_or_empty(const char *value)
{
	if (value == NULL) {
		value = "";
	}

	return bstrdup(value);
}

void free_snapshot_strings(struct kifu_snapshot *snapshot)
{
	if (snapshot->backend_address != NULL) {
		bfree(snapshot->backend_address);
		snapshot->backend_address = NULL;
	}

	if (snapshot->capture_source != NULL) {
		bfree(snapshot->capture_source);
		snapshot->capture_source = NULL;
	}
}

static void snapshot_init_defaults(struct kifu_snapshot *snapshot)
{
	snapshot->width = KIFU_DEFAULT_WIDTH;
	snapshot->height = KIFU_DEFAULT_HEIGHT;
	snapshot->request_interval_ms = KIFU_DEFAULT_REQUEST_INTERVAL_MS;
	snapshot->request_timeout_ms = KIFU_DEFAULT_REQUEST_TIMEOUT_MS;
	snapshot->enabled = true;
	snapshot->backend_address = dup_or_empty(KIFU_DEFAULT_BACKEND_ADDRESS);
	snapshot->capture_source = dup_or_empty("");
	snapshot->revision = 0;
}

static void snapshot_copy_from_data(struct kifu_snapshot *snapshot, obs_data_t *settings)
{
	const int64_t width = obs_data_get_int(settings, "width");
	const int64_t height = obs_data_get_int(settings, "height");
	const int64_t request_interval_ms = obs_data_get_int(settings, "request_interval_ms");
	const int64_t request_timeout_ms = obs_data_get_int(settings, "request_timeout_ms");
	const char *backend_address = obs_data_get_string(settings, "backend_address");
	if (backend_address == NULL || backend_address[0] == '\0') {
		backend_address = KIFU_DEFAULT_BACKEND_ADDRESS;
	}

	snapshot->width = (uint32_t)((width > 0) ? width : KIFU_DEFAULT_WIDTH);
	snapshot->height = (uint32_t)((height > 0) ? height : KIFU_DEFAULT_HEIGHT);
	snapshot->request_interval_ms = (uint32_t)((request_interval_ms >= 100) ? request_interval_ms : KIFU_DEFAULT_REQUEST_INTERVAL_MS);
	snapshot->request_timeout_ms = (uint32_t)((request_timeout_ms >= 100) ? request_timeout_ms : KIFU_DEFAULT_REQUEST_TIMEOUT_MS);
	snapshot->enabled = obs_data_get_bool(settings, "enabled");

	free_snapshot_strings(snapshot);
	snapshot->backend_address = dup_or_empty(backend_address);
	snapshot->capture_source = dup_or_empty(obs_data_get_string(settings, "capture_source"));
	snapshot->revision += 1;
}

static void source_apply_settings(struct kifu_source *context, obs_data_t *settings)
{
	char *previous_capture_source = NULL;
	char *updated_capture_source = NULL;
	bool capture_source_changed = false;

	pthread_mutex_lock(&context->mutex);
	previous_capture_source = dup_or_empty(context->snapshot.capture_source);
	snapshot_copy_from_data(&context->snapshot, settings);
	updated_capture_source = dup_or_empty(context->snapshot.capture_source);
	capture_source_changed = strcmp(previous_capture_source, updated_capture_source) != 0;
	pthread_mutex_unlock(&context->mutex);

	bfree(previous_capture_source);
	bfree(updated_capture_source);

	if (capture_source_changed) {
		kifu_capture_clear_frame(context);
	}

	obs_source_set_enabled(context->source, context->snapshot.enabled);
}

struct kifu_source *kifu_source_create(obs_data_t *settings, obs_source_t *source)
{
	struct kifu_source *context = bzalloc(sizeof(*context));
	context->source = source;
	pthread_mutex_init(&context->mutex, NULL);
	context->worker_thread_started = false;
	context->worker_stop_requested = false;
	context->client = NULL;
	context->client_backend_address = NULL;
	context->capture_texrender = NULL;
	context->capture_stagesurface = NULL;
	context->capture_width = 0;
	context->capture_height = 0;
	context->capture_color_format = 0;
	context->capture_frame_bytes = NULL;
	context->capture_frame_size = 0;
	context->capture_frame_revision = 0;
	context->inference_frame_bytes = NULL;
	context->inference_frame_size = 0U;
	context->inference_frame_width = 0U;
	context->inference_frame_height = 0U;
	context->inference_frame_revision = 0U;
	memset(&context->logo_image, 0, sizeof(context->logo_image));
	context->logo_image_loaded = false;
	context->logo_load_attempted = false;
	context->logo_load_warning_logged = false;
	context->logo_fade_effect = NULL;
	context->logo_fade_effect_load_attempted = false;
	context->logo_fade_effect_warning_logged = false;
	context->dice_stabilizer = kifu_dice_stabilizer_create();
	context->logo_state_machine = kifu_logo_state_machine_create();
	for (uint32_t slot = 0U; slot < 2U; ++slot) {
		context->preview_textures[slot] = NULL;
		context->preview_texture_width[slot] = 0U;
		context->preview_texture_height[slot] = 0U;
	}
	context->latest_dice_count = 0U;
	context->latest_dice_frame_revision = 0U;
	context->last_capture_failure_reason = KIFU_CAPTURE_FAILURE_NONE;
	for (uint32_t i = 0; i < KIFU_MAX_DICE_RESULTS; ++i) {
		context->latest_dice[i].confidence = 0.0F;
		context->latest_dice[i].box.x = 0.0F;
		context->latest_dice[i].box.y = 0.0F;
		context->latest_dice[i].box.width = 0.0F;
		context->latest_dice[i].box.height = 0.0F;
		context->latest_dice[i].center_x = 0.0F;
		context->latest_dice[i].center_y = 0.0F;
	}
	for (uint32_t slot = 0U; slot < 2U; ++slot) {
		context->latest_dice_valid[slot] = false;
	}
	context->backend_state = KIFU_BACKEND_STATE_IDLE;
	context->backend_request_sequence = 0;
	context->backend_message[0] = '\0';
	snapshot_init_defaults(&context->snapshot);

	if (settings != NULL) {
		source_apply_settings(context, settings);
	}

	obs_log(LOG_INFO, "source created");
	return context;
}

void kifu_source_destroy(void *data)
{
	struct kifu_source *context = data;
	if (context == NULL) {
		return;
	}

	kifu_backend_stop_worker(context);

	obs_enter_graphics();
	kifu_render_destroy_preview_textures(context);
	kifu_render_destroy_logo_image(context);
	kifu_capture_destroy_resources(context);
	obs_leave_graphics();

	pthread_mutex_lock(&context->mutex);
	free_snapshot_strings(&context->snapshot);
	if (context->capture_frame_bytes != NULL) {
		bfree(context->capture_frame_bytes);
		context->capture_frame_bytes = NULL;
	}
	clear_inference_frame_locked(context);
	pthread_mutex_unlock(&context->mutex);

	kifu_backend_free_client(context);
	kifu_dice_stabilizer_destroy(context->dice_stabilizer);
	kifu_logo_state_machine_destroy(context->logo_state_machine);
	pthread_mutex_destroy(&context->mutex);
	bfree(context);
	obs_log(LOG_INFO, "source destroyed");
}

void kifu_source_update(void *data, obs_data_t *settings)
{
	struct kifu_source *context = data;
	if (context == NULL || settings == NULL) {
		return;
	}

	source_apply_settings(context, settings);
}

void kifu_source_activate(void *data)
{
	struct kifu_source *context = data;
	if (context == NULL) {
		return;
	}

	pthread_mutex_lock(&context->mutex);
	kifu_logo_state_machine_activate(context->logo_state_machine, os_gettime_ns());
	pthread_mutex_unlock(&context->mutex);

	kifu_backend_start_worker(context);
	obs_log(LOG_INFO, "source activated");
}

void kifu_source_deactivate(void *data)
{
	struct kifu_source *context = data;
	if (context != NULL) {
		kifu_backend_stop_worker(context);
		kifu_capture_clear_frame(context);
		obs_enter_graphics();
		kifu_render_destroy_preview_textures(context);
		kifu_render_destroy_logo_image(context);
		kifu_capture_destroy_resources(context);
		obs_leave_graphics();
	}

	obs_log(LOG_INFO, "source deactivated");
}

void kifu_source_render(void *data, gs_effect_t *effect)
{
	struct kifu_source *context = data;
	if (context == NULL) {
		return;
	}

	struct kifu_snapshot snapshot;
	enum kifu_capture_failure_reason capture_failure_reason = KIFU_CAPTURE_FAILURE_NONE;
	bool should_render_logo = false;
	float logo_opacity = 1.0F;
	memset(&snapshot, 0, sizeof(snapshot));

	const uint64_t now_ns = os_gettime_ns();
	pthread_mutex_lock(&context->mutex);
	snapshot.width = context->snapshot.width;
	snapshot.height = context->snapshot.height;
	snapshot.request_interval_ms = context->snapshot.request_interval_ms;
	snapshot.request_timeout_ms = context->snapshot.request_timeout_ms;
	snapshot.enabled = context->snapshot.enabled;
	snapshot.revision = context->snapshot.revision;
	snapshot.backend_address = dup_or_empty(context->snapshot.backend_address);
	snapshot.capture_source = dup_or_empty(context->snapshot.capture_source);
	should_render_logo = kifu_render_update_logo_visibility_locked(context, now_ns);
	if (should_render_logo) {
		logo_opacity = kifu_render_logo_opacity_locked(context, now_ns);
	}
	pthread_mutex_unlock(&context->mutex);

	if (!snapshot.enabled) {
		free_snapshot_strings(&snapshot);
		return;
	}

	if (!kifu_capture_selected_source_frame(context, &snapshot, &capture_failure_reason)) {
		pthread_mutex_lock(&context->mutex);
		context->last_capture_failure_reason = capture_failure_reason;
		pthread_mutex_unlock(&context->mutex);
	}

	gs_effect_t *draw_effect = effect != NULL ? effect : obs_get_base_effect(OBS_EFFECT_DEFAULT);
	if (draw_effect == NULL) {
		free_snapshot_strings(&snapshot);
		return;
	}
	bool logo_texture_ready = false;
	if (should_render_logo) {
		logo_texture_ready = kifu_render_ensure_logo_image_texture(context);
	}
	const bool effect_already_active = gs_get_effect() == draw_effect;
	const bool draw_logo = should_render_logo && logo_texture_ready;

	gs_blend_state_push();
	gs_blend_function(GS_BLEND_SRCALPHA, GS_BLEND_INVSRCALPHA);
	if (draw_logo) {
		kifu_render_draw_logo(context, &snapshot, draw_effect, logo_opacity);
	} else if (effect_already_active) {
		kifu_render_draw_dice_crops(context, &snapshot, draw_effect);
	} else {
		while (gs_effect_loop(draw_effect, "Draw")) {
			kifu_render_draw_dice_crops(context, &snapshot, draw_effect);
		}
	}
	gs_blend_state_pop();

	free_snapshot_strings(&snapshot);
}

uint32_t kifu_source_get_width(void *data)
{
	struct kifu_source *context = data;
	if (context == NULL) {
		return 0;
	}

	pthread_mutex_lock(&context->mutex);
	const uint32_t width = context->snapshot.width;
	pthread_mutex_unlock(&context->mutex);
	return width;
}

uint32_t kifu_source_get_height(void *data)
{
	struct kifu_source *context = data;
	if (context == NULL) {
		return 0;
	}

	pthread_mutex_lock(&context->mutex);
	const uint32_t height = context->snapshot.height;
	pthread_mutex_unlock(&context->mutex);
	return height;
}

obs_properties_t *kifu_source_properties(void *data)
{
	struct kifu_source *context = data;
	obs_properties_t *properties = obs_properties_create();
	add_capture_source_property(properties, context);
	obs_properties_add_bool(properties, "enabled", "Enabled");
	return properties;
}

void kifu_source_defaults(obs_data_t *settings)
{
	obs_data_set_default_bool(settings, "enabled", true);
	obs_data_set_default_int(settings, "width", KIFU_DEFAULT_WIDTH);
	obs_data_set_default_int(settings, "height", KIFU_DEFAULT_HEIGHT);
	obs_data_set_default_int(settings, "request_interval_ms", KIFU_DEFAULT_REQUEST_INTERVAL_MS);
	obs_data_set_default_int(settings, "request_timeout_ms", KIFU_DEFAULT_REQUEST_TIMEOUT_MS);
	obs_data_set_default_string(settings, "backend_address", KIFU_DEFAULT_BACKEND_ADDRESS);
	obs_data_set_default_string(settings, "capture_source", "");
}

void kifu_source_save(void *data, obs_data_t *settings)
{
	struct kifu_source *context = data;
	if (context == NULL || settings == NULL) {
		return;
	}

	uint32_t width;
	uint32_t height;
	uint32_t request_interval_ms;
	uint32_t request_timeout_ms;
	bool enabled;
	char *backend_address;
	char *capture_source;

	pthread_mutex_lock(&context->mutex);
	width = context->snapshot.width;
	height = context->snapshot.height;
	request_interval_ms = context->snapshot.request_interval_ms;
	request_timeout_ms = context->snapshot.request_timeout_ms;
	enabled = context->snapshot.enabled;
	backend_address = dup_or_empty(context->snapshot.backend_address);
	capture_source = dup_or_empty(context->snapshot.capture_source);
	pthread_mutex_unlock(&context->mutex);

	obs_data_set_bool(settings, "enabled", enabled);
	obs_data_set_int(settings, "width", width);
	obs_data_set_int(settings, "height", height);
	obs_data_set_int(settings, "request_interval_ms", request_interval_ms);
	obs_data_set_int(settings, "request_timeout_ms", request_timeout_ms);
	obs_data_set_string(settings, "backend_address", backend_address);
	obs_data_set_string(settings, "capture_source", capture_source);

	bfree(backend_address);
	bfree(capture_source);
}

void kifu_source_load(void *data, obs_data_t *settings)
{
	struct kifu_source *context = data;
	if (context == NULL || settings == NULL) {
		return;
	}

	source_apply_settings(context, settings);
}

const char *kifu_source_get_name(void *type_data)
{
	(void)type_data;
	return "Dice Magnifier";
}

struct obs_source_info kifu_source_info = {
	.id = "kifu_me_overlay_source",
	.type = OBS_SOURCE_TYPE_INPUT,
	.output_flags = OBS_SOURCE_VIDEO,
	.get_name = kifu_source_get_name,
	.create = (void *(*)(obs_data_t *, obs_source_t *))kifu_source_create,
	.destroy = kifu_source_destroy,
	.update = kifu_source_update,
	.activate = kifu_source_activate,
	.deactivate = kifu_source_deactivate,
	.video_render = kifu_source_render,
	.get_width = kifu_source_get_width,
	.get_height = kifu_source_get_height,
	.get_properties = kifu_source_properties,
	.get_defaults = kifu_source_defaults,
	.load = kifu_source_load,
	.save = kifu_source_save,
};
