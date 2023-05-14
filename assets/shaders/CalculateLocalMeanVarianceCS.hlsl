#ifndef __CALCULATELOCALMEANVARIANCECS_HLSL__
#define __CALCULATELOCALMEANVARIANCECS_HLSL__

// ---- Descriptions -------------------------------------------------------------------
// Calculate local mean and variance via a separable kernel and using wave intrinsics.
// Requirments:
//  - Wave lane size 16 or higher
//  - WaveReadLaneAt() with any to any to wave read lane support
// Supports:
//  - Up to 9x9 kernels
//  - Checkerboard on/off input. If enabled, outputs only for active pixels.
//     Active pixel is a pixel on the checkerboard pattern and has a valid /
//     generated value for it. The kernel is stretched in y direction
//     to sample only from active pixels.
// -------------------------------------------------------------------------------------

#ifndef HLSL
#define HLSL
#endif

#include "./../../include/HlslCompaction.h"
#include "ShadingHelpers.hlsli"
#include "Rtao.hlsli"

ConstantBuffer<CalcLocalMeanVarianceConstants> cb : register(b0);

Texture2D<float>	gi_Value				: register(t0);
RWTexture2D<float2>	go_LocalMeanVariance	: register(u0);

// Group shared memory cache for the row aggregated results.
groupshared uint PackedRowResultCache[16][8];	// 16bit float valueSum, squared valueSum
groupshared uint NumValuesCache[16][8];

// Adjust an index to a pixel that had a valid value generated for it.
// Inactive pixel indices get increased by 1 in the y direction.
int2 GetActivePixelIndex(int2 pixel) {
	bool isEvenPixel = ((pixel.x + pixel.y) & 1) == 0;
	return cb.CheckerboardSamplingEnabled && cb.EvenPixelActivated != isEvenPixel ? pixel + int2(0, 1) : pixel;
}

// Load up to 16x16 pixels and filter them horizontally.
// The output is cached in shared memory and contains NumRows x 8 results.
void FilterHorizontally(uint2 groupID, uint groupIndex) {
	const uint2 GroupDim = uint2(8, 8);
	const uint NumValuesToLoadPerRowOrColumn = GroupDim.x + (cb.KernelWidth - 1);

	// Processes the thread group as row-major 4x16, where each sub group of 16 threads processes one row.
	// Each thread loads up to 4 values, with the subgroups loading rows interleaved.
	// Loads up to 4x16x4 == 256 input values.
	uint2 groupThreadID4x16_row0 = uint2(groupIndex % 16, groupIndex / 16);
	const int2 KernelBasePixel = (groupID * GroupDim - int(cb.KernelRadius) * int2(1, cb.PixelStepY));
	const uint NumRowsToLoadPerThread = 4;
	const uint RowBaseWaveLaneIndex = (WaveGetLaneIndex() / 16) * 16;

	[unroll]
	for (uint i = 0; i < NumRowsToLoadPerThread; ++i) {
		uint2 groupThreadID4x16 = groupThreadID4x16_row0 + uint2(0, i * 4);
		if (groupThreadID4x16.y >= NumValuesToLoadPerRowOrColumn) {
			if (groupThreadID4x16.x < GroupDim.x) NumValuesCache[groupThreadID4x16.y][groupThreadID4x16.x] = 0;
			break;
		}

		// Load all the contributing columns for each row.
		int2 pixel = GetActivePixelIndex(KernelBasePixel + groupThreadID4x16 * int2(1, cb.PixelStepY));
		float value = Rtao::InvalidAOCoefficientValue;

		// The lane is out of bounds of the GroupDim * kernel, but could be within bounds of the input texture, so don't read it form the texture.
		// However, we need to keep it as an active lane for a below split sum.
		if (groupThreadID4x16.x < NumValuesToLoadPerRowOrColumn && IsWithinBounds(pixel, cb.TextureDim))
			value = gi_Value[pixel];

		// Filter the values for the first GroupDim columns.
		{
			// Accumulate for the whole kernel width.
			float valueSum = 0;
			float squaredValueSum = 0;
			uint numValues = 0;

			// Since a row uses 16 lanes, but we only need to calculate the aggregate for the first half (8) lanes,
			// split the kernel wide aggregation among the first 8 and the second 8 lanes, and then combine them.

			// Initialize the first 8 lanes to the first cell contribution of the kernel.
			// This covers the remainder of 1 in cb.KernelWidth / 2 used in the loop below.
			if (groupThreadID4x16.x < GroupDim.x && value != Rtao::InvalidAOCoefficientValue) {
				valueSum = value;
				squaredValueSum = value * value;
				++numValues;
			}

			// Get the lane index that has the first value for a kernel in this lane.
			uint RowKernelStartLaneIndex = RowBaseWaveLaneIndex + 1 // Skip over the already accumulated firt cell of kernel.
				+ (groupThreadID4x16.x < GroupDim.x ? groupThreadID4x16.x : (groupThreadID4x16.x - GroupDim.x) + cb.KernelRadius);

			for (uint c = 0; c < cb.KernelRadius; ++c) {
				uint laneToReadFrom = RowKernelStartLaneIndex + c;
				float cValue = WaveReadLaneAt(value, laneToReadFrom);
				if (cValue != Rtao::InvalidAOCoefficientValue) {
					valueSum += cValue;
					squaredValueSum += cValue * cValue;
					++numValues;
				}
			}

			// Combine the sub-results.
			uint laneToReadFrom = min(WaveGetLaneCount() - 1, RowBaseWaveLaneIndex + groupThreadID4x16.x + GroupDim.x);
			valueSum += WaveReadLaneAt(valueSum, laneToReadFrom);
			squaredValueSum += WaveReadLaneAt(squaredValueSum, laneToReadFrom);
			numValues += WaveReadLaneAt(numValues, laneToReadFrom);

			// Store only the valid results, i.e. first GroupDim columns.
			if (groupThreadID4x16.x < GroupDim.x) {
				PackedRowResultCache[groupThreadID4x16.y][groupThreadID4x16.x] = Float2ToHalf(float2(valueSum, squaredValueSum));
				NumValuesCache[groupThreadID4x16.y][groupThreadID4x16.x] = numValues;
			}
		}
	}
}

