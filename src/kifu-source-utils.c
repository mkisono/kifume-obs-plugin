#include "kifu-source-internal.h"

static struct kifu_bounding_box normalize_box_to_pixels_impl(struct kifu_bounding_box box, uint32_t width, uint32_t height)
{
	const float max_dim = box.x + box.y + box.width + box.height;
	if (max_dim <= 4.0F) {
		box.x *= (float)width;
		box.y *= (float)height;
		box.width *= (float)width;
		box.height *= (float)height;
	}

	return box;
}

struct kifu_bounding_box kifu_normalize_box_to_pixels(struct kifu_bounding_box box, uint32_t width, uint32_t height)
{
	return normalize_box_to_pixels_impl(box, width, height);
}

float kifu_normalize_center_component(float value, uint32_t dimension)
{
	if (value >= 0.0F && value <= 1.0F) {
		return value * (float)dimension;
	}

	return value;
}

const char *kifu_capture_failure_reason_name(enum kifu_capture_failure_reason reason)
{
	switch (reason) {
	case KIFU_CAPTURE_FAILURE_NOT_SELECTED:
		return "capture source is not selected";
	case KIFU_CAPTURE_FAILURE_SOURCE_NOT_FOUND:
		return "capture source not found";
	case KIFU_CAPTURE_FAILURE_SOURCE_SELF:
		return "capture source is this plugin source";
	case KIFU_CAPTURE_FAILURE_SOURCE_SIZE_ZERO:
		return "capture source has zero size";
	case KIFU_CAPTURE_FAILURE_RESOURCE_INIT:
		return "capture resource initialization failed";
	case KIFU_CAPTURE_FAILURE_TEXRENDER_BEGIN:
		return "capture render begin failed";
	case KIFU_CAPTURE_FAILURE_STAGE_MAP:
		return "capture stage map failed";
	case KIFU_CAPTURE_FAILURE_FRAME_ALLOC:
		return "capture frame allocation failed";
	case KIFU_CAPTURE_FAILURE_NONE:
	default:
		return "none";
	}
}
