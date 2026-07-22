#include "kifu-source-private.h"

#include <plugin-support.h>

#include <math.h>
#include <string.h>

static void destroy_preview_texture_slot(struct kifu_source *context, uint32_t slot)
{
	if (slot >= 2U) {
		return;
	}

	if (context->preview_textures[slot] != NULL) {
		gs_texture_destroy(context->preview_textures[slot]);
		context->preview_textures[slot] = NULL;
	}
	context->preview_texture_width[slot] = 0U;
	context->preview_texture_height[slot] = 0U;
}

void kifu_render_destroy_preview_textures(struct kifu_source *context)
{
	for (uint32_t slot = 0U; slot < 2U; ++slot) {
		destroy_preview_texture_slot(context, slot);
	}
}

void kifu_render_destroy_logo_image(struct kifu_source *context)
{
	if (!context->logo_image_loaded) {
		return;
	}

	gs_image_file_free(&context->logo_image);
	memset(&context->logo_image, 0, sizeof(context->logo_image));
	context->logo_image_loaded = false;
	context->logo_load_attempted = false;
	context->logo_load_warning_logged = false;
}

bool kifu_render_ensure_logo_image_texture(struct kifu_source *context)
{
	if (!context->logo_image_loaded) {
		if (context->logo_load_attempted) {
			return false;
		}
		context->logo_load_attempted = true;

		char *logo_path = obs_module_file("image/kifume.png");
		if (logo_path == NULL) {
			if (!context->logo_load_warning_logged) {
				obs_log(LOG_WARNING, "logo image not found: image/kifume.png");
				context->logo_load_warning_logged = true;
			}
			return false;
		}

		gs_image_file_init(&context->logo_image, logo_path);
		context->logo_image_loaded = true;
		bfree(logo_path);
	}

	if (context->logo_image.texture == NULL) {
		gs_image_file_init_texture(&context->logo_image);
	}

	if (context->logo_image.texture == NULL) {
		if (!context->logo_load_warning_logged) {
			obs_log(LOG_WARNING, "failed to initialize logo texture from image/kifume.png");
			context->logo_load_warning_logged = true;
		}
		return false;
	}

	return true;
}

bool kifu_render_update_logo_visibility_locked(struct kifu_source *context, uint64_t now_ns)
{
	if (context->logo_boot_grace_until_ns > now_ns) {
		context->logo_visible = true;
		context->logo_hidden_since_ns = 0U;
		if (context->logo_visible_since_ns == 0U) {
			context->logo_visible_since_ns = now_ns;
		}
		return true;
	}

	if (!context->logo_has_seen_detection) {
		context->logo_visible = true;
		context->logo_hidden_since_ns = 0U;
		if (context->logo_visible_since_ns == 0U) {
			context->logo_visible_since_ns = now_ns;
		}
		return true;
	}

	if (context->latest_dice_count > 0U) {
		if (context->logo_visible) {
			if (context->logo_visible_since_ns == 0U) {
				context->logo_visible_since_ns = now_ns;
				return true;
			}

			if (now_ns - context->logo_visible_since_ns >= KIFU_LOGO_MIN_VISIBLE_NS) {
				context->logo_visible = false;
				context->logo_visible_since_ns = 0U;
				context->logo_hidden_since_ns = now_ns;
				return false;
			}

			return true;
		}

		if (context->logo_hidden_since_ns == 0U) {
			context->logo_hidden_since_ns = now_ns;
		}
		return false;
	}

	if (!context->logo_visible &&
	    context->logo_hidden_since_ns > 0U &&
	    now_ns - context->logo_hidden_since_ns >= KIFU_LOGO_IDLE_DELAY_NS) {
		context->logo_visible = true;
		context->logo_visible_since_ns = now_ns;
		context->logo_hidden_since_ns = 0U;
	}

	if (context->logo_visible && context->logo_visible_since_ns == 0U) {
		context->logo_visible_since_ns = now_ns;
	}

	return context->logo_visible;
}

void kifu_render_draw_logo(struct kifu_source *context, const struct kifu_snapshot *snapshot, gs_effect_t *effect)
{
	if (snapshot->width == 0U || snapshot->height == 0U) {
		return;
	}

	gs_texture_t *logo_texture = context->logo_image.texture;
	if (logo_texture == NULL) {
		return;
	}

	const uint32_t texture_width = gs_texture_get_width(logo_texture);
	const uint32_t texture_height = gs_texture_get_height(logo_texture);
	if (texture_width == 0U || texture_height == 0U) {
		return;
	}
	const uint32_t draw_x = 0U;
	const uint32_t draw_y = 0U;
	const uint32_t draw_width = snapshot->width;
	const uint32_t draw_height = snapshot->height;
	gs_eparam_t *const image_param = gs_effect_get_param_by_name(effect, "image");
	if (image_param == NULL) {
		return;
	}
	gs_effect_set_texture_srgb(image_param, logo_texture);

	gs_matrix_push();
	gs_matrix_translate3f((float)draw_x, (float)draw_y, 0.0F);
	gs_matrix_scale3f((float)draw_width / (float)texture_width, (float)draw_height / (float)texture_height, 1.0F);
	gs_draw_sprite(logo_texture, 0U, texture_width, texture_height);
	gs_matrix_pop();
}

