#pragma once

#include <kifu-client.h>

#ifdef __cplusplus
extern "C" {
#endif

enum kifu_capture_failure_reason {
	KIFU_CAPTURE_FAILURE_NONE,
	KIFU_CAPTURE_FAILURE_NOT_SELECTED,
	KIFU_CAPTURE_FAILURE_SOURCE_NOT_FOUND,
	KIFU_CAPTURE_FAILURE_SOURCE_SELF,
	KIFU_CAPTURE_FAILURE_SOURCE_SIZE_ZERO,
	KIFU_CAPTURE_FAILURE_RESOURCE_INIT,
	KIFU_CAPTURE_FAILURE_TEXRENDER_BEGIN,
	KIFU_CAPTURE_FAILURE_STAGE_MAP,
	KIFU_CAPTURE_FAILURE_FRAME_ALLOC,
};

struct kifu_bounding_box kifu_normalize_box_to_pixels(struct kifu_bounding_box box, uint32_t width, uint32_t height);
float kifu_normalize_center_component(float value, uint32_t dimension);
const char *kifu_capture_failure_reason_name(enum kifu_capture_failure_reason reason);

#ifdef __cplusplus
}
#endif