void FilterVertically(uint2 dispatchThreadID, uint2 groupThreadID) {
	float valueSum = 0;
	float squaredValueSum = 0;
	uint numValues = 0;

	uint2 pixel = GetActivePixelIndex(int2(dispatchThreadID.x, dispatchThreadID.y * cb.PixelStepY));

	float4 val1, val2;
	// Accumulate for the whole kernel.
	for (uint r = 0; r < cb.KernelWidth; ++r) {
		uint rowID = groupThreadID.y + r;
		uint rNumValues = NumValuesCache[rowID][groupThreadID.x];

		if (rNumValues > 0) {
			float2 unpackedRowSum = HalfToFloat2(PackedRowResultCache[rowID][groupThreadID.x]);
			float rValueSum = unpackedRowSum.x;
			float rSquaredValueSum = unpackedRowSum.y;

			valueSum += rValueSum;
			squaredValueSum += rSquaredValueSum;
			numValues += rNumValues;
		}
	}

	// Calculate mean and variance.
	float invN = 1.0 / max(numValues, 1);
	float mean = invN * valueSum;

	// Apply Bessel's correction to the estimated variance, multiply by N/N-1,
	// since the true population mean is not known; it is only estimated as the sample mean.
	float besselCorrection = numValues / float(max(numValues, 2) - 1);
	float variance = besselCorrection * (invN * squaredValueSum - mean * mean);

	variance = max(0, variance); // Ensure variance doesn't go negative due to imprecision.

	go_LocalMeanVariance[pixel] = numValues > 0 ? float2(mean, variance) : Rtao::InvalidAOCoefficientValue;
}

[numthreads(DefaultComputeShaderParams::ThreadGroup::Width, DefaultComputeShaderParams::ThreadGroup::Height, 1)]
void CS(uint2 groupID : SV_GroupID, uint2 groupThreadID : SV_GroupThreadID, uint groupIndex : SV_GroupIndex, uint2 dispatchThreadID : SV_DispatchThreadID) {
	FilterHorizontally(groupID, groupIndex);
	GroupMemoryBarrierWithGroupSync();

	FilterVertically(dispatchThreadID, groupThreadID);
}

#endif // __CALCULATELOCALMEANVARIANCECS_HLSL__