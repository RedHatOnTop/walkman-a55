/*
 * Build shim — NOT the real platform cutils/log.h.
 *
 * The real android-5.1.1 cutils/log.h pulls in the whole log/ header tree
 * (log/log.h, log/logd.h, log/log_read.h ...), none of which the NDK ships.
 * This HAL only needs the ALOGx macros, so we map them onto the NDK's stable
 * __android_log_print (liblog) via <android/log.h>. ABI is identical: both
 * resolve to liblog at load time on the device.
 */
#ifndef _NWA55_CUTILS_LOG_SHIM_H
#define _NWA55_CUTILS_LOG_SHIM_H

#include <android/log.h>

#ifndef LOG_TAG
#define LOG_TAG NULL
#endif

#define ALOGV(...) ((void)__android_log_print(ANDROID_LOG_VERBOSE, LOG_TAG, __VA_ARGS__))
#define ALOGD(...) ((void)__android_log_print(ANDROID_LOG_DEBUG,   LOG_TAG, __VA_ARGS__))
#define ALOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO,    LOG_TAG, __VA_ARGS__))
#define ALOGW(...) ((void)__android_log_print(ANDROID_LOG_WARN,    LOG_TAG, __VA_ARGS__))
#define ALOGE(...) ((void)__android_log_print(ANDROID_LOG_ERROR,   LOG_TAG, __VA_ARGS__))

#endif /* _NWA55_CUTILS_LOG_SHIM_H */
