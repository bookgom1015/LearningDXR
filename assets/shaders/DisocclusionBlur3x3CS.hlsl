#ifndef __DISOCCLUSIONBLUR3X3CS_HLSL__
#define __DISOCCLUSIONBLUR3X3CS_HLSL__

#ifndef HLSL
#define HLSL
#endif

#include "./../../include/HlslCompaction.h"
#include "ShadingHelpers.hlsli"
#include "Rtao.hlsli"

#define GAUSSIAN_KERNEL_3X3
#include "Kernels.hlsli"

cbuffer cbRootConstants : register(b0) {
	uint2	gTextureDim;
	uint	gStep;
};

Texture2D<float> gi_Depth			: register(t0);
Texture2D<float> gi_BlurStrength	: register(t1);

RWTexture2D<float> gio_Value		: register(u0);

// Group shared memory cache for the row aggregated results.
static const uint NumValuesToLoadPerRowOrColumn =
	DefaultComputeShaderParams::ThreadGroup::Width 
	+ (FilterKernel::Width - 1);
groupshared uint PackedValueDepthCache[NumValuesToLoadPerRowOrColumn][8]; // 16bit float value, depth.
groupshared float FilteredResultCache[NumValuesToLoadPerRowOrColumn][8];	// 32bit float filtered value.

// Find a dispatchThreadID with steps in between the group threads and groups interleaved to cover all pixels.
uint2 GetPixelIndex(uint2 groupID, uint2 groupThreadID) {
	const uint2 GroupDim = uint2(8, 8);
	uint2 groupBase = (groupID / gStep) * GroupDim * gStep + groupID % gStep;
	uint2 groupThreadOffset = groupThreadID * gStep;
	uint2 sDispatchThreadID = groupBase + groupThreadOffset;
	return sDispatchThreadID;
}

bool IsInRange(float val, float min, float max) {
	return (val >= min && val <= max);
}

