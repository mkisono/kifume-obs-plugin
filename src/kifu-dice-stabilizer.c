#include "kifu-dice-stabilizer.h"

#include <math.h>
#include <string.h>
#include <stdlib.h>

#define KIFU_DICE_STABILIZER_SLOT_COUNT 2U
#define KIFU_DICE_STABILIZER_RETENTION_NS (2ULL * 1000000000ULL)
#define KIFU_DICE_STABILIZER_JITTER_RATIO 0.12F
#define KIFU_DICE_STABILIZER_SWAP_RATIO 1.0F

struct kifu_dice_track {
	bool valid;
	struct kifu_dice_result result;
	uint64_t last_seen_ns;
};

struct kifu_dice_stabilizer {
	struct kifu_dice_track slots[KIFU_DICE_STABILIZER_SLOT_COUNT];
};

static void clear_track(struct kifu_dice_track *track)
{
	track->valid = false;
	memset(&track->result, 0, sizeof(track->result));
	track->last_seen_ns = 0U;
}

static float result_extent(const struct kifu_dice_result *result)
{
	return fmaxf(result->box.width, result->box.height);
}

static float result_center_x(const struct kifu_dice_result *result)
{
	if (isfinite(result->center_x)) {
		return result->center_x;
	}
	return result->box.x + (result->box.width * 0.5F);
}

static float result_center_y(const struct kifu_dice_result *result)
{
	if (isfinite(result->center_y)) {
		return result->center_y;
	}
	return result->box.y + (result->box.height * 0.5F);
}

static float result_distance_sq(const struct kifu_dice_result *lhs, const struct kifu_dice_result *rhs)
{
	const float dx = result_center_x(lhs) - result_center_x(rhs);
	const float dy = result_center_y(lhs) - result_center_y(rhs);
	return (dx * dx) + (dy * dy);
}

static float jitter_threshold(const struct kifu_dice_result *result)
{
	const float extent = fmaxf(result_extent(result), 0.01F);
	return fmaxf(extent * KIFU_DICE_STABILIZER_JITTER_RATIO, 0.01F);
}

static void stabilize_track(struct kifu_dice_track *track, const struct kifu_dice_result *candidate, uint64_t now_ns)
{
	if (track->valid) {
		const float delta_sq = result_distance_sq(&track->result, candidate);
		const float threshold = jitter_threshold(&track->result);
		if (delta_sq <= (threshold * threshold)) {
			track->last_seen_ns = now_ns;
			return;
		}
	}

	track->result = *candidate;
	track->valid = true;
	track->last_seen_ns = now_ns;
}

static bool detection_is_better(const struct kifu_dice_result *lhs, const struct kifu_dice_result *rhs)
{
	return lhs->confidence > rhs->confidence;
}

static uint32_t select_top_detections(const struct kifu_dice_result *detections,
					   uint32_t detection_count,
					   struct kifu_dice_result *selected,
					   uint32_t selected_capacity)
{
	uint32_t selected_count = 0U;
	for (uint32_t i = 0U; i < detection_count; ++i) {
		const struct kifu_dice_result *candidate = &detections[i];
		if (selected_count < selected_capacity) {
			selected[selected_count++] = *candidate;
			continue;
		}

		uint32_t weakest_index = 0U;
		for (uint32_t j = 1U; j < selected_count; ++j) {
			if (detection_is_better(&selected[weakest_index], &selected[j])) {
				weakest_index = j;
			}
		}

		if (detection_is_better(candidate, &selected[weakest_index])) {
			selected[weakest_index] = *candidate;
		}
	}

	return selected_count;
}

static void order_two_detections(const struct kifu_dice_result *lhs,
					 const struct kifu_dice_result *rhs,
					 struct kifu_dice_result *left,
					 struct kifu_dice_result *right)
{
	const float lhs_extent = result_extent(lhs);
	const float rhs_extent = result_extent(rhs);
	const float size_reference = fmaxf(fmaxf(lhs_extent, rhs_extent), 0.01F);
	const float x_diff = fabsf(result_center_x(lhs) - result_center_x(rhs));

