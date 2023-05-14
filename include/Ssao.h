#pragma once

#include <d3dx12.h>
#include <array>

#include "MathHelper.h"
#include "Samplers.h"

class ShaderManager;

namespace Ssao {
	namespace RootSignatureLayout {
		enum {
			ECB_SsaoPass = 0,
			ESI_NormalAndDepth,
			ESI_RandomVector,
			Count
		};
	}

	namespace Resources {
		enum ResourceType {
			EAmbientCoefficient = 0,
			ETemporary,
			ERandomVector,
			Count
		};

		namespace Descriptors {
			enum {
				ES_AmbientCoefficient = 0,
				ER_AmbientCoefficient,
				ES_Temporary,
				ER_Temporary,
				ES_RandomVector,
				Count
			};
		}
	}

	using ResourcesType = std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, Resources::Count>;
	using ResourcesCpuDescriptors = std::array<CD3DX12_CPU_DESCRIPTOR_HANDLE, Resources::Descriptors::Count>;
	using ResourcesGpuDescriptors = std::array<CD3DX12_GPU_DESCRIPTOR_HANDLE, Resources::Descriptors::Count>;

	const UINT NumRenderTargets = 2;

	const float AmbientMapClearValues[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

	const DXGI_FORMAT AmbientCoefficientFormat	= DXGI_FORMAT_R16_UNORM;
	const DXGI_FORMAT RandomVectorFormat		= DXGI_FORMAT_R8G8B8A8_UNORM;

	class SsaoClass {
	public:
		SsaoClass() = default;
		virtual ~SsaoClass() = default;

	public:
		bool Initialize(ID3D12Device*const device, ID3D12GraphicsCommandList*const cmdList, ShaderManager*const manager, UINT width, UINT height, UINT divider);
		bool CompileShaders(const std::wstring& filePath);
		bool BuildRootSignature(const StaticSamplers& samplers);
		bool BuildPso();
		void Run(
			ID3D12GraphicsCommandList*const cmdList,
			D3D12_GPU_VIRTUAL_ADDRESS cbAddress,
			D3D12_GPU_DESCRIPTOR_HANDLE normalAndDepthSrv);

		__forceinline constexpr UINT Width() const;
		__forceinline constexpr UINT Height() const;

		__forceinline constexpr D3D12_VIEWPORT Viewport() const;
		__forceinline constexpr D3D12_RECT ScissorRect() const;

		__forceinline const ResourcesType& Resources() const;
		__forceinline const ResourcesCpuDescriptors& ResourcesCpuDescriptors() const;
		__forceinline const ResourcesGpuDescriptors& ResourcesGpuDescriptors() const;

		void GetOffsetVectors(DirectX::XMFLOAT4 offsets[14]);

		void BuildDescriptors(
			CD3DX12_CPU_DESCRIPTOR_HANDLE& hCpuSrv,
			CD3DX12_GPU_DESCRIPTOR_HANDLE& hGpuSrv,
			CD3DX12_CPU_DESCRIPTOR_HANDLE& hCpuRtv,
			UINT descSize, UINT rtvDescSize);

		bool OnResize(UINT width, UINT height);

	private:
		void BuildDescriptors();
		bool BuildResource();

		void BuildOffsetVectors();
		bool BuildRandomVectorTexture(ID3D12GraphicsCommandList* cmdList);

	private:
		ID3D12Device* md3dDevice;
		ShaderManager* mShaderManager;

		Microsoft::WRL::ComPtr<ID3D12RootSignature> mRootSignature;
		Microsoft::WRL::ComPtr<ID3D12PipelineState> mPSO;

		UINT mWidth;
		UINT mHeight;

		UINT mDivider;

		D3D12_VIEWPORT mViewport;
		D3D12_RECT mScissorRect;
		
		Ssao::ResourcesType mResources;
		Ssao::ResourcesCpuDescriptors mhResourcesCpuDescriptors;
		Ssao::ResourcesGpuDescriptors mhResourcesGpuDescriptors;

		Microsoft::WRL::ComPtr<ID3D12Resource> mRandomVectorMapUploadBuffer;

		DirectX::XMFLOAT4 mOffsets[14];
	};
}

constexpr UINT Ssao::SsaoClass::Width() const {
	return mWidth;
}

constexpr UINT Ssao::SsaoClass::Height() const {
	return mHeight;
}

constexpr D3D12_VIEWPORT Ssao::SsaoClass::Viewport() const {
	return mViewport;
}

constexpr D3D12_RECT Ssao::SsaoClass::ScissorRect() const {
	return mScissorRect;
}

const Ssao::ResourcesType& Ssao::SsaoClass::Resources() const {
	return mResources;
}

const Ssao::ResourcesCpuDescriptors& Ssao::SsaoClass::ResourcesCpuDescriptors() const {
	return mhResourcesCpuDescriptors;
}

const Ssao::ResourcesGpuDescriptors& Ssao::SsaoClass::ResourcesGpuDescriptors() const {
	return mhResourcesGpuDescriptors;
}