// Load up to 16x16 pixels and filter them horizontally.
// The output is cached in Shared Memory and constants NumRows x 8 results.
void FilterHorizontally(uint2 groupID, uint groupIndex) {
	const uint2 GroupDim = uint2(8, 8);

	// Processes the thread group as row-major 4x16, where each sub group of 16 threads processes one row.
	// Each thread loads up to 4 values, with the sub groups loading rows interleaved.
	// Loads up to 4x16x4 == 256 input values.
	const uint2 GroupThreadID4x16_row0 = uint2(groupIndex % 16, groupIndex / 16);
	const int2 GroupKernelBasePixel = GetPixelIndex(groupID, 0) - int(FilterKernel::Radius * gStep);
	const uint NumRowsToLoadPerThread = 4;
	const uint RowBaseWaveLaneIndex = (WaveGetLaneIndex() / 16) * 16;

	[unroll]
	for (uint i = 0; i < NumRowsToLoadPerThread; ++i) {
		uint2 groupThreadID4x16 = GroupThreadID4x16_row0 + uint2(0, i * 4);
		if (groupThreadID4x16.y >= NumValuesToLoadPerRowOrColumn) break;

		// Load all the contributing columns for each row.
		int2 pixel = GroupKernelBasePixel + groupThreadID4x16 * gStep;
		float value = Rtao::InvalidAOCoefficientValue;
		float depth = 0;

		// The lane is out of bounds of the GroupDim + kernel, but could be within bounds of the input texture,
		//  so don't read it from the texture.
		// However, we need to keep it as an active lane for a below split sum.
		if (groupThreadID4x16.x < NumValuesToLoadPerRowOrColumn && IsWithinBounds(pixel, gTextureDim)) {
			value = gio_Value[pixel];
			depth = gi_Depth[pixel];
		}

		// Cache the kernel center values.
		if (IsInRange(groupThreadID4x16.x, FilterKernel::Radius, FilterKernel::Radius + GroupDim.x - 1)) {
			PackedValueDepthCache[groupThreadID4x16.y][groupThreadID4x16.x - FilterKernel::Radius] = Float2ToHalf(float2(value, depth));
		}

		// Filter the values for the first GroupDim columns.
		{
			// Accumulate for the whole kernel width.
			float weightedValueSum = 0;
			float weightSum = 0;
			float gaussianWeightedValueSum = 0;
			float gaussianWeightedSum = 0;

			// Since a row uses 16 lanes, but we only need to calculate the aggregate for the first half (8) lanes,
			//  split the kernel wide aggregation among the first 8 and the second 8 lanes, and then combine them.

			// Get the lane index that has the first value for a kernel in this lane.
			uint RowKernelStartLaneIndex =
				(RowBaseWaveLaneIndex + groupThreadID4x16.x)
				- (groupThreadID4x16.x < GroupDim.x ? 0 : GroupDim.x);

			// Get values for the kernel center.
			uint kcLaneIndex = RowKernelStartLaneIndex + FilterKernel::Radius;
			float kcValue = WaveReadLaneAt(value, kcLaneIndex);
			float kcDepth = WaveReadLaneAt(depth, kcLaneIndex);

			// Initialize the first 8 lanes to the center cell contribution of the kernel.
			// This covers the remainder of 1 in FilterKernel::Width / 2 used in the loop below.
			if (groupThreadID4x16.x < GroupDim.x && kcValue != Rtao::InvalidAOCoefficientValue && kcDepth != 1) {
				float w_h = FilterKernel::Kernel1D[FilterKernel::Radius];
				gaussianWeightedValueSum = w_h * kcValue;
				gaussianWeightedSum = w_h;
				weightedValueSum = gaussianWeightedSum;
				weightSum = w_h;
			}

			// Second 8 lanes start just past the kernel center.
			uint KernelCellIndexOffset = groupThreadID4x16.x < GroupDim.x ? 
				0 : (FilterKernel::Radius + 1); // Skip over the already accumulated center cell of the kernel.

			// For all columns in the kernel.
			for (uint c = 0; c < FilterKernel::Radius; ++c) {
				uint kernelCellIndex = KernelCellIndexOffset + c;

				uint laneToReadFrom = RowKernelStartLaneIndex + kernelCellIndex;
				float cValue = WaveReadLaneAt(value, laneToReadFrom);
				float cDepth = WaveReadLaneAt(depth, laneToReadFrom);

				if (cValue != Rtao::InvalidAOCoefficientValue && kcDepth != 1 && cDepth != 1) {
					float w_h = FilterKernel::Kernel1D[kernelCellIndex];

					// Simple depth test with tolerance growing as the kernel radius increases.
					// Goal is to prevent values too far apart to blend together, while having
					//  the test being relaxed enough to get a strong blurring result.
					float depthThreshold = 0.05 + gStep * 0.001 * abs(int(FilterKernel::Radius) - c);
					float w_d = abs(kcDepth - cDepth) <= depthThreshold * kcDepth;
					float w = w_h * w_d;

					weightedValueSum += w * cValue;
					weightSum += w;
					gaussianWeightedValueSum += w_h * cValue;
					gaussianWeightedSum += w_h;
				}
			}

			// Combine the sub-results.
			uint laneToReadFrom = min(WaveGetLaneCount() - 1, RowBaseWaveLaneIndex + groupThreadID4x16.x + GroupDim.x);
			weightedValueSum += WaveReadLaneAt(weightedValueSum, laneToReadFrom);
			weightSum += WaveReadLaneAt(weightSum, laneToReadFrom);
			gaussianWeightedValueSum += WaveReadLaneAt(gaussianWeightedValueSum, laneToReadFrom);
			gaussianWeightedSum += WaveReadLaneAt(gaussianWeightedSum, laneToReadFrom);

			// Store only the valid results, i.e. first GroupDim columns.
			if (groupThreadID4x16.x < GroupDim.x) {
				float gaussianFilteredValue = gaussianWeightedSum > 1e-6 ? gaussianWeightedValueSum / gaussianWeightedSum : Rtao::InvalidAOCoefficientValue;
				float filteredValue = weightSum > 1e-6 ? weightedValueSum / weightSum : gaussianFilteredValue;

				FilteredResultCache[groupThreadID4x16.y][groupThreadID4x16.x] = filteredValue;
			}
		}
	}
}

