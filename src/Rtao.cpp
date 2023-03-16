#include "Rtao.h"
#include "Logger.h"
#include "D3D12Util.h"

#include <DirectXColors.h>

#undef max

using namespace DirectX;
using namespace DirectX::PackedVector;

const float Rtao::AmbientMapClearValues[1] = { 1.0f };

Rtao::Rtao() {}

Rtao::~Rtao() {}

bool Rtao::Initialize(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, UINT width, UINT height) {
	md3dDevice = device;

	mWidth = width;
	mHeight = height;

	bSwitch = false;
	bResourceState = false;

	CheckIsValid(BuildResource(cmdList));

	return true;
}

void Rtao::BuildDescriptors(CD3DX12_CPU_DESCRIPTOR_HANDLE& hCpu, CD3DX12_GPU_DESCRIPTOR_HANDLE& hGpu, UINT descSize) {
	mhAOResourcesCpus[RaytracedAO::AOResources::Descriptors::ES_AmbientCoefficient] = hCpuSrv;
	mhAOResourcesGpus[RaytracedAO::AOResources::Descriptors::ES_AmbientCoefficient] = hGpuSrv;
	mhAOResourcesCpus[RaytracedAO::AOResources::Descriptors::EU_AmbientCoefficient] = hCpuUav;
	mhAOResourcesGpus[RaytracedAO::AOResources::Descriptors::EU_AmbientCoefficient] = hGpuUav;

	for (size_t i = 0; i < 2; ++i) {
		mhTemporalCachesCpus[i][RaytracedAO::TemporalCaches::Descriptors::ES_TemporalSupersampling] = hCpuSrv.Offset(1, descSize);
		mhTemporalCachesGpus[i][RaytracedAO::TemporalCaches::Descriptors::ES_TemporalSupersampling] = hGpuSrv.Offset(1, descSize);
		mhTemporalCachesCpus[i][RaytracedAO::TemporalCaches::Descriptors::EU_TemporalSupersampling] = hCpuUav.Offset(1, descSize);
		mhTemporalCachesGpus[i][RaytracedAO::TemporalCaches::Descriptors::EU_TemporalSupersampling] = hGpuUav.Offset(1, descSize);
	}

	//
	// Srvs
	//	
	mhCachedNormalDepthMapCpuSrv = hCpuSrv.Offset(1, descSize);
	mhCachedNormalDepthMapGpuSrv = hGpuSrv.Offset(1, descSize);

	mhDisocclusionBlurStrengthMapCpuSrv = hCpuSrv.Offset(1, descSize);
	mhDisocclusionBlurStrengthMapGpuSrv = hGpuSrv.Offset(1, descSize);

	for (size_t i = 0; i < 2; ++i) {
		mhTemporalAOCoefficientMapCpuSrvs[i] = hCpuSrv.Offset(1, descSize);
		mhTemporalAOCoefficientMapGpuSrvs[i] = hGpuSrv.Offset(1, descSize);
	}

	for (size_t i = 0; i < 2; ++i) {
		mhCoefficientSquaredMeanMapCpuSrvs[i] = hCpuSrv.Offset(1, descSize);
		mhCoefficientSquaredMeanMapGpuSrvs[i] = hGpuSrv.Offset(1, descSize);
	}

	for (size_t i = 0; i < 2; ++i) {
		mhRayHitDistanceMapCpuSrvs[i] = hCpuSrv.Offset(1, descSize);
		mhRayHitDistanceMapGpuSrvs[i] = hGpuSrv.Offset(1, descSize);
	}

	for (size_t i = 0; i < 2; ++i) {
		mhLocalMeanVarianceMapCpuSrvs[i] = hCpuSrv.Offset(1, descSize);
		mhLocalMeanVarianceMapGpuSrvs[i] = hGpuSrv.Offset(1, descSize);
	}

	for (size_t i = 0; i < 2; ++i) {
		mhVarianceMapCpuSrvs[i] = hCpuSrv.Offset(1, descSize);
		mhVarianceMapGpuSrvs[i] = hGpuSrv.Offset(1, descSize);
	}

	//
	// Uavs
	//
	mhLinearDepthDerivativesMapCpuUav = hCpuUav.Offset(1, descSize);
	mhLinearDepthDerivativesMapGpuUav = hGpuUav.Offset(1, descSize);

	mhTsppCoefficientSquaredMeanRayHitDistanceMapCpuUav = hCpuUav.Offset(1, descSize);
	mhTsppCoefficientSquaredMeanRayHitDistanceMapGpuUav = hGpuUav.Offset(1, descSize);

	mhDisocclusionBlurStrengthMapCpuUav = hCpuUav.Offset(1, descSize);
	mhDisocclusionBlurStrengthMapGpuUav = hGpuUav.Offset(1, descSize);

	for (size_t i = 0; i < 2; ++i) {
		mhTemporalAOCoefficientMapCpuUavs[i] = hCpuUav.Offset(1, descSize);
		mhTemporalAOCoefficientMapGpuUavs[i] = hGpuUav.Offset(1, descSize);
	}

	for (size_t i = 0; i < 2; ++i) {
		mhCoefficientSquaredMeanMapCpuUavs[i] = hCpuUav.Offset(1, descSize);
		mhCoefficientSquaredMeanMapGpuUavs[i] = hGpuUav.Offset(1, descSize);
	}

	for (size_t i = 0; i < 2; ++i) {
		mhRayHitDistanceMapCpuUavs[i] = hCpuUav.Offset(1, descSize);
		mhRayHitDistanceMapGpuUavs[i] = hGpuUav.Offset(1, descSize);
	}

	for (size_t i = 0; i < 2; ++i) {
		mhLocalMeanVarianceMapCpuUavs[i] = hCpuUav.Offset(1, descSize);
		mhLocalMeanVarianceMapGpuUavs[i] = hGpuUav.Offset(1, descSize);
	}

	for (size_t i = 0; i < 2; ++i) {
		mhVarianceMapCpuUavs[i] = hCpuUav.Offset(1, descSize);
		mhVarianceMapGpuUavs[i] = hGpuUav.Offset(1, descSize);
	}

	BuildDescriptors();

	hCpuSrv.Offset(1, descSize);
	hGpuSrv.Offset(1, descSize);
	hCpuUav.Offset(1, descSize);
	hGpuUav.Offset(1, descSize);
}

