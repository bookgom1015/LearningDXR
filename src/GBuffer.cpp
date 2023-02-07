#include "GBuffer.h"
#include "Logger.h"

const float GBuffer::ColorMapClearValues[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
const float GBuffer::AlbedoMapClearValues[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
const float GBuffer::NormalMapClearValues[4] = { 0.0f, 0.0f, 1.0f, 0.0f };
const float GBuffer::SpecularMapClearValues[4] = { 0.0f, 0.0f, 0.0f, 1.0f };

GBuffer::GBuffer() {}

GBuffer::~GBuffer() {}

bool GBuffer::Initialize(
	ID3D12Device* device, UINT width, UINT height,
	DXGI_FORMAT colorMapFormat, DXGI_FORMAT normalMapFormat, DXGI_FORMAT depthMapFormat, DXGI_FORMAT specularMapFormat) {
	md3dDevice = device;

	mWidth = width;
	mHeight = height;

	mColorMapFormat = colorMapFormat;
	mNormalMapFormat = normalMapFormat;
	mDepthMapFormat = depthMapFormat;
	mSpecularMapFormat = specularMapFormat;

	CheckIsValid(BuildResource());

	return true;
}

void GBuffer::BuildDescriptors(
		CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuSrv,
		CD3DX12_GPU_DESCRIPTOR_HANDLE hGpuSrv,
		CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuRtv,
		UINT descSize, UINT rtvDescSize,
		ID3D12Resource* depth) {
	mhColorMapCpuSrv = hCpuSrv;
	mhColorMapGpuSrv = hGpuSrv;
	mhColorMapCpuRtv = hCpuRtv;

	mhAlbedoMapCpuSrv = hCpuSrv.Offset(1, descSize);
	mhAlbedoMapGpuSrv = hGpuSrv.Offset(1, descSize);
	mhAlbedoMapCpuRtv = hCpuRtv.Offset(1, rtvDescSize);

	mhNormalMapCpuSrv = hCpuSrv.Offset(1, descSize);
	mhNormalMapGpuSrv = hGpuSrv.Offset(1, descSize);
	mhNormalMapCpuRtv = hCpuRtv.Offset(1, rtvDescSize);

	mhDepthMapCpuSrv = hCpuSrv.Offset(1, descSize);
	mhDepthMapGpuSrv = hGpuSrv.Offset(1, descSize);

	mhSpecularMapCpuSrv = hCpuSrv.Offset(1, descSize);
	mhSpecularMapGpuSrv = hGpuSrv.Offset(1, descSize);
	mhSpecularMapCpuRtv = hCpuRtv.Offset(1, rtvDescSize);

	BuildDescriptors(depth);
}

bool GBuffer::OnResize(UINT width, UINT height, ID3D12Resource* depth) {
	if ((mWidth != width) || (mHeight != height)) {
		mWidth = width;
		mHeight = height;

		CheckIsValid(BuildResource());
		BuildDescriptors(depth);
	}

	return true;
}

void GBuffer::BuildDescriptors(ID3D12Resource* depth) {
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
	srvDesc.Texture2D.MipLevels = 1;

	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	rtvDesc.Texture2D.MipSlice = 0;
	rtvDesc.Texture2D.PlaneSlice = 0;

	// Creates a shader resource view for color map.
	srvDesc.Format = mColorMapFormat;
	md3dDevice->CreateShaderResourceView(mColorMap.Get(), &srvDesc, mhColorMapCpuSrv);

	// Creates a render target view for color map.
	rtvDesc.Format = mColorMapFormat;
	md3dDevice->CreateRenderTargetView(mColorMap.Get(), &rtvDesc, mhColorMapCpuRtv);

	// Creates a shader resource and render target views for albedo map.
	md3dDevice->CreateShaderResourceView(mAlbedoMap.Get(), &srvDesc, mhAlbedoMapCpuSrv);
	md3dDevice->CreateRenderTargetView(mAlbedoMap.Get(), &rtvDesc, mhAlbedoMapCpuRtv);

	// Create a shader resource view for normal map.
	srvDesc.Format = mNormalMapFormat;
	md3dDevice->CreateShaderResourceView(mNormalMap.Get(), &srvDesc, mhNormalMapCpuSrv);

	// Create a render target view for normal map.
	rtvDesc.Format = mNormalMapFormat;
	md3dDevice->CreateRenderTargetView(mNormalMap.Get(), &rtvDesc, mhNormalMapCpuRtv);

	// Create a shader resource view for depth map.
	srvDesc.Format = mDepthMapFormat;
	md3dDevice->CreateShaderResourceView(depth, &srvDesc, mhDepthMapCpuSrv);

	// Create a shader resource view for specular map.
	srvDesc.Format = mSpecularMapFormat;
	md3dDevice->CreateShaderResourceView(mSpecularMap.Get(), &srvDesc, mhSpecularMapCpuSrv);

	// Create a render target view for normal map.
	rtvDesc.Format = mSpecularMapFormat;
	md3dDevice->CreateRenderTargetView(mSpecularMap.Get(), &rtvDesc, mhSpecularMapCpuRtv);
}

bool GBuffer::BuildResource() {
	D3D12_RESOURCE_DESC rscDesc = {};
	rscDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	rscDesc.Alignment = 0;
	rscDesc.Width = mWidth;
	rscDesc.Height = mHeight;
	rscDesc.DepthOrArraySize = 1;
	rscDesc.MipLevels = 1;
	rscDesc.SampleDesc.Count = 1;
	rscDesc.SampleDesc.Quality = 0;
	rscDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	rscDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

	{
		rscDesc.Format = mColorMapFormat;

		CD3DX12_CLEAR_VALUE optClear(mColorMapFormat, ColorMapClearValues);

		CheckHResult(md3dDevice->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&rscDesc,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
			&optClear,
			IID_PPV_ARGS(mColorMap.GetAddressOf())
		));
	}
	{
		rscDesc.Format = mColorMapFormat;

		CD3DX12_CLEAR_VALUE optClear = CD3DX12_CLEAR_VALUE(mColorMapFormat, AlbedoMapClearValues);

		CheckHResult(md3dDevice->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&rscDesc,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
			&optClear,
			IID_PPV_ARGS(mAlbedoMap.GetAddressOf())
		));
	}
	{
		rscDesc.Format = mNormalMapFormat;

		CD3DX12_CLEAR_VALUE optClear = CD3DX12_CLEAR_VALUE(mNormalMapFormat, NormalMapClearValues);

		CheckHResult(md3dDevice->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&rscDesc,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
			&optClear,
			IID_PPV_ARGS(mNormalMap.GetAddressOf())
		));
	}
	{
		rscDesc.Format = mSpecularMapFormat;

		CD3DX12_CLEAR_VALUE optClear = CD3DX12_CLEAR_VALUE(mSpecularMapFormat, SpecularMapClearValues);

		CheckHResult(md3dDevice->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&rscDesc,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
			&optClear,
			IID_PPV_ARGS(mSpecularMap.GetAddressOf())
		));
	}

	return true;
}