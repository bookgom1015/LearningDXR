#ifndef __GROUNDTRUTHAO_HLSL__
#define __GROUNDTRUTHAO_HLSL__

#ifndef HLSL
#define HLSL
#endif

#include "./../../include/HlslCompaction.h"
#include "Samplers.hlsli"

cbuffer cbRootConstants : register (b0) {
	uint2	gDimension;
	uint	gAccumulation;
};

RWTexture2D<float> gLastAmbientMap		: register(u0);
RWTexture2D<float> gAccumAmbientMap		: register(u1);

[numthreads(DefaultComputeShaderParams::ThreadGroup::Width, DefaultComputeShaderParams::ThreadGroup::Height, 1)]
void CS(uint2 dispatchThreadID : SV_DispatchThreadID) {
	if (dispatchThreadID.x >= gDimension.x || dispatchThreadID.y >= gDimension.y) return;

	float curr = gLastAmbientMap[dispatchThreadID];
	float prev = gAccumAmbientMap[dispatchThreadID];

	float final = (gAccumulation * prev + curr) / (gAccumulation + 1);

	gLastAmbientMap[dispatchThreadID] = final;
	gAccumAmbientMap[dispatchThreadID] = final;
}

#endif // __GROUNDTRUTHAO_HLSL__