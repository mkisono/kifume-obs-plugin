#include "kifu-source-private.h"

#include <plugin-support.h>
#include <math.h>

void kifu_capture_destroy_resources(struct kifu_source *context)
{
	if (context->capture_stagesurface != NULL) {
		gs_stagesurface_destroy(context->capture_stagesurface);
		context->capture_stagesurface = NULL;
	}

	if (context->capture_texrender != NULL) {
		gs_texrender_destroy(context->capture_texrender);
		context->capture_texrender = NULL;
	}

	context->capture_width = 0;
	context->capture_height = 0;
	context->capture_color_format = 0;
}

void kifu_capture_clear_frame(struct kifu_source *context)
{
	pthread_mutex_lock(&context->mutex);
	const bool had_capture_or_dice =
		context->capture_frame_bytes != NULL || context->capture_frame_size > 0U ||
		context->inference_frame_bytes != NULL || context->inference_frame_size > 0U ||
		context->latest_dice_count > 0U;
	if (context->capture_frame_bytes != NULL) {
		bfree(context->capture_frame_bytes);
		context->capture_frame_bytes = NULL;
	}
	context->capture_frame_size = 0;
	clear_inference_frame_locked(context);
	clear_latest_dice_locked(context);
	context->last_capture_failure_reason = KIFU_CAPTURE_FAILURE_NONE;
	if (had_capture_or_dice) {
		context->capture_frame_revision += 1;
	}
	pthread_mutex_unlock(&context->mutex);
}

bool kifu_capture_ensure_resources(struct kifu_source *context, uint32_t width, uint32_t height, enum gs_color_format format)
{
	if (context->capture_texrender != NULL && context->capture_stagesurface != NULL &&
	    context->capture_width == width && context->capture_height == height && context->capture_color_format == (uint32_t)format) {
		return true;
	}

	kifu_capture_destroy_resources(context);
	context->capture_texrender = gs_texrender_create(format, GS_ZS_NONE);
	if (context->capture_texrender == NULL) {
		return false;
	}

	context->capture_stagesurface = gs_stagesurface_create(width, height, format);
	if (context->capture_stagesurface == NULL) {
		kifu_capture_destroy_resources(context);
		return false;
	}

	context->capture_width = width;
	context->capture_height = height;
	context->capture_color_format = (uint32_t)format;
	return true;
}

bool kifu_capture_selected_source_frame(struct kifu_source *context,
						const struct kifu_snapshot *snapshot,
						enum kifu_capture_failure_reason *failure_reason)
{
	if (failure_reason != NULL) {
		*failure_reason = KIFU_CAPTURE_FAILURE_NONE;
	}

	if (snapshot->capture_source == NULL || snapshot->capture_source[0] == '\0') {
		if (failure_reason != NULL) {
			*failure_reason = KIFU_CAPTURE_FAILURE_NOT_SELECTED;
		}
		return false;
	}

	obs_source_t *selected_source = obs_get_source_by_name(snapshot->capture_source);
	if (selected_source == NULL) {
		if (failure_reason != NULL) {
			*failure_reason = KIFU_CAPTURE_FAILURE_SOURCE_NOT_FOUND;
		}
		return false;
	}

	if (selected_source == context->source) {
		obs_source_release(selected_source);
		if (failure_reason != NULL) {
			*failure_reason = KIFU_CAPTURE_FAILURE_SOURCE_SELF;
		}
		return false;
	}

	const uint32_t source_width = obs_source_get_width(selected_source);
	const uint32_t source_height = obs_source_get_height(selected_source);
	if (source_width == 0U || source_height == 0U) {
		obs_source_release(selected_source);
		if (failure_reason != NULL) {
			*failure_reason = KIFU_CAPTURE_FAILURE_SOURCE_SIZE_ZERO;
		}
		return false;
	}

	/* Keep overlay dimensions independent from backend inference capture resolution. */
	const uint32_t width = KIFU_CAPTURE_FRAME_WIDTH;
	const float source_aspect = (float)source_height / (float)source_width;
	uint32_t height = (uint32_t)lroundf((float)width * source_aspect);
	if (height == 0U) {
		height = 1U;
	}

