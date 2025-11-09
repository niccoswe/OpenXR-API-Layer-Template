#include "headturn_shim.hpp"

#define _USE_MATH_DEFINES
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Read amplification factor from env, default 1.5
static float ReadAmplifyEnv()
{
    const char* env = std::getenv("XR_HEADTURN_AMPLIFY");
    if (!env) return 3.0f; // default amplification increased from 1.5 -> 3.0
    const float v = static_cast<float>(std::atof(env));
    return v > 0.0f ? v : 1.0f;
}

// Ensure unit quaternion (prevents drift from float error)
static XrQuaternionf Normalize(XrQuaternionf q)
{
    const float magSq = q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w;
    if (magSq <= 0.0f) return {0.0f, 0.0f, 0.0f, 1.0f};
    const float inv = 1.0f / std::sqrt(magSq);
    q.x *= inv; q.y *= inv; q.z *= inv; q.w *= inv;
    return q;
}

// Amplify yaw around world-up (Y) without introducing roll/pitch coupling.
// Decompose q = q_yaw * q_tilt, amplify yaw, then recompose q' = q_yaw' * q_tilt.
static XrQuaternionf AmplifyYawDecoupled(const XrQuaternionf& input, float amplify)
{
    auto normalize = [](XrQuaternionf q) {
        const double s = (double)q.x*q.x + (double)q.y*q.y + (double)q.z*q.z + (double)q.w*q.w;
        if (s <= 0.0) return XrQuaternionf{0.f,0.f,0.f,1.f};
        const float inv = (float)(1.0 / std::sqrt(s));
        q.x *= inv; q.y *= inv; q.z *= inv; q.w *= inv;
        return q;
    };
    auto mul = [](const XrQuaternionf& a, const XrQuaternionf& b) {
        return XrQuaternionf{
            a.w*b.x + a.x*b.w + a.y*b.z - a.z*b.y,
            a.w*b.y - a.x*b.z + a.y*b.w + a.z*b.x,
            a.w*b.z + a.x*b.y - a.y*b.x + a.z*b.w,
            a.w*b.w - a.x*b.x - a.y*b.y - a.z*b.z
        };
    };
    auto conj = [](const XrQuaternionf& q) { return XrQuaternionf{-q.x,-q.y,-q.z,q.w}; };
    auto rotate = [&](const XrQuaternionf& q, const XrVector3f& v) {
        const XrQuaternionf vq{v.x, v.y, v.z, 0.f};
        XrQuaternionf qn = normalize(q);
        XrQuaternionf r = mul(mul(qn, vq), conj(qn));
        return XrVector3f{r.x, r.y, r.z};
    };
    auto yawAroundY = [](double angle) {
        const double h = angle * 0.5;
        return XrQuaternionf{0.f, (float)std::sin(h), 0.f, (float)std::cos(h)};
    };

    // Normalize input
    XrQuaternionf q = normalize(input);

    // Derive heading from forward vector projected to XZ plane
    const XrVector3f fwdWorld = rotate(q, XrVector3f{0.f, 0.f, -1.f}); // OpenXR forward is -Z
    const double fx = fwdWorld.x, fz = fwdWorld.z;
    const double lenXZ = std::sqrt(fx*fx + fz*fz);
    if (lenXZ < 1e-6)
        return q; // near vertical: undefined heading

    const double yaw = std::atan2(fx, -fz); // 0 when facing -Z, + to +X (right)

    // Factor q as q = q_tilt * q_yaw (tilt first, then heading)
    const XrQuaternionf q_yaw = yawAroundY(yaw);
    const XrQuaternionf q_tilt = mul(q, conj(q_yaw)); // remove yaw on the right

    // Amplify only yaw and recompose: q' = q_tilt * q_yaw'
    const XrQuaternionf q_yaw_amp = yawAroundY(yaw * (double)amplify);
    XrQuaternionf out = mul(q_tilt, q_yaw_amp);

    return normalize(out);
}

struct ProjectionLayerCopy
{
    XrCompositionLayerProjection layer{};
    std::vector<XrCompositionLayerProjectionView> views;
};

// Clone projection layers, adjust only view pose orientation (yaw), and return a modified frameEndInfo
const XrFrameEndInfo* HeadturnShim_FilterEndFrame(XrSession, const XrFrameEndInfo* frameEndInfo)
{
    if (!frameEndInfo || frameEndInfo->layerCount == 0)
        return frameEndInfo;

    const float amplify = ReadAmplifyEnv();
    if (std::abs(amplify - 1.0f) < 1e-3f)
        return frameEndInfo;

    static thread_local std::vector<ProjectionLayerCopy> projectionCopies;
    static thread_local std::vector<const XrCompositionLayerBaseHeader*> layerPtrs;
    static thread_local XrFrameEndInfo modified;

    projectionCopies.clear();
    projectionCopies.reserve(frameEndInfo->layerCount);

    layerPtrs.clear();
    layerPtrs.reserve(frameEndInfo->layerCount);

    for (uint32_t i = 0; i < frameEndInfo->layerCount; ++i)
    {
        const XrCompositionLayerBaseHeader* base = frameEndInfo->layers[i];
        if (!base) continue;

        if (base->type != XR_TYPE_COMPOSITION_LAYER_PROJECTION)
        {
            layerPtrs.push_back(base);
            continue;
        }

        const auto* src = reinterpret_cast<const XrCompositionLayerProjection*>(base);

        projectionCopies.emplace_back();
        auto& copy = projectionCopies.back();

        copy.layer = *src;
        copy.views.assign(src->views, src->views + src->viewCount);

        for (auto& v : copy.views)
            v.pose.orientation = AmplifyYawDecoupled(v.pose.orientation, amplify);

        copy.layer.views = copy.views.data();

        layerPtrs.push_back(reinterpret_cast<const XrCompositionLayerBaseHeader*>(&copy.layer));
    }

    modified = *frameEndInfo;
    modified.layerCount = static_cast<uint32_t>(layerPtrs.size());
    modified.layers = layerPtrs.data();
    return &modified;
}