bool Rtao::OnResize(ID3D12GraphicsCommandList* cmdList, UINT width, UINT height) {
	if ((mWidth != width) || (mHeight != height)) {
		mWidth = width;
		mHeight = height;

		CheckIsValid(BuildResource(cmdList));
		BuildDescriptors();
	}

	return true;
}

void Rtao::Transite(ID3D12GraphicsCommandList* cmdList, bool srvToUav) {
	
}

void Rtao::Switch() {
	bSwitch = !bSwitch;
}

void Rtao::BuildDescriptors() {
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
	srvDesc.Texture2D.MipLevels = 1;

	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;

	{
		srvDesc.Format = AmbientMapFormat;
		md3dDevice->CreateShaderResourceView(
			mAOResources[RaytracedAO::AOResources::EAmbientCoefficient].Get(), &srvDesc,
			mhAOResourcesCpus[RaytracedAO::AOResources::Descriptors::ES_AmbientCoefficient]
		);

		uavDesc.Format = AmbientMapFormat;
		md3dDevice->CreateUnorderedAccessView(
			mAOResources[RaytracedAO::AOResources::EAmbientCoefficient].Get(), nullptr, &uavDesc,
			mhAOResourcesCpus[RaytracedAO::AOResources::Descriptors::EU_AmbientCoefficient]
		);
	}

	{
		srvDesc.Format = TemporalSuperSamplingMapFormat;
		uavDesc.Format = TemporalSuperSamplingMapFormat;
		for (size_t i = 0; i < 2; ++i) {
			md3dDevice->CreateShaderResourceView(
				mTemporalCaches[i][RaytracedAO::TemporalCaches::ETemporalSupersampling].Get(), &srvDesc, 
				mhTemporalCachesCpus[i][RaytracedAO::TemporalCaches::Descriptors::ES_TemporalSupersampling]
			);
			md3dDevice->CreateUnorderedAccessView(
				mTemporalCaches[i][RaytracedAO::TemporalCaches::ETemporalSupersampling].Get(), nullptr, &uavDesc, 
				mhTemporalCachesCpus[i][RaytracedAO::TemporalCaches::Descriptors::EU_TemporalSupersampling]
			);
		}
	}

	srvDesc.Format = NormalDepthMapFormat;
	md3dDevice->CreateShaderResourceView(mCachedNormalDepthMap.Get(), &srvDesc, mhCachedNormalDepthMapCpuSrv);

	srvDesc.Format = DisocclusionBlurStrengthMapFormat;
	md3dDevice->CreateShaderResourceView(mDisocclusionBlurStrengthMap.Get(), &srvDesc, mhDisocclusionBlurStrengthMapCpuSrv);

	

	srvDesc.Format = TemporalAOCoefficientMapFormat;
	for (size_t i = 0; i < 2; ++i)
		md3dDevice->CreateShaderResourceView(mTemporalAOCoefficientMaps[i].Get(), &srvDesc, mhTemporalAOCoefficientMapCpuSrvs[i]);

	srvDesc.Format = CoefficientSquaredMeanMapFormat;
	for (size_t i = 0; i < 2; ++i)
		md3dDevice->CreateShaderResourceView(mCoefficientSquaredMeanMaps[i].Get(), &srvDesc, mhCoefficientSquaredMeanMapCpuSrvs[i]);

	srvDesc.Format = RayHitDistanceMapFormat;
	for (size_t i = 0; i < 2; ++i)
		md3dDevice->CreateShaderResourceView(mRayHitDistanceMaps[i].Get(), &srvDesc, mhRayHitDistanceMapCpuSrvs[i]);

	srvDesc.Format = LocalMeanVarianceMapFormat;
	for (size_t i = 0; i < 2; ++i)
		md3dDevice->CreateShaderResourceView(mLocalMeanVarianceMaps[i].Get(), &srvDesc, mhLocalMeanVarianceMapCpuSrvs[i]);

	srvDesc.Format = VarianceMapFormat;
	for (size_t i = 0; i < 2; ++i)
		md3dDevice->CreateShaderResourceView(mVarianceMaps[i].Get(), &srvDesc, mhVarianceMapCpuSrvs[i]);

	

	uavDesc.Format = LinearDepthDerivativesMapFormat;
	md3dDevice->CreateUnorderedAccessView(mLinearDepthDerivativesMap.Get(), nullptr, &uavDesc, mhLinearDepthDerivativesMapCpuUav);

	uavDesc.Format = TsppCoefficientSquaredMeanRayHitDistanceFormat;
	md3dDevice->CreateUnorderedAccessView(mTsppCoefficientSquaredMeanRayHitDistanceMap.Get(), nullptr, &uavDesc, mhTsppCoefficientSquaredMeanRayHitDistanceMapCpuUav);

	uavDesc.Format = DisocclusionBlurStrengthMapFormat;
	md3dDevice->CreateUnorderedAccessView(mDisocclusionBlurStrengthMap.Get(), nullptr, &uavDesc, mhDisocclusionBlurStrengthMapCpuUav);

	uavDesc.Format = TemporalAOCoefficientMapFormat;
	for (size_t i = 0; i < 2; ++i)
		md3dDevice->CreateUnorderedAccessView(mTemporalAOCoefficientMaps[i].Get(), nullptr, &uavDesc, mhTemporalAOCoefficientMapCpuUavs[i]);

	uavDesc.Format = CoefficientSquaredMeanMapFormat;
	for (size_t i = 0; i < 2; ++i)
		md3dDevice->CreateUnorderedAccessView(mCoefficientSquaredMeanMaps[i].Get(), nullptr, &uavDesc, mhCoefficientSquaredMeanMapCpuUavs[i]);

	uavDesc.Format = RayHitDistanceMapFormat;
	for (size_t i = 0; i < 2; ++i)
		md3dDevice->CreateUnorderedAccessView(mRayHitDistanceMaps[i].Get(), nullptr, &uavDesc, mhRayHitDistanceMapCpuUavs[i]);

	uavDesc.Format = LocalMeanVarianceMapFormat;
	for (size_t i = 0; i < 2; ++i)
		md3dDevice->CreateUnorderedAccessView(mLocalMeanVarianceMaps[i].Get(), nullptr, &uavDesc, mhLocalMeanVarianceMapCpuUavs[i]);

	uavDesc.Format = VarianceMapFormat;
	for (size_t i = 0; i < 2; ++i)
		md3dDevice->CreateUnorderedAccessView(mVarianceMaps[i].Get(), nullptr, &uavDesc, mhVarianceMapCpuUavs[i]);
}

