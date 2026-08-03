#pragma once

#include "kifu-source-state.h"
#include "kifu-dice-stabilizer.h"
#include "kifu-logo-state-machine.h"

void kifu_render_destroy_preview_textures(struct kifu_source *context);
void kifu_render_destroy_logo_image(struct kifu_source *context);
bool kifu_render_ensure_logo_image_texture(struct kifu_source *context);
bool kifu_render_update_logo_visibility_locked(struct kifu_source *context, uint64_t now_ns);
float kifu_render_logo_opacity_locked(const struct kifu_source *context, uint64_t now_ns);
bool kifu_render_draw_logo(struct kifu_source *context,
				   const struct kifu_snapshot *snapshot,
				   gs_effect_t *effect,
				   float opacity);
void kifu_render_draw_dice_crops(struct kifu_source *context, const struct kifu_snapshot *snapshot, gs_effect_t *effect);

void kifu_capture_destroy_resources(struct kifu_source *context);
void kifu_capture_clear_frame(struct kifu_source *context);
bool kifu_capture_selected_source_frame(struct kifu_source *context,
						const struct kifu_snapshot *snapshot,
						enum kifu_capture_failure_reason *failure_reason);

char *dup_or_empty(const char *value);
void free_snapshot_strings(struct kifu_snapshot *snapshot);
void clear_latest_dice_locked(struct kifu_source *context);
void clear_inference_frame_locked(struct kifu_source *context);

void kifu_backend_free_client(struct kifu_source *context);
void kifu_backend_start_worker(struct kifu_source *context);
void kifu_backend_stop_worker(struct kifu_source *context);
