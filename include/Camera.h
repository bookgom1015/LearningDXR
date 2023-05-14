#pragma once

#include "GameTimer.h"

#include <DirectXMath.h>
#include <Windows.h>

class Camera {
public:
	Camera();
	virtual ~Camera();

public:
	bool Initialize(UINT width, UINT height, float fovY);

	bool Update(const GameTimer& gt);
	bool OnResize(UINT width, UINT height);

	void AddPhi(float phi);
	void AddTheta(float theta);
	void AddRadius(float radius);
	const DirectX::XMFLOAT3& GetCameraPosition() const;

	DirectX::XMMATRIX GetViewMatrix(bool unit = false) const;
	DirectX::XMMATRIX GetProjectionMatrix(bool perspective = true) const;

	__forceinline float FovY() const;

private:
	const float ThetaMinLimit = DirectX::XM_PI * 0.1f;
	const float ThetaMaxLimit = DirectX::XM_PI * 0.9f;
	const float RadiusMinLimit = 1.0f;
	const float RadiusMaxLimit = 30.0f;

	DirectX::XMFLOAT3 mPosition;

	DirectX::XMMATRIX mPerspective;
	DirectX::XMMATRIX mOrthographic;
	float mFovY;

	float mPhi;
	float mTheta;
	float mRadius;
};

float Camera::FovY() const {
	return mFovY;
}