	if (x_diff >= (size_reference * KIFU_DICE_STABILIZER_SWAP_RATIO)) {
		if (result_center_x(lhs) <= result_center_x(rhs)) {
			*left = *lhs;
			*right = *rhs;
		} else {
			*left = *rhs;
			*right = *lhs;
		}
		return;
	}

	if (result_center_y(lhs) <= result_center_y(rhs)) {
		*left = *lhs;
		*right = *rhs;
	} else {
		*left = *rhs;
		*right = *lhs;
	}
}

static void expire_tracks(struct kifu_dice_stabilizer *stabilizer, uint64_t now_ns)
{
	for (uint32_t slot = 0U; slot < KIFU_DICE_STABILIZER_SLOT_COUNT; ++slot) {
		if (stabilizer->slots[slot].valid &&
		    now_ns > stabilizer->slots[slot].last_seen_ns &&
		    now_ns - stabilizer->slots[slot].last_seen_ns > KIFU_DICE_STABILIZER_RETENTION_NS) {
			clear_track(&stabilizer->slots[slot]);
		}
	}
}

struct kifu_dice_stabilizer *kifu_dice_stabilizer_create(void)
{
	return calloc(1U, sizeof(struct kifu_dice_stabilizer));
}

void kifu_dice_stabilizer_destroy(struct kifu_dice_stabilizer *stabilizer)
{
	if (stabilizer != NULL) {
		free(stabilizer);
	}
}

void kifu_dice_stabilizer_reset(struct kifu_dice_stabilizer *stabilizer)
{
	if (stabilizer == NULL) {
		return;
	}

	for (uint32_t slot = 0U; slot < KIFU_DICE_STABILIZER_SLOT_COUNT; ++slot) {
		clear_track(&stabilizer->slots[slot]);
	}
}

uint32_t kifu_dice_stabilizer_update(struct kifu_dice_stabilizer *stabilizer,
					 const struct kifu_dice_result *detections,
					 uint32_t detection_count,
					 uint64_t now_ns,
					 struct kifu_dice_result *out_dice,
					 bool *out_valid,
					 uint32_t out_capacity)
{
	if (stabilizer == NULL || out_dice == NULL || out_capacity == 0U) {
		return 0U;
	}

	expire_tracks(stabilizer, now_ns);

	struct kifu_dice_result selected[2];
	uint32_t selected_count = 0U;
	if (detections != NULL && detection_count > 0U) {
		selected_count = select_top_detections(detections, detection_count, selected, KIFU_DICE_STABILIZER_SLOT_COUNT);
	}

	if (selected_count >= 2U) {
		struct kifu_dice_result left = selected[0];
		struct kifu_dice_result right = selected[1];
		order_two_detections(&selected[0], &selected[1], &left, &right);
		stabilize_track(&stabilizer->slots[0], &left, now_ns);
		stabilize_track(&stabilizer->slots[1], &right, now_ns);
	} else if (selected_count == 1U) {
		uint32_t slot_index = 0U;
		if (stabilizer->slots[0].valid && stabilizer->slots[1].valid) {
			const float distance0 = result_distance_sq(&stabilizer->slots[0].result, &selected[0]);
			const float distance1 = result_distance_sq(&stabilizer->slots[1].result, &selected[0]);
			slot_index = (distance1 < distance0) ? 1U : 0U;
		} else if (!stabilizer->slots[0].valid && stabilizer->slots[1].valid) {
			slot_index = 1U;
		} else if (!stabilizer->slots[0].valid && !stabilizer->slots[1].valid) {
			slot_index = 0U;
		}

		stabilize_track(&stabilizer->slots[slot_index], &selected[0], now_ns);
	}

	uint32_t out_count = 0U;
	for (uint32_t slot = 0U; slot < KIFU_DICE_STABILIZER_SLOT_COUNT && out_count < out_capacity; ++slot) {
		if (stabilizer->slots[slot].valid) {
			if (slot < out_capacity) {
				out_dice[slot] = stabilizer->slots[slot].result;
			}
			if (out_valid != NULL && slot < out_capacity) {
				out_valid[slot] = true;
			}
			out_count += 1U;
		} else if (out_valid != NULL && slot < out_capacity) {
			out_valid[slot] = false;
		}
	}

	return out_count;
}
