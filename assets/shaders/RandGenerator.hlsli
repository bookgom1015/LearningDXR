#ifndef __RANDGENERATOR_HLSLI__
#define __RANDGENERATOR_HLSLI__

// Takes our seed, updates it, and returns a pseudorandom float in [0..1]
float NextRand(inout uint s) {
	s = (1664525u * s + 1013904223u);
	return float(s & 0x00FFFFFF) / float(0x01000000);
}

// Utility function to get a vector perpendicular to an input vector 
//    (from "Efficient Construction of Perpendicular Vectors Without Branching")
float3 PerpendicularVector(float3 u) {
	float3 a = abs(u);
	uint xm = ((a.x - a.y) < 0 && (a.x - a.z) < 0) ? 1 : 0;
	uint ym = (a.y - a.z) < 0 ? (1 ^ xm) : 0;
	uint zm = 1 ^ (xm | ym);
	return cross(u, float3(xm, ym, zm));
}

// Get a cosine-weighted random vector centered around a specified normal direction.
float3 CosHemisphereSample(inout uint seed, float3 hitNorm) {
	// Get 2 random numbers to select our sample with
	float2 randVal = float2(NextRand(seed), NextRand(seed));

	// Cosine weighted hemisphere sample from RNG
	float3 bitangent = PerpendicularVector(hitNorm);
	float3 tangent = cross(bitangent, hitNorm);
	float r = sqrt(randVal.x);
	float phi = 2.0f * 3.14159265f * randVal.y;

	// Get our cosine-weighted hemisphere lobe sample direction
	return tangent * (r * cos(phi).x) + bitangent * (r * sin(phi)) + hitNorm.xyz * sqrt(1 - randVal.x);
}

#endif // __RANDGENERATOR_HLSLI__