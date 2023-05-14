#pragma once

#include <d3d12.h>
#include <cmath>
#include <algorithm>

#undef max
#undef min

inline UINT CeilDivide(UINT value, UINT divisor) {
	return (value + divisor - 1) / divisor;
}

inline UINT CeilLogWithBase(UINT value, UINT base) {
	return static_cast<UINT>(ceil(log(value) / log(base)));
}

inline float Clamp(float a, float _min, float _max) {
	return std::max(_min, std::min(_max, a));
}

inline float Lerp(float a, float b, float t) {
	return a + t * (b - a);
}

inline float RelativeCoef(float a, float _min, float _max) {
	float _a = Clamp(a, _min, _max);
	return (_a - _min) / (_max - _min);
}

inline UINT NumMantissaBitsInFloatFormat(UINT FloatFormatBitLength) {
	switch (FloatFormatBitLength) {
	case 32: return 23;
	case 16: return 10;
	case 11: return 6;
	case 10: return 5;
	}
	return 0;
}