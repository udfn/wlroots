#ifndef WLR_UTIL_LOG_H
#define WLR_UTIL_LOG_H

#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>

enum wlr_log_importance {
	WLR_SILENT = 0,
	WLR_ERROR = 1,
	WLR_INFO = 2,
	WLR_DEBUG = 3,
	WLR_LOG_IMPORTANCE_LAST,
};

typedef void (*wlr_log_func_t)(enum wlr_log_importance importance,
	const char *fmt, va_list args);

// Will log all messages less than or equal to `verbosity`
// If `callback` is NULL, wlr will use its default logger.
void wlr_log_init(enum wlr_log_importance verbosity, wlr_log_func_t callback);

#ifdef __GNUC__
#define _WLR_ATTRIB_PRINTF(start, end) __attribute__((format(printf, start, end)))
#else
#define _WLR_ATTRIB_PRINTF(start, end)
#endif

void _wlr_log(enum wlr_log_importance verbosity, const char *format, ...) _WLR_ATTRIB_PRINTF(2, 3);
void _wlr_vlog(enum wlr_log_importance verbosity, const char *format, va_list args) _WLR_ATTRIB_PRINTF(2, 0);
const char *_wlr_strip_path(const char *filepath);

#define wlr_log(verb, fmt, ...) \
	_wlr_log(verb, "[%s:%d] " fmt, _wlr_strip_path(__FILE__), __LINE__, ##__VA_ARGS__)

#define wlr_vlog(verb, fmt, args) \
	_wlr_vlog(verb, "[%s:%d] " fmt, _wlr_strip_path(__FILE__), __LINE__, args)

#define wlr_log_errno(verb, fmt, ...) \
	wlr_log(verb, fmt ": %s", ##__VA_ARGS__, strerror(errno))

#endif
