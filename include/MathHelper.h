//***************************************************************************************
// MathHelper.h by Frank Luna (C) 2011 All Rights Reserved.
//
// Helper math class.
//***************************************************************************************

#pragma once

#include <cmath>
#include <DirectXMath.h>
#include <DirectXPackedVector.h>

class MathHelper {
public:
	static float Sin(float t) {
		return sinf(t);
	}

	static float ASin(float t) {
		return asinf(t);
	}

	static float Cos(float t) {
		return cosf(t);
	}

	static float ACos(float t) {
		return acosf(t);
	}

	static float Tan(float t) {
		return tanf(t);
	}

	static float ATan2(float x, float y) {
		return atan2f(y, x);
	}

	static float DegreesToRadians(float degrees) {
		return degrees * DegToRad;
	}

	static float RadiansToDegrees(float radians) {
		return radians * RadToDeg;
	}

	// Returns random float in [0, 1).
	static float RandF() {
		return (float)(rand()) / (float)RAND_MAX;
	}

	// Returns random float in [a, b).
	static float RandF(float a, float b) {
		return a + RandF()*(b-a);
	}

    static int Rand(int a, int b) {
        return a + rand() % ((b - a) + 1);
    }

	template<typename T>
	static T Min(const T& a, const T& b) {
		return a < b ? a : b;
	}

	template<typename T>
	static T Max(const T& a, const T& b) {
		return a > b ? a : b;
	}
	 
	template<typename T>
	static T Lerp(const T& a, const T& b, float t) {
		return a + (b-a)*t;
	}

	template<typename T>
	static T Clamp(const T& x, const T& low, const T& high) {
		return x < low ? low : (x > high ? high : x); 
	}

	static float Abs(float param) {
		return (float)fabs(param);
	}

	static bool IsZero(float value) {
		return value * value < Epsilon * Epsilon;
	}

	inline bool IsNotZero(float value) {
		return !IsZero(value);
	}

	static bool IsEqual(float a, float b) {
		return Abs(a - b) < Epsilon;
	}

	static bool IsNotEqual(float a, float b) {
		return Abs(a - b) > Epsilon;
	}

	static bool IsEqual(const DirectX::XMFLOAT2& lhs, const DirectX::XMFLOAT2& rhs) {
		return IsEqual(lhs.x, rhs.x) && IsEqual(lhs.y, rhs.y);
	}

	static bool IsNotEqual(const DirectX::XMFLOAT2& lhs, const DirectX::XMFLOAT2& rhs) {
		return IsNotEqual(lhs, rhs);
	}

	static bool IsEqual(const DirectX::XMFLOAT3& lhs, const DirectX::XMFLOAT3& rhs) {
		return IsEqual(lhs.x, rhs.x) && IsEqual(lhs.y, rhs.y) && IsEqual(lhs.z, rhs.z);
	}

	static bool IsNotEqual(const DirectX::XMFLOAT3& lhs, const DirectX::XMFLOAT3& rhs) {
		return IsNotEqual(lhs, rhs);
	}

	static bool IsEqual(const DirectX::XMFLOAT4& lhs, const DirectX::XMFLOAT4& rhs) {
		return IsEqual(lhs.x, rhs.x) && IsEqual(lhs.y, rhs.y) && IsEqual(lhs.z, rhs.z) && IsEqual(lhs.w, rhs.w);
	}

	static bool IsNotEqual(const DirectX::XMFLOAT4& lhs, const DirectX::XMFLOAT4& rhs) {
		return IsNotEqual(lhs, rhs);
	}

	// Returns the polar angle of the point (x,y) in [0, 2*PI).
	static float AngleFromXY(float x, float y);

	static DirectX::XMVECTOR SphericalToCartesian(float radius, float theta, float phi) {
		return DirectX::XMVectorSet(
			radius*sinf(phi)*cosf(theta),
			radius*cosf(phi),
			radius*sinf(phi)*sinf(theta),
			1.0f);
	}

    static DirectX::XMMATRIX InverseTranspose(DirectX::CXMMATRIX M) {
		// Inverse-transpose is just applied to normals.  So zero out 
		// translation row so that it doesn't get into our inverse-transpose
		// calculation--we don't want the inverse-transpose of the translation.
        DirectX::XMMATRIX A = M;
        A.r[3] = DirectX::XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);

        DirectX::XMVECTOR det = DirectX::XMMatrixDeterminant(A);
        return DirectX::XMMatrixTranspose(DirectX::XMMatrixInverse(&det, A));
	}

    static DirectX::XMFLOAT4X4 Identity4x4() {
        static DirectX::XMFLOAT4X4 I(
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f);

        return I;
    }

    static DirectX::XMVECTOR RandUnitVec3();
    static DirectX::XMVECTOR RandHemisphereUnitVec3(DirectX::XMVECTOR n);

public:
	static const float Infinity;
	static const float Pi;
	static const float Epsilon;

	static const float RadToDeg;
	static const float DegToRad;
};