bool Rtao::BuildResource(ID3D12GraphicsCommandList* cmdList) {
	D3D12_RESOURCE_DESC texDesc;
	ZeroMemory(&texDesc, sizeof(D3D12_RESOURCE_DESC));
	texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	texDesc.Alignment = 0;
	// Ambient occlusion maps are at half resolution.
	texDesc.Width = mWidth;
	texDesc.Height = mHeight;
	texDesc.DepthOrArraySize = 1;
	texDesc.MipLevels = 1;
	texDesc.SampleDesc.Count = 1;
	texDesc.SampleDesc.Quality = 0;
	texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	
	auto& ambientCoefficientMap = mAOResources[RaytracedAO::AOResources::EAmbientCoefficient];
	{
		texDesc.Format = AmbientMapFormat;
		CheckHResult(md3dDevice->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&texDesc,
			D3D12_RESOURCE_STATE_COMMON,
			nullptr,
			IID_PPV_ARGS(&ambientCoefficientMap)
		));
		ambientCoefficientMap.Get()->SetName(L"DxrAmbientCoefficientMap");
	}
	{
		texDesc.Format = TemporalSuperSamplingMapFormat;
		for (int i = 0; i < 2; ++i) {
			CheckHResult(md3dDevice->CreateCommittedResource(
				&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
				D3D12_HEAP_FLAG_NONE,
				&texDesc,
				D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
				nullptr,
				IID_PPV_ARGS(&mTemporalCaches[i][RaytracedAO::TemporalCaches::ETemporalSupersampling])
			));
			mTemporalCaches[i][RaytracedAO::TemporalCaches::ETemporalSupersampling]->SetName(L"TemporalSuperSamplingMap_" + i);
		}
	}
	{
		auto _texDesc = texDesc;
		_texDesc.Format = NormalDepthMapFormat;
		_texDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
		CheckHResult(md3dDevice->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&_texDesc,
			D3D12_RESOURCE_STATE_COPY_DEST,
			nullptr,
			IID_PPV_ARGS(&mCachedNormalDepthMap)
		));
		mCachedNormalDepthMap->SetName(L"CachedNormalDepthMap");
	}
	{
		texDesc.Format = LinearDepthDerivativesMapFormat;
		CheckHResult(md3dDevice->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&texDesc,
			D3D12_RESOURCE_STATE_COMMON,
			nullptr,
			IID_PPV_ARGS(&mLinearDepthDerivativesMap)
		));
		mLinearDepthDerivativesMap->SetName(L"LinearDepthDerivativesMap");
	}
	{
		texDesc.Format = TsppCoefficientSquaredMeanRayHitDistanceFormat;
		CheckHResult(md3dDevice->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&texDesc,
			D3D12_RESOURCE_STATE_COMMON,
			nullptr,
			IID_PPV_ARGS(&mTsppCoefficientSquaredMeanRayHitDistanceMap)
		));
		mTsppCoefficientSquaredMeanRayHitDistanceMap->SetName(L"TsppCoefficientSquaredMeanRayHitDistanceMap");
	}
	{
		texDesc.Format = DisocclusionBlurStrengthMapFormat;
		CheckHResult(md3dDevice->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&texDesc,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
			nullptr,
			IID_PPV_ARGS(&mDisocclusionBlurStrengthMap)
		));
		mDisocclusionBlurStrengthMap->SetName(L"DisocclusionBlurStrengthMap");
	}
	{
		texDesc.Format = TemporalAOCoefficientMapFormat;
		CheckHResult(md3dDevice->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&texDesc,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
			nullptr,
			IID_PPV_ARGS(&mTemporalAOCoefficientMaps[0])
		));
		mTemporalAOCoefficientMaps[0]->SetName(L"TemporalAOCoefficientMap_0");

		CheckHResult(md3dDevice->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&texDesc,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
			nullptr,
			IID_PPV_ARGS(&mTemporalAOCoefficientMaps[1])
		));
		mTemporalAOCoefficientMaps[1]->SetName(L"TemporalAOCoefficientMap_1");
	}
	{
		texDesc.Format = CoefficientSquaredMeanMapFormat;
		CheckHResult(md3dDevice->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&texDesc,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
			nullptr,
			IID_PPV_ARGS(&mCoefficientSquaredMeanMaps[0])
		));
		mCoefficientSquaredMeanMaps[0]->SetName(L"CoefficientSquaredMeanMap_0");

		CheckHResult(md3dDevice->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&texDesc,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
			nullptr,
			IID_PPV_ARGS(&mCoefficientSquaredMeanMaps[1])
		));
		mCoefficientSquaredMeanMaps[1]->SetName(L"CoefficientSquaredMeanMap_1");
	}
	{
		texDesc.Format = RayHitDistanceMapFormat;
		CheckHResult(md3dDevice->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&texDesc,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
			nullptr,
			IID_PPV_ARGS(&mRayHitDistanceMaps[0])
		));
		mRayHitDistanceMaps[0]->SetName(L"RayHitDistanceMap_0");

		CheckHResult(md3dDevice->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&texDesc,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
			nullptr,
			IID_PPV_ARGS(&mRayHitDistanceMaps[1])
		));
		mRayHitDistanceMaps[1]->SetName(L"RayHitDistanceMap_1");
	}
	{
		texDesc.Format = LocalMeanVarianceMapFormat;
		CheckHResult(md3dDevice->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&texDesc,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
			nullptr,
			IID_PPV_ARGS(&mLocalMeanVarianceMaps[0])
		));
		mLocalMeanVarianceMaps[0]->SetName(L"LocalMeanVarianceMap_0");

		CheckHResult(md3dDevice->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&texDesc,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
			nullptr,
			IID_PPV_ARGS(&mLocalMeanVarianceMaps[1])
		));
		mLocalMeanVarianceMaps[1]->SetName(L"LocalMeanVarianceMap_1");
	}
	{
		texDesc.Format = VarianceMapFormat;
		CheckHResult(md3dDevice->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&texDesc,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
			nullptr,
			IID_PPV_ARGS(&mVarianceMaps[0])
		));
		mVarianceMaps[0]->SetName(L"VarianceMap_0");

		CheckHResult(md3dDevice->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&texDesc,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
			nullptr,
			IID_PPV_ARGS(&mVarianceMaps[1])
		));
		mVarianceMaps[1]->SetName(L"VarianceMap_1");
	}

	{
		const UINT num2DSubresources = texDesc.DepthOrArraySize * texDesc.MipLevels;
		const UINT64 uploadBufferSize = GetRequiredIntermediateSize(mCachedNormalDepthMap.Get(), 0, num2DSubresources);

		CheckHResult(md3dDevice->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize),
			D3D12_RESOURCE_STATE_COPY_SOURCE,
			nullptr,
			IID_PPV_ARGS(mCachedNormalDepthMapUploadBuffer.GetAddressOf())
		));

		const UINT size = mWidth * mHeight * 4;
		std::vector<BYTE> data(size);

		for (UINT i = 0; i < size; i += 4) {
			data[i] = data[i + 1] = data[i + 2] = 0;	// rgb-channels(normal) = 0 / 128;
			data[i + 3] = 127;							// a-channel(depth) = 127 / 128;
		}

		D3D12_SUBRESOURCE_DATA subResourceData = {};
		subResourceData.pData = data.data();
		subResourceData.RowPitch = mWidth * 4;
		subResourceData.SlicePitch = subResourceData.RowPitch * mHeight;

		UpdateSubresources(
			cmdList,
			mCachedNormalDepthMap.Get(),
			mCachedNormalDepthMapUploadBuffer.Get(),
			0,
			0,
			num2DSubresources,
			&subResourceData
		);
		cmdList->ResourceBarrier(
			1,
			&CD3DX12_RESOURCE_BARRIER::Transition(
				mCachedNormalDepthMap.Get(),
				D3D12_RESOURCE_STATE_COPY_DEST,
				D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
			)
		);
	}

	std::vector<ID3D12Resource*> resources = { 
		ambientCoefficientMap.Get(),
		mLinearDepthDerivativesMap.Get(),
		mTsppCoefficientSquaredMeanRayHitDistanceMap.Get()
	};
	D3D12_RESOURCE_BARRIER barriers[] = {
		CD3DX12_RESOURCE_BARRIER::Transition(
			ambientCoefficientMap.Get(),
			D3D12_RESOURCE_STATE_COMMON,
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS
		),
		CD3DX12_RESOURCE_BARRIER::Transition(
			mLinearDepthDerivativesMap.Get(),
			D3D12_RESOURCE_STATE_COMMON,
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS
		),
		CD3DX12_RESOURCE_BARRIER::Transition(
			mTsppCoefficientSquaredMeanRayHitDistanceMap.Get(),
			D3D12_RESOURCE_STATE_COMMON,
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS
		)
	};
	cmdList->ResourceBarrier(
		_countof(barriers),
		barriers
	);
	D3D12Util::UavBarriers(cmdList, resources.data(), resources.size());

	return true;
}