#include "Camera.h"
#include "Logger.h"

#include <algorithm>

#undef min
#undef max

using namespace DirectX;

Camera::Camera() {
	mPosition = { 0.0f, 0.0f, 0.0f };
	mPhi = DirectX::XM_PI * 1.5f;
	mTheta = DirectX::XM_PI * 0.5f;
	mRadius = 15.0f;
}

Camera::~Camera() {}

bool Camera::Initialize(UINT width, UINT height, float fovY) {
	mFovY = fovY;
	CheckIsValid(OnResize(width, height));

	return true;
}

bool Camera::Update(const GameTimer& gt) {
	mPosition.x = mRadius * std::sin(mTheta) * std::cos(mPhi);
	mPosition.z = mRadius * std::sin(mTheta) * std::sin(mPhi);
	mPosition.y = mRadius * std::cos(mTheta);

	return true;
}

bool Camera::OnResize(UINT width, UINT height) {
	mPerspective = XMMatrixPerspectiveFovLH(mFovY, static_cast<float>(width) / static_cast<float>(height), 0.1f, 1000.0f);
	mOrthographic = XMMatrixOrthographicLH(static_cast<float>(width), static_cast<float>(height), 0.1f, 1000.0f);

	return true;
}

void Camera::AddPhi(float phi) {
	mPhi += phi;
}

void Camera::AddTheta(float theta) {
	mTheta = std::max(std::min(mTheta + theta, ThetaMaxLimit), ThetaMinLimit);
}

void Camera::AddRadius(float radius) {
	mRadius = std::max(std::min(mRadius + radius, RadiusMaxLimit), RadiusMinLimit);
}

const DirectX::XMFLOAT3& Camera::GetCameraPosition() const {
	return mPosition;
}

DirectX::XMMATRIX Camera::GetViewMatrix(bool unit) const {
	XMVECTOR pos; 
	if (unit) {
		XMVECTOR vec = XMLoadFloat3(&mPosition);
		pos = XMVector4Normalize(vec);		
	}
	else {
		pos = XMVectorSet(mPosition.x, mPosition.y, mPosition.z, 1.0f);
	}
	XMVECTOR target = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
	XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
	
	return XMMatrixLookAtLH(pos, target, up);
}

DirectX::XMMATRIX Camera::GetProjectionMatrix(bool perspective) const {
	return perspective ? mPerspective : mOrthographic;
}