static gs_texture_t *ensure_preview_texture_slot(struct kifu_source *context,
						 uint32_t slot,
						 uint32_t width,
						 uint32_t height)
{
	if (slot >= 2U || width == 0U || height == 0U) {
		return NULL;
	}

	if (context->preview_textures[slot] != NULL &&
	    context->preview_texture_width[slot] == width &&
	    context->preview_texture_height[slot] == height) {
		return context->preview_textures[slot];
	}

	destroy_preview_texture_slot(context, slot);
	context->preview_textures[slot] = gs_texture_create(width, height, GS_RGBA, 1U, NULL, GS_DYNAMIC);
	if (context->preview_textures[slot] == NULL) {
		return NULL;
	}

	context->preview_texture_width[slot] = width;
	context->preview_texture_height[slot] = height;
	return context->preview_textures[slot];
}

void kifu_render_draw_dice_crops(struct kifu_source *context, const struct kifu_snapshot *snapshot, gs_effect_t *effect)
{
	struct kifu_dice_result dice_to_render[2];
	uint32_t dice_to_render_count = 0U;
	uint32_t inference_width = 0U;
	uint32_t inference_height = 0U;
	uint8_t *inference_frame_bytes = NULL;
	size_t inference_frame_size = 0U;
	uint64_t capture_frame_revision = 0U;
	uint64_t latest_dice_frame_revision = 0U;
	uint64_t inference_frame_revision = 0U;

	pthread_mutex_lock(&context->mutex);
	inference_width = context->inference_frame_width;
	inference_height = context->inference_frame_height;
	capture_frame_revision = context->capture_frame_revision;
	latest_dice_frame_revision = context->latest_dice_frame_revision;
	inference_frame_revision = context->inference_frame_revision;
	dice_to_render_count = context->latest_dice_count;
	if (dice_to_render_count > 2U) {
		dice_to_render_count = 2U;
	}
	for (uint32_t i = 0U; i < dice_to_render_count; ++i) {
		dice_to_render[i] = context->latest_dice[i];
	}
	inference_frame_size = context->inference_frame_size;
	if (context->inference_frame_bytes != NULL && inference_frame_size > 0U) {
		inference_frame_bytes = bzalloc(inference_frame_size);
		if (inference_frame_bytes != NULL) {
			memcpy(inference_frame_bytes, context->inference_frame_bytes, inference_frame_size);
		}
	}
	pthread_mutex_unlock(&context->mutex);

	if (dice_to_render_count == 0U ||
	    inference_width == 0U || inference_height == 0U || inference_frame_bytes == NULL) {
		if (inference_frame_bytes != NULL) {
			bfree(inference_frame_bytes);
		}
		return;
	}

	if (latest_dice_frame_revision == 0U ||
	    inference_frame_revision == 0U ||
	    latest_dice_frame_revision != inference_frame_revision) {
		bfree(inference_frame_bytes);
		return;
	}

	if (inference_frame_size != (size_t)inference_width * (size_t)inference_height * 3U) {
		obs_log(LOG_WARNING,
			"draw skipped: invalid inference frame size (size=%zu, expected=%zu, w=%u, h=%u)",
			inference_frame_size,
			(size_t)inference_width * (size_t)inference_height * 3U,
			inference_width,
			inference_height);
		bfree(inference_frame_bytes);
		return;
	}

	const uint32_t vertical_padding = 8U;
	const uint32_t preview_height = snapshot->height > 220U ? 220U : snapshot->height;
	const uint32_t slot_width = snapshot->width / 2U;
	if (preview_height == 0U) {
		bfree(inference_frame_bytes);
		return;
	}
	if (slot_width == 0U) {
		bfree(inference_frame_bytes);
		return;
	}

	for (uint32_t slot = 0U; slot < dice_to_render_count; ++slot) {
		struct kifu_bounding_box box = kifu_normalize_box_to_pixels(dice_to_render[slot].box, inference_width, inference_height);
		if (box.width <= 1.0F || box.height <= 1.0F) {
			continue;
		}

		float center_x = kifu_normalize_center_component(dice_to_render[slot].center_x, inference_width);
		float center_y = kifu_normalize_center_component(dice_to_render[slot].center_y, inference_height);
		if (!isfinite(center_x) || !isfinite(center_y)) {
			center_x = box.x + (box.width * 0.5F);
			center_y = box.y + (box.height * 0.5F);
		}

		float square_size = fmaxf(box.width, box.height);
		if (!(square_size > 1.0F) || !isfinite(square_size)) {
			continue;
		}
		const float max_square_size = (float)(inference_width < inference_height ? inference_width : inference_height);
		if (square_size > max_square_size) {
			square_size = max_square_size;
		}

		uint32_t square_crop_size = (uint32_t)ceilf(square_size);
		if (square_crop_size == 0U) {
			continue;
		}
		if (square_crop_size > inference_width) {
			square_crop_size = inference_width;
		}
		if (square_crop_size > inference_height) {
			square_crop_size = inference_height;
		}
		if (square_crop_size == 0U) {
			continue;
		}

		if (center_x < 0.0F || center_y < 0.0F ||
		    center_x >= (float)inference_width || center_y >= (float)inference_height) {
			continue;
		}

		int32_t sx = (int32_t)lroundf(center_x - ((float)square_crop_size * 0.5F));
		int32_t sy = (int32_t)lroundf(center_y - ((float)square_crop_size * 0.5F));
		const int32_t max_sx = (int32_t)inference_width - (int32_t)square_crop_size;
		const int32_t max_sy = (int32_t)inference_height - (int32_t)square_crop_size;
		if (sx < 0) {
			sx = 0;
		} else if (sx > max_sx) {
			sx = max_sx;
		}
		if (sy < 0) {
			sy = 0;
		} else if (sy > max_sy) {
			sy = max_sy;
		}

		uint32_t sw = square_crop_size;
		uint32_t sh = square_crop_size;
		if (sw == 0U || sh == 0U) {
			continue;
		}

		const uint32_t dx = slot * slot_width;
		if (dx >= snapshot->width) {
			continue;
		}

		uint32_t draw_size = slot_width;
		if (draw_size == 0U) {
			continue;
		}
		if (draw_size > preview_height) {
			draw_size = preview_height;
		}
		if (draw_size == 0U) {
			continue;
		}

		const uint32_t dy = snapshot->height > (vertical_padding + draw_size)
			? snapshot->height - vertical_padding - draw_size
			: 0U;

		const size_t crop_size = (size_t)sw * (size_t)sh * 4U;
		uint8_t *crop_rgba = bzalloc(crop_size);
		if (crop_rgba == NULL) {
			obs_log(LOG_WARNING,
				"draw dice slot=%u frame_rev=%llu skipped: crop buffer allocation failed (size=%zu)",
				slot,
				(unsigned long long)capture_frame_revision,
				crop_size);
			continue;
		}

		for (uint32_t y = 0; y < sh; ++y) {
			const uint8_t *src_row = inference_frame_bytes + (((size_t)(sy + (int32_t)y) * (size_t)inference_width + (size_t)sx) * 3U);
			uint8_t *dst_row = crop_rgba + ((size_t)y * (size_t)sw * 4U);
			for (uint32_t x = 0; x < sw; ++x) {
				dst_row[(size_t)x * 4U + 0U] = src_row[(size_t)x * 3U + 0U];
				dst_row[(size_t)x * 4U + 1U] = src_row[(size_t)x * 3U + 1U];
				dst_row[(size_t)x * 4U + 2U] = src_row[(size_t)x * 3U + 2U];
				dst_row[(size_t)x * 4U + 3U] = 0xFFU;
			}
		}

		gs_texture_t *crop_texture = ensure_preview_texture_slot(context, slot, sw, sh);
		if (crop_texture != NULL) {
			gs_eparam_t *const image_param = gs_effect_get_param_by_name(effect, "image");
			if (image_param == NULL) {
				bfree(crop_rgba);
				continue;
			}
			gs_effect_set_texture_srgb(image_param, crop_texture);
			gs_texture_set_image(crop_texture, crop_rgba, sw * 4U, false);
			gs_matrix_push();
			gs_matrix_translate3f((float)dx, (float)dy, 0.0F);
			gs_matrix_scale3f((float)draw_size / (float)sw, (float)draw_size / (float)sh, 1.0F);
			gs_draw_sprite(crop_texture, 0U, sw, sh);
			gs_matrix_pop();
		} else {
			obs_log(LOG_WARNING,
				"draw dice slot=%u frame_rev=%llu skipped: preview texture unavailable (sw=%u, sh=%u)",
				slot,
				(unsigned long long)capture_frame_revision,
				sw,
				sh);
		}

		bfree(crop_rgba);
	}

	bfree(inference_frame_bytes);
}
