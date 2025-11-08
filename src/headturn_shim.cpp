#include "headturn_shim.hpp"

#define _USE_MATH_DEFINES
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static float ReadAmplifyEnv()
{
    const char* env = std::getenv("XR_HEADTURN_AMPLIFY");
    if (!env) return 1.5f;
    const float v = static_cast<float>(std::atof(env));
    return v > 0.0f ? v : 1.0f;
}

static XrQuaternionf Normalize(XrQuaternionf q)
{
    const float magSq = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
    if (magSq <= 0.0f)
        return {0.0f, 0.0f, 0.0f, 1.0f};
    const float invMag = 1.0f / std::sqrt(magSq);
    q.x *= invMag;
    q.y *= invMag;
    q.z *= invMag;
    q.w *= invMag;
    return q;
}

static XrQuaternionf AmplifyYaw(const XrQuaternionf& input, float amplify)
{
    XrQuaternionf q = Normalize(input);
    const double x = q.x, y = q.y, z = q.z, w = q.w;

    const double siny_cosp = 2.0 * (w * y + z * x);
    const double cosy_cosp = 1.0 - 2.0 * (y * y + z * z);
    double yaw = std::atan2(siny_cosp, cosy_cosp);

    const double sinp = 2.0 * (w * x - y * z);
    const double clampSinp = std::clamp(sinp, -1.0, 1.0);
    const double pitch = std::abs(clampSinp) >= 1.0 ? std::copysign(M_PI / 2.0, clampSinp) : std::asin(clampSinp);

    const double sinr_cosp = 2.0 * (w * z + x * y);
    const double cosr_cosp = 1.0 - 2.0 * (z * z + x * x);
    const double roll = std::atan2(sinr_cosp, cosr_cosp);

    yaw *= static_cast<double>(amplify);

    const double cy = std::cos(yaw * 0.5);
    const double sy = std::sin(yaw * 0.5);
    const double cp = std::cos(pitch * 0.5);
    const double sp = std::sin(pitch * 0.5);
    const double cr = std::cos(roll * 0.5);
    const double sr = std::sin(roll * 0.5);

    q.w = static_cast<float>(cr * cp * cy + sr * sp * sy);
    q.x = static_cast<float>(sr * cp * cy - cr * sp * sy);
    q.y = static_cast<float>(cr * sp * cy + sr * cp * sy);
    q.z = static_cast<float>(cr * cp * sy - sr * sp * cy);
    return Normalize(q);
}

struct ProjectionLayerCopy
{
    XrCompositionLayerProjection layer{};
    std::vector<XrCompositionLayerProjectionView> views;
};

const XrFrameEndInfo* HeadturnShim_FilterEndFrame(XrSession, const XrFrameEndInfo* frameEndInfo)
{
    if (!frameEndInfo || frameEndInfo->layerCount == 0)
        return frameEndInfo;

    const float amplify = ReadAmplifyEnv();
    if (std::abs(amplify - 1.0f) < 1e-3f)
        return frameEndInfo;

    static thread_local std::vector<ProjectionLayerCopy> projectionCopies;
    static thread_local std::vector<const XrCompositionLayerBaseHeader*> layerPointers;
    static thread_local XrFrameEndInfo modifiedFrameEndInfo;

    projectionCopies.clear();
    projectionCopies.reserve(frameEndInfo->layerCount);

    layerPointers.clear();
    layerPointers.reserve(frameEndInfo->layerCount);

    for (uint32_t layerIndex = 0; layerIndex < frameEndInfo->layerCount; ++layerIndex)
    {
        const XrCompositionLayerBaseHeader* base = frameEndInfo->layers[layerIndex];
        if (!base)
            continue;

        if (base->type != XR_TYPE_COMPOSITION_LAYER_PROJECTION)
        {
            layerPointers.push_back(base);
            continue;
        }

        const auto* originalProjection =
            reinterpret_cast<const XrCompositionLayerProjection*>(base);

        projectionCopies.emplace_back();
        ProjectionLayerCopy& copy = projectionCopies.back();
        copy.layer = *originalProjection;
        copy.views.assign(originalProjection->views,
                          originalProjection->views + originalProjection->viewCount);

        for (auto& view : copy.views)
            view.pose.orientation = AmplifyYaw(view.pose.orientation, amplify);

        copy.layer.views = copy.views.data();

        layerPointers.push_back(
            reinterpret_cast<const XrCompositionLayerBaseHeader*>(&copy.layer));
    }

    modifiedFrameEndInfo = *frameEndInfo;
    modifiedFrameEndInfo.layerCount = static_cast<uint32_t>(layerPointers.size());
    modifiedFrameEndInfo.layers = layerPointers.data();

    return &modifiedFrameEndInfo;
}