	const enum gs_color_space space = GS_CS_SRGB;
	const enum gs_color_format format = gs_get_format_from_space(space);
	if (!kifu_capture_ensure_resources(context, width, height, format)) {
		obs_source_release(selected_source);
		if (failure_reason != NULL) {
			*failure_reason = KIFU_CAPTURE_FAILURE_RESOURCE_INIT;
		}
		return false;
	}

	if (!gs_texrender_begin_with_color_space(context->capture_texrender, width, height, space)) {
		/* Recover from stale graphics resources after source deactivate/activate. */
		kifu_capture_destroy_resources(context);
		if (!kifu_capture_ensure_resources(context, width, height, format) ||
		    !gs_texrender_begin_with_color_space(context->capture_texrender, width, height, space)) {
			obs_source_release(selected_source);
			if (failure_reason != NULL) {
				*failure_reason = KIFU_CAPTURE_FAILURE_TEXRENDER_BEGIN;
			}
			return false;
		}
	}

	struct vec4 clear_color;
	vec4_zero(&clear_color);
	gs_clear(GS_CLEAR_COLOR, &clear_color, 0.0F, 0U);
	gs_ortho(0.0F, (float)width, 0.0F, (float)height, -100.0F, 100.0F);

	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
	gs_matrix_push();
	gs_matrix_scale3f((float)width / (float)source_width, (float)height / (float)source_height, 1.0F);
	obs_source_inc_showing(selected_source);
	obs_source_video_render(selected_source);
	obs_source_dec_showing(selected_source);
	gs_matrix_pop();
	gs_blend_state_pop();
	gs_texrender_end(context->capture_texrender);

	gs_stage_texture(context->capture_stagesurface, gs_texrender_get_texture(context->capture_texrender));

	uint8_t *mapped = NULL;
	uint32_t linesize = 0;
	if (!gs_stagesurface_map(context->capture_stagesurface, &mapped, &linesize)) {
		obs_source_release(selected_source);
		if (failure_reason != NULL) {
			*failure_reason = KIFU_CAPTURE_FAILURE_STAGE_MAP;
		}
		return false;
	}

	const size_t source_row_size = (size_t)width * 4U;
	const size_t frame_size = (size_t)width * (size_t)height * 3U;
	uint8_t *frame_bytes = bzalloc(frame_size);
	if (frame_bytes == NULL) {
		gs_stagesurface_unmap(context->capture_stagesurface);
		obs_source_release(selected_source);
		if (failure_reason != NULL) {
			*failure_reason = KIFU_CAPTURE_FAILURE_FRAME_ALLOC;
		}
		return false;
	}

	for (uint32_t y = 0; y < height; ++y) {
		const uint8_t *source_row = mapped + ((size_t)y * (size_t)linesize);
		uint8_t *dest_row = frame_bytes + ((size_t)y * (size_t)width * 3U);
		for (uint32_t x = 0; x < width; ++x) {
			const size_t source_index = (size_t)x * 4U;
			const size_t dest_index = (size_t)x * 3U;
			dest_row[dest_index + 0U] = source_row[source_index + 0U];
			dest_row[dest_index + 1U] = source_row[source_index + 1U];
			dest_row[dest_index + 2U] = source_row[source_index + 2U];
		}
		(void)source_row_size;
	}

	gs_stagesurface_unmap(context->capture_stagesurface);
	obs_source_release(selected_source);

	pthread_mutex_lock(&context->mutex);
	if (context->capture_frame_bytes != NULL) {
		bfree(context->capture_frame_bytes);
	}
	context->capture_frame_bytes = frame_bytes;
	context->capture_frame_size = frame_size;
	context->capture_frame_revision += 1;
	context->last_capture_failure_reason = KIFU_CAPTURE_FAILURE_NONE;
	pthread_mutex_unlock(&context->mutex);

	return true;
}
