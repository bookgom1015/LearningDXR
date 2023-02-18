#pragma once

#include <d3dx12.h>

class GBuffer {
public:
	GBuffer();
	virtual ~GBuffer();

public:
	bool Initialize(ID3D12Device* device, UINT width, UINT height, DXGI_FORMAT backBufferFormat, DXGI_FORMAT depthMapFormat);

	__forceinline constexpr UINT Width() const;
	__forceinline constexpr UINT Height() const;

	__forceinline ID3D12Resource* ColorMapResource();
	__forceinline ID3D12Resource* AlbedoMapResource();
	__forceinline ID3D12Resource* NormalMapResource();
	__forceinline ID3D12Resource* SpecularMapResource();
	__forceinline ID3D12Resource* VelocityMapResource();

	__forceinline constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE ColorMapSrv() const;
	__forceinline constexpr CD3DX12_CPU_DESCRIPTOR_HANDLE ColorMapRtv() const;

	__forceinline constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE DepthMapSrv() const;

	__forceinline constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE AlbedoMapSrv() const;
	__forceinline constexpr CD3DX12_CPU_DESCRIPTOR_HANDLE AlbedoMapRtv() const;

	__forceinline constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE NormalMapSrv() const;
	__forceinline constexpr CD3DX12_CPU_DESCRIPTOR_HANDLE NormalMapRtv() const;

	__forceinline constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE SpecularMapSrv() const;
	__forceinline constexpr CD3DX12_CPU_DESCRIPTOR_HANDLE SpecularMapRtv() const;

	__forceinline constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE VelocityMapSrv() const;
	__forceinline constexpr CD3DX12_CPU_DESCRIPTOR_HANDLE VelocityMapRtv() const;

	void BuildDescriptors(
		CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuSrv,
		CD3DX12_GPU_DESCRIPTOR_HANDLE hGpuSrv,
		CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuRtv,
		UINT descSize, UINT rtvDescSize,
		ID3D12Resource* depth);

	bool OnResize(UINT width, UINT height, ID3D12Resource* depth);

private:
	void BuildDescriptors(ID3D12Resource* depth);
	bool BuildResource();

public:
	static const UINT NumRenderTargets = 5;

	static const DXGI_FORMAT NormalMapFormat = DXGI_FORMAT_R8G8B8A8_SNORM;
	static const DXGI_FORMAT SpecularMapFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
	static const DXGI_FORMAT VelocityMapFormat = DXGI_FORMAT_R16G16_SNORM;

	static const float ColorMapClearValues[4];
	static const float AlbedoMapClearValues[4];
	static const float NormalMapClearValues[4];
	static const float SpecularMapClearValues[4];
	static const float VelocityMapClearValues[2];

private:
	ID3D12Device* md3dDevice;

	UINT mWidth;
	UINT mHeight;

	DXGI_FORMAT mColorMapFormat;
	DXGI_FORMAT mDepthMapFormat;

	Microsoft::WRL::ComPtr<ID3D12Resource> mColorMap;
	Microsoft::WRL::ComPtr<ID3D12Resource> mAlbedoMap;
	Microsoft::WRL::ComPtr<ID3D12Resource> mNormalMap;
	Microsoft::WRL::ComPtr<ID3D12Resource> mSpecularMap;
	Microsoft::WRL::ComPtr<ID3D12Resource> mVelocityMap;

	CD3DX12_CPU_DESCRIPTOR_HANDLE mhColorMapCpuSrv;
	CD3DX12_GPU_DESCRIPTOR_HANDLE mhColorMapGpuSrv;
	CD3DX12_CPU_DESCRIPTOR_HANDLE mhColorMapCpuRtv;

	CD3DX12_CPU_DESCRIPTOR_HANDLE mhAlbedoMapCpuSrv;
	CD3DX12_GPU_DESCRIPTOR_HANDLE mhAlbedoMapGpuSrv;
	CD3DX12_CPU_DESCRIPTOR_HANDLE mhAlbedoMapCpuRtv;

	CD3DX12_CPU_DESCRIPTOR_HANDLE mhNormalMapCpuSrv;
	CD3DX12_GPU_DESCRIPTOR_HANDLE mhNormalMapGpuSrv;
	CD3DX12_CPU_DESCRIPTOR_HANDLE mhNormalMapCpuRtv;

	CD3DX12_CPU_DESCRIPTOR_HANDLE mhDepthMapCpuSrv;
	CD3DX12_GPU_DESCRIPTOR_HANDLE mhDepthMapGpuSrv;

	CD3DX12_CPU_DESCRIPTOR_HANDLE mhSpecularMapCpuSrv;
	CD3DX12_GPU_DESCRIPTOR_HANDLE mhSpecularMapGpuSrv;
	CD3DX12_CPU_DESCRIPTOR_HANDLE mhSpecularMapCpuRtv;

	CD3DX12_CPU_DESCRIPTOR_HANDLE mhVelocityMapCpuSrv;
	CD3DX12_GPU_DESCRIPTOR_HANDLE mhVelocityMapGpuSrv;
	CD3DX12_CPU_DESCRIPTOR_HANDLE mhVelocityMapCpuRtv;
};

constexpr UINT GBuffer::Width() const {
	return mWidth;
}

constexpr UINT GBuffer::Height() const {
	return mHeight;
}

ID3D12Resource* GBuffer::ColorMapResource() {
	return mColorMap.Get();
}

ID3D12Resource* GBuffer::AlbedoMapResource() {
	return mAlbedoMap.Get();
}

ID3D12Resource* GBuffer::NormalMapResource() {
	return mNormalMap.Get();
}

ID3D12Resource* GBuffer::SpecularMapResource() {
	return mSpecularMap.Get();
}

ID3D12Resource* GBuffer::VelocityMapResource() {
	return mVelocityMap.Get();
}

constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE GBuffer::ColorMapSrv() const {
	return mhColorMapGpuSrv;
}

constexpr CD3DX12_CPU_DESCRIPTOR_HANDLE GBuffer::ColorMapRtv() const {
	return mhColorMapCpuRtv;
}

constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE GBuffer::DepthMapSrv() const {
	return mhDepthMapGpuSrv;
}

constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE GBuffer::AlbedoMapSrv() const {
	return mhAlbedoMapGpuSrv;
}

constexpr CD3DX12_CPU_DESCRIPTOR_HANDLE GBuffer::AlbedoMapRtv() const {
	return mhAlbedoMapCpuRtv;
}

constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE GBuffer::NormalMapSrv() const {
	return mhNormalMapGpuSrv;
}

constexpr CD3DX12_CPU_DESCRIPTOR_HANDLE GBuffer::NormalMapRtv() const {
	return mhNormalMapCpuRtv;
}

constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE GBuffer::SpecularMapSrv() const {
	return mhSpecularMapGpuSrv;
}

constexpr CD3DX12_CPU_DESCRIPTOR_HANDLE GBuffer::SpecularMapRtv() const {
	return mhSpecularMapCpuRtv;
}

constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE GBuffer::VelocityMapSrv() const {
	return mhVelocityMapGpuSrv;
}

constexpr CD3DX12_CPU_DESCRIPTOR_HANDLE GBuffer::VelocityMapRtv() const {
	return mhVelocityMapCpuRtv;
}