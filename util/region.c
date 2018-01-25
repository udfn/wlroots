#include <stdlib.h>
#include <math.h>
#include <wlr/util/region.h>

void wlr_region_scale(pixman_region32_t *dst, pixman_region32_t *src,
		float scale) {
	if (scale == 1) {
		pixman_region32_copy(dst, src);
		return;
	}

	int nrects;
	pixman_box32_t *src_rects = pixman_region32_rectangles(src, &nrects);

	pixman_box32_t *dst_rects = malloc(nrects * sizeof(pixman_box32_t));
	if (dst_rects == NULL) {
		return;
	}

	for (int i = 0; i < nrects; ++i) {
		dst_rects[i].x1 = floor(src_rects[i].x1 * scale);
		dst_rects[i].x2 = ceil(src_rects[i].x2 * scale);
		dst_rects[i].y1 = floor(src_rects[i].y1 * scale);
		dst_rects[i].y2 = ceil(src_rects[i].y2 * scale);
	}

	pixman_region32_fini(dst);
	pixman_region32_init_rects(dst, dst_rects, nrects);
}
