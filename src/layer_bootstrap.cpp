// SPDX-FileCopyrightText: 2021-2023 Arthur Brainville (Ybalrid) <ybalrid@ybalrid.info>
//
// SPDX-License-Identifier: MIT
//
// Initial Author: Arthur Brainville <ybalrid@ybalrid.info>

#include "layer_bootstrap.hpp"
#include "layer_shims.hpp"
#include "layer_config.hpp"
#include <cstring>

extern "C" XrResult LAYER_EXPORT XRAPI_CALL xrNegotiateLoaderApiLayerInterface(const XrNegotiateLoaderInfo * loaderInfo, const char* apiLayerName,
	XrNegotiateApiLayerRequest* apiLayerRequest)
{
	if (nullptr == loaderInfo || nullptr == apiLayerRequest || loaderInfo->structType != XR_LOADER_INTERFACE_STRUCT_LOADER_INFO ||
		loaderInfo->structVersion != XR_LOADER_INFO_STRUCT_VERSION || loaderInfo->structSize != sizeof(XrNegotiateLoaderInfo) ||
		apiLayerRequest->structType != XR_LOADER_INTERFACE_STRUCT_API_LAYER_REQUEST ||
		apiLayerRequest->structVersion != XR_API_LAYER_INFO_STRUCT_VERSION ||
		apiLayerRequest->structSize != sizeof(XrNegotiateApiLayerRequest) ||
		loaderInfo->minInterfaceVersion > XR_CURRENT_LOADER_API_LAYER_VERSION ||
		loaderInfo->maxInterfaceVersion < XR_CURRENT_LOADER_API_LAYER_VERSION ||
		loaderInfo->maxInterfaceVersion > XR_CURRENT_LOADER_API_LAYER_VERSION ||
		loaderInfo->maxApiVersion < XR_CURRENT_API_VERSION ||
		loaderInfo->minApiVersion > XR_CURRENT_API_VERSION ||
		0 != strcmp(apiLayerName, XR_THISLAYER_NAME)) {
		return XR_ERROR_INITIALIZATION_FAILED;
	}

	apiLayerRequest->layerInterfaceVersion = XR_CURRENT_LOADER_API_LAYER_VERSION;
	apiLayerRequest->layerApiVersion = XR_CURRENT_API_VERSION;
	apiLayerRequest->getInstanceProcAddr = reinterpret_cast<PFN_xrGetInstanceProcAddr>(thisLayer_xrGetInstanceProcAddr);
	apiLayerRequest->createApiLayerInstance = reinterpret_cast<PFN_xrCreateApiLayerInstance>(thisLayer_xrCreateApiLayerInstance);

	return XR_SUCCESS;
}

XrResult thisLayer_xrCreateApiLayerInstance(const XrInstanceCreateInfo* info, const XrApiLayerCreateInfo* apiLayerInfo,
    XrInstance* instance)
{
    if (nullptr == apiLayerInfo || XR_LOADER_INTERFACE_STRUCT_API_LAYER_CREATE_INFO != apiLayerInfo->structType ||
        XR_API_LAYER_CREATE_INFO_STRUCT_VERSION > apiLayerInfo->structVersion ||
        sizeof(XrApiLayerCreateInfo) > apiLayerInfo->structSize || nullptr == apiLayerInfo->nextInfo ||
        XR_LOADER_INTERFACE_STRUCT_API_LAYER_NEXT_INFO != apiLayerInfo->nextInfo->structType ||
        XR_API_LAYER_NEXT_INFO_STRUCT_VERSION > apiLayerInfo->nextInfo->structVersion ||
        sizeof(XrApiLayerNextInfo) > apiLayerInfo->nextInfo->structSize ||
        0 != strcmp(XR_THISLAYER_NAME, apiLayerInfo->nextInfo->layerName) ||
        nullptr == apiLayerInfo->nextInfo->nextGetInstanceProcAddr ||
        nullptr == apiLayerInfo->nextInfo->nextCreateApiLayerInstance)
    {
        return XR_ERROR_INITIALIZATION_FAILED;
    }

    //Prepare to call this function down the layer chain
    XrApiLayerCreateInfo newApiLayerCreateInfo;
    memcpy(&newApiLayerCreateInfo, apiLayerInfo, sizeof(newApiLayerCreateInfo));
    newApiLayerCreateInfo.nextInfo = apiLayerInfo->nextInfo->next;

    XrInstanceCreateInfo instanceCreateInfo = *info;
    std::vector<const char*> extension_list_without_implemented_extensions;
    std::vector<const char*> enabled_this_layer_extensions;

    //If we deal with extensions, we will check the list of enabled extensions.
    //We remove ours form the list if present, and we store the list of *our* extensions that were enabled
#if 0 // XR_THISLAYER_HAS_EXTENSIONS
    {
        for (size_t enabled_extension_index = 0; enabled_extension_index < instanceCreateInfo.enabledExtensionCount; ++enabled_extension_index)
        {
            if (OpenXRLayer::IsExtensionImplemented(instanceCreateInfo.enabledExtensionNames[enabled_extension_index]))
                enabled_this_layer_extensions.push_back(instanceCreateInfo.enabledExtensionNames[enabled_extension_index]);
            else
                extension_list_without_implemented_extensions.push_back(instanceCreateInfo.enabledExtensionNames[enabled_extension_index]);
        }

        instanceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(extension_list_without_implemented_extensions.size());
        instanceCreateInfo.enabledExtensionNames = extension_list_without_implemented_extensions.data();
        OpenXRLayer::SetEnabledExtensions(enabled_this_layer_extensions);
    }
#endif

    //This is the real "bootstrap" of this layer's
    OpenXRLayer::CreateLayerContext(apiLayerInfo->nextInfo->nextGetInstanceProcAddr, ListShims());

    XrInstance newInstance = *instance;
    const auto result = apiLayerInfo->nextInfo->nextCreateApiLayerInstance(&instanceCreateInfo, &newApiLayerCreateInfo, &newInstance);
    if (XR_FAILED(result))
    {
        return result;
    }

    OpenXRLayer::GetLayerContext().LoadDispatchTable(newInstance);

    *instance = newInstance;
    return XR_SUCCESS;
}

XrResult thisLayer_xrGetInstanceProcAddr(XrInstance instance, const char* name, PFN_xrVoidFunction* function)
{
    return OpenXRLayer::GetLayerContext().GetInstanceProcAddr(instance, name, function);
}

// after the code where the loader's xrGetInstanceProcAddr (often named pfnGetInstanceProcAddr) is set:
//extern "C" void InitializeLayerShims(PFN_xrGetInstanceProcAddr pfnGetInstanceProcAddr);

// initialize shims that need access to the next-layer GetInstanceProcAddr:
//InitializeLayerShims(pfnGetInstanceProcAddr);
