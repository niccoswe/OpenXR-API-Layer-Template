// SPDX-FileCopyrightText: 2021-2023 Arthur Brainville (Ybalrid) <ybalrid@ybalrid.info>
//
// SPDX-License-Identifier: MIT
//
// Initial Author: Arthur Brainville <ybalrid@ybalrid.info>

#include "headturn_shim.hpp"
#include "layer_shims.hpp"

#include <cassert>
#include <iostream>
#include <cmath>
#include <algorithm>

//IMPORTANT: to allow for multiple instance creation/destruction, the contect of the layer must be re-initialized when the instance is being destroyed.
//Hooking xrDestroyInstance is the best way to do that.
XRAPI_ATTR XrResult XRAPI_CALL thisLayer_xrDestroyInstance(
    XrInstance instance)
{
    static PFN_xrDestroyInstance next = nullptr;
    if (!next)
        next = GetNextLayerFunction(xrDestroyInstance);

    const auto result = next(instance);

    OpenXRLayer::DestroyLayerContext();

    return result;
}

//Define the functions implemented in this layer like this:
XRAPI_ATTR XrResult XRAPI_CALL thisLayer_xrEndFrame(XrSession session, const XrFrameEndInfo* frameEndInfo)
{
    static PFN_xrEndFrame next = nullptr;
    if (!next)
        next = GetNextLayerFunction(xrEndFrame);

    const XrFrameEndInfo* adjusted = HeadturnShim_FilterEndFrame(session, frameEndInfo);
    return next ? next(session, adjusted) : XR_ERROR_FUNCTION_UNSUPPORTED;
}

#if XR_THISLAYER_HAS_EXTENSIONS
//The following function doesn't exist in the spec, this is just a test for the extension mecanism
XRAPI_ATTR XrResult XRAPI_CALL thisLayer_xrTestMeTEST(XrSession session)
{
	(void)session;
	std::cout << "xrTestMe()\n";
	return XR_SUCCESS;
}
#endif

//This functions returns the list of function pointers and name we implement, and is called during the initialization of the layer:
std::vector<OpenXRLayer::ShimFunction> ListShims()
{
    std::vector<OpenXRLayer::ShimFunction> functions;
    functions.emplace_back("xrDestroyInstance", PFN_xrVoidFunction(thisLayer_xrDestroyInstance));

    //List every functions that is callable on this API layer
    functions.emplace_back("xrEndFrame", PFN_xrVoidFunction(thisLayer_xrEndFrame));

#if XR_THISLAYER_HAS_EXTENSIONS
    if (OpenXRLayer::IsExtensionEnabled("XR_TEST_test_me"))
        functions.emplace_back("xrTestMeTEST", PFN_xrVoidFunction(thisLayer_xrTestMeTEST));
#endif

    return functions;
}
