#pragma once
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#ifdef __cplusplus
extern "C" {
#endif

const XrFrameEndInfo* HeadturnShim_FilterEndFrame(XrSession session, const XrFrameEndInfo* frameEndInfo);

#ifdef __cplusplus
}
#endif
