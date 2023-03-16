#ifndef __TEMPORALSUPERSAMPLINGBLENDWIDTHCURRENTFRAME_HLSL__
#define __TEMPORALSUPERSAMPLINGBLENDWIDTHCURRENTFRAME_HLSL__

#ifndef HLSL
#define HLSL
#endif

#include "./../../include/HlslCompaction.h"
#include "ShadingHelpers.hlsli"
#include "Samplers.hlsli"
#include "Rtao.hlsli"

ConstantBuffer<TemporalSupersamplingBlendWithCurrentFrameConstants> cbBlendCurrFrame : register(b0);

Texture2D<float> gAmbientCoefficientMap										: register(t0);
Texture2D<float2> gLocalMeanVarianceMap										: register(t1);
Texture2D<float> gRayHitDistanceMap											: register(t2);
Texture2D<uint4> gReprojectedTsppCoefficientSquaredMeanRayHitDistanceMap	: register(t3);

RWTexture2D<float> gioTemporalAOCoefficientMap	: register(u0);
RWTexture2D<uint> gioTsppMap					: register(u1);
RWTexture2D<float> gioSquaredMeanMap			: register(u2);
RWTexture2D<float> gioRayHitDistanceMap			: register(u3);
RWTexture2D<float> goVarianceMap				: register(u4);
RWTexture2D<float> goBlurStrength				: register(u5);

[numthreads(DefaultComputeShaderParams::ThreadGroup::Width, DefaultComputeShaderParams::ThreadGroup::Height, 1)]
void CS(uint2 dispatchThreadID : SV_DispatchThreadID) {

}

#endif // __TEMPORALSUPERSAMPLINGBLENDWIDTHCURRENTFRAME_HLSL__