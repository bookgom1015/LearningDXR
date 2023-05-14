#ifndef __RTAO_HLSLI__
#define __RTAO_HLSLI__

namespace Rtao {
	static const float RayHitDistanceOnMiss = 0;
	static const float InvalidAOCoefficientValue = -1;
	bool HasAORayHitAnyGeometry(float tHit) {
		return tHit != RayHitDistanceOnMiss;
	}
}

#endif // __RTAO_HLSLI__