void FilterVertically(uint2 dispatchThreadID, uint2 groupThreadID, float blurStrength) {
	// Kernel center values.
	float2 kcValueDepth = HalfToFloat2(PackedValueDepthCache[groupThreadID.y + FilterKernel::Radius][groupThreadID.x]);
	float kcValue = kcValueDepth.x;
	float kcDepth = kcValueDepth.y;

	float filteredValue = kcValue;
	if (blurStrength >= 0.01 && kcDepth != 1) {
		float weightedValueSum = 0;
		float weightSum = 0;
		float gaussianWeightedValueSum = 0;
		float gaussianWeightSum = 0;

		// For all rows in the kernel.
		[unroll]
		for (uint r = 0; r < FilterKernel::Width; ++r) {
			uint rowID = groupThreadID.y + r;

			float2 rUnpackedValueDepth = HalfToFloat2(PackedValueDepthCache[rowID][groupThreadID.x]);
			float rDepth = rUnpackedValueDepth.y;
			float rFilteredValue = FilteredResultCache[rowID][groupThreadID.x];

			if (rDepth != 1 && rFilteredValue != Rtao::InvalidAOCoefficientValue) {
				float w_h = FilterKernel::Kernel1D[r];

				// Simple depth test with tolerance growing as the kernel radius increases.
				// Goal is to prevent values too far apart to blend together, while having 
				// the test being relaxed enough to get a strong blurring result.
				float depthThreshold = 0.05 + gStep * 0.001 * abs(int(FilterKernel::Radius) - int(r));
				float w_d = abs(kcDepth - rDepth) <= depthThreshold * kcDepth;
				float w = w_h * w_d;

				weightedValueSum += w * rFilteredValue;
				weightSum += w;
				gaussianWeightedValueSum += w_h * rFilteredValue;
				gaussianWeightSum += w_h;
			}
		}
		float gaussianFilteredValue = gaussianWeightSum > 1e-6 ? gaussianWeightedValueSum / gaussianWeightSum : Rtao::InvalidAOCoefficientValue;
		filteredValue = weightSum > 1e-6 ? weightedValueSum / weightSum : gaussianFilteredValue;
		filteredValue = filteredValue != Rtao::InvalidAOCoefficientValue ? lerp(kcValue, filteredValue, blurStrength) : filteredValue;
	}
	gio_Value[dispatchThreadID] = filteredValue;
}

[numthreads(DefaultComputeShaderParams::ThreadGroup::Width, DefaultComputeShaderParams::ThreadGroup::Height, 1)]
void CS(uint2 groupID : SV_GroupID, uint2 groupThreadID : SV_GroupThreadID, uint groupIndex : SV_GroupIndex) {
	uint2 sDispatchThreadID = GetPixelIndex(groupID, groupThreadID);
	// Pass through if all pixels have 0 blur strength set.
	float blurStrength;
	{
		if (groupIndex == 0) FilteredResultCache[0][0] = 0;
		GroupMemoryBarrierWithGroupSync();

		blurStrength = gi_BlurStrength[sDispatchThreadID];

		const float MinBlurStrength = 0.01;
		bool valueNeedsFiltering = blurStrength >= MinBlurStrength;
		if (valueNeedsFiltering) FilteredResultCache[0][0] = 1;

		GroupMemoryBarrierWithGroupSync();

		if (FilteredResultCache[0][0] == 0) return;
	}

	FilterHorizontally(groupID, groupIndex);
	GroupMemoryBarrierWithGroupSync();

	FilterVertically(sDispatchThreadID, groupThreadID, blurStrength);
}

#endif // __DISOCCLUSIONBLUR3X3CS_HLSL__