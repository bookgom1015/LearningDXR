#pragma once

#include <d3dx12.h>
#include <array>

#include "Samplers.h"

class ShaderManager;

struct RenderItem;

namespace GBuffer {
	namespace RootSignatureLayout {
		enum {
			ECB_Pass = 0,
			EC_Consts,
			ESB_Object,
			ESB_Material,
			Count
		};
	}

	namespace RootConstantsLayout {
		enum {
			EInstanceID = 0,
			EIsRaytracing,
			Count
		};
	}

	namespace Resources {
		enum ResourceType {
			EColor = 0,
			EAlbedo,
			ENormalDepth,
			ESpecular,
			EVelocity,
			EReprojectedNormalDepth,
			Count
		};

		namespace Descriptors {
			enum {
				ES_Color = 0,
				ER_Color,
				ES_Albedo,
				ER_Albedo,
				ES_NormalDepth,
				ER_NormalDepth,
				ES_Depth,
				ES_Specular,
				ER_Specular,
				ES_Velocity,
				ER_Velocity,
				ES_ReprojectedNormalDepth,
				ER_ReprojectedNormalDepth,
				Count
			};
		}
	}

	using ResourcesType = std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, Resources::Count>;
	using ResourcesCpuDescriptors = std::array<CD3DX12_CPU_DESCRIPTOR_HANDLE, Resources::Descriptors::Count>;
	using ResourcesGpuDescriptors = std::array<CD3DX12_GPU_DESCRIPTOR_HANDLE, Resources::Descriptors::Count>;

	const float ColorMapClearValues[4]					= { 0.0f, 0.0f, 0.0f, 0.0f };
	const float AlbedoMapClearValues[4]					= { 0.0f, 0.0f, 0.0f, 0.0f };
	const float NormalDepthMapClearValues[4]			= { 0.0f, 0.0f, 0.0f, 1.0f };
	const float SpecularMapClearValues[4]				= { 0.0f, 0.0f, 0.0f, 1.0f };
	const float VelocityMapClearValues[2]				= { 1000.0f, 1000.0f };
	const float ReprojectedNormalDepthMapClearValues[4]	= { 0.0f, 0.0f, 0.0f, 1.0f };

	const DXGI_FORMAT ColorFormat					= DXGI_FORMAT_R8G8B8A8_UNORM;
	const DXGI_FORMAT AlbedoFormat					= DXGI_FORMAT_R8G8B8A8_UNORM;
	const DXGI_FORMAT NormalDepthFormat				= DXGI_FORMAT_R8G8B8A8_SNORM;
	const DXGI_FORMAT DepthFormat					= DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
	const DXGI_FORMAT SpecularFormat				= DXGI_FORMAT_R8G8B8A8_UNORM;
	const DXGI_FORMAT VelocityFormat				= DXGI_FORMAT_R16G16_SNORM;
	const DXGI_FORMAT ReprojectedNormalDepthFormat	= DXGI_FORMAT_R8G8B8A8_SNORM;

	class GBufferClass {
	public:
		GBufferClass() = default;
		virtual ~GBufferClass() = default;

	public:
		bool Initialize(ID3D12Device*const device, ShaderManager*const manager, UINT width, UINT height);
		bool CompileShaders(const std::wstring& filePath);
		bool BuildRootSignature(const StaticSamplers& samplers);
		bool BuildPso(D3D12_INPUT_LAYOUT_DESC inputLayout, DXGI_FORMAT depthFormat);
		void Run(
			ID3D12GraphicsCommandList*const cmdList, 
			D3D12_CPU_DESCRIPTOR_HANDLE dsv,
			D3D12_GPU_VIRTUAL_ADDRESS cbAddress,
			D3D12_GPU_VIRTUAL_ADDRESS objSBAddress,
			D3D12_GPU_VIRTUAL_ADDRESS matSBAddress,
			const std::vector<RenderItem*>& ritems);

		__forceinline constexpr UINT Width() const;
		__forceinline constexpr UINT Height() const;

		__forceinline const ResourcesType& Resources() const;
		__forceinline const ResourcesCpuDescriptors& ResourcesCpuDescriptors() const;
		__forceinline const ResourcesGpuDescriptors& ResourcesGpuDescriptors() const;

		void BuildDescriptors(
			CD3DX12_CPU_DESCRIPTOR_HANDLE& hCpuSrv,
			CD3DX12_GPU_DESCRIPTOR_HANDLE& hGpuSrv,
			CD3DX12_CPU_DESCRIPTOR_HANDLE& hCpuRtv,
			UINT descSize, UINT rtvDescSize,
			ID3D12Resource*const depth);

		bool OnResize(UINT width, UINT height, ID3D12Resource*const depth);

	private:
		void BuildDescriptors(ID3D12Resource* depth);
		bool BuildResource();

		void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems);

	private:
		ID3D12Device* md3dDevice;
		ShaderManager* mShaderManager;

		Microsoft::WRL::ComPtr<ID3D12RootSignature> mRootSignature;
		Microsoft::WRL::ComPtr<ID3D12PipelineState> mPSO;

		UINT mWidth;
		UINT mHeight;

		GBuffer::ResourcesType mResources;
		GBuffer::ResourcesCpuDescriptors mhResourcesCpuDescriptors;
		GBuffer::ResourcesGpuDescriptors mhResourcesGpuDescriptors;
	};
}

constexpr UINT GBuffer::GBufferClass::Width() const {
	return mWidth;
}

constexpr UINT GBuffer::GBufferClass::Height() const {
	return mHeight;
}

const GBuffer::ResourcesType& GBuffer::GBufferClass::Resources() const {
	return mResources;
}

const GBuffer::ResourcesCpuDescriptors& GBuffer::GBufferClass::ResourcesCpuDescriptors() const {
	return mhResourcesCpuDescriptors;
}

const GBuffer::ResourcesGpuDescriptors& GBuffer::GBufferClass::ResourcesGpuDescriptors() const {
	return mhResourcesGpuDescriptors;
}