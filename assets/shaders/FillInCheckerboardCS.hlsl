#ifndef __FILLINCHECKERBOARDCS_HLSL__
#define __FILLINCHECKERBOARDCS_HLSL__

// ---- Descriptions ----------------------------------------------------------------------------------
// Filters/fills-in invalid values for a checkerboard filled input from neighborhood.
// The compute shader is to be run with (width, height / 2) dimension as
//  it scales Y coordinates by 2 to process only the inactive pixels in the checkerboard filled input.
// ----------------------------------------------------------------------------------------------------

#ifndef HLSL
#define HLSL
#endif

#include "./../../include/HlslCompaction.h"
#include "ShadingHelpers.hlsli"
#include "Rtao.hlsli"

ConstantBuffer<CalcLocalMeanVarianceConstants> cbLocalMeanVar : register(b0);

RWTexture2D<float2> gioLocalMeanVarianceMap	: register(u0);

// Adjust an index in Y coordinate to a same/next pixel that has an invalid value generated for it.
int2 GetInactivePixelIndex(int2 pixel) {
	bool isEvenPixel = ((pixel.x + pixel.y) & 1) == 0;
	return cbLocalMeanVar.EvenPixelActivated == isEvenPixel ? pixel + int2(0, 1) : pixel;
}

[numthreads(DefaultComputeShaderParams::ThreadGroup::Width, DefaultComputeShaderParams::ThreadGroup::Height, 1)]
void CS(uint2 dispatchThreadID : SV_DispatchThreadID) {
	int2 pixel = GetInactivePixelIndex(int2(dispatchThreadID.x, dispatchThreadID.y * 2));

	// Load 4 valid neighbors.
	const int2 offsets[4] = { {-1, 0}, {0, -1}, {1, 0}, {0, 1} };
	float4x2 inValues4x2;
	{
		[unroll]
		for (uint i = 0; i < 4; ++i) {
			inValues4x2[i] = gioLocalMeanVarianceMap[pixel + offsets[i]];
		}
	}

	// Average valid inputs.
	float4 weights = inValues4x2._11_21_31_41 != Rtao::InvalidAOCoefficientValue;
	float weightSum = dot(1, weights);
	float2 filteredValue = weightSum > 0.001f ? mul(weights, inValues4x2) / weightSum : Rtao::InvalidAOCoefficientValue;

	gioLocalMeanVarianceMap[pixel] = filteredValue;
}

#endif // __FILLINCHECKERBOARDCS_HLSL__