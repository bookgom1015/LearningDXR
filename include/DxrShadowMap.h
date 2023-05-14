#pragma once

#include <d3dx12.h>
#include <array>
#include <unordered_map>

#include "Samplers.h"

class ShaderManager;

namespace DxrShadow {
	namespace RootSignatureLayout {
		enum {
			ECB_Pass = 0,
			ESI_AccelerationStructure,
			ESB_Object,
			ESB_Material,
			ESB_Vertices,
			EAB_Indices,
			ESI_Depth,
			EUO_Shadow,
			Count
		};
	}

	namespace Resources {
		enum ResourceType {
			EShadow = 0,
			ETemporary,
			Count
		};

		namespace Descriptors {
			enum {
				ES_Shadow = 0,
				EU_Shadow,
				ES_Temporary,
				EU_Temporary,
				Count
			};
		}
	}

	using ResourcesType = std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, Resources::Count>;
	using ResourcesCpuDescriptors = std::array<CD3DX12_CPU_DESCRIPTOR_HANDLE, Resources::Descriptors::Count>;
	using ResourcesGpuDescriptors = std::array<CD3DX12_GPU_DESCRIPTOR_HANDLE, Resources::Descriptors::Count>;

	const DXGI_FORMAT ShadowFormat = DXGI_FORMAT_R16_UNORM;

	class DxrShadowClass {
	public:
		DxrShadowClass() = default;
		virtual ~DxrShadowClass() = default;

	public:
		bool Initialize(ID3D12Device5*const device, ID3D12GraphicsCommandList*const cmdList, ShaderManager*const manager, UINT width, UINT height);
		bool CompileShaders(const std::wstring& filePath);
		bool BuildRootSignatures(const StaticSamplers& samplers, UINT geometryBufferCount);
		bool BuildDXRPSO();
		bool BuildShaderTables();
		void Run(
			ID3D12GraphicsCommandList4*const cmdList,
			D3D12_GPU_VIRTUAL_ADDRESS accelStruct,
			D3D12_GPU_VIRTUAL_ADDRESS cbAddress,
			D3D12_GPU_VIRTUAL_ADDRESS objSBAddress,
			D3D12_GPU_VIRTUAL_ADDRESS matSBAddress,
			D3D12_GPU_DESCRIPTOR_HANDLE i_vertices,
			D3D12_GPU_DESCRIPTOR_HANDLE i_indices,
			D3D12_GPU_DESCRIPTOR_HANDLE i_depth,
			D3D12_GPU_DESCRIPTOR_HANDLE o_shadow,
			UINT width, UINT height);

		__forceinline constexpr UINT Width() const;
		__forceinline constexpr UINT Height() const;

		__forceinline const DxrShadow::ResourcesType& Resources() const;
		__forceinline const DxrShadow::ResourcesGpuDescriptors& ResourcesGpuDescriptors() const;

		void BuildDescriptors(CD3DX12_CPU_DESCRIPTOR_HANDLE& hCpu, CD3DX12_GPU_DESCRIPTOR_HANDLE& hGpu, UINT descSize);

		bool OnResize(ID3D12GraphicsCommandList* cmdList, UINT width, UINT height);

	private:
		void BuildDescriptors();
		bool BuildResource(ID3D12GraphicsCommandList* cmdList);

	private:
		ID3D12Device5* md3dDevice;
		ShaderManager* mShaderManager;

		Microsoft::WRL::ComPtr<ID3D12RootSignature> mRootSignature;
		Microsoft::WRL::ComPtr<ID3D12StateObject> mPSO;
		Microsoft::WRL::ComPtr<ID3D12StateObjectProperties> mPSOProp;

		std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3D12Resource>> mShaderTables;

		UINT mWidth;
		UINT mHeight;

		DxrShadow::ResourcesType mResources;
		DxrShadow::ResourcesCpuDescriptors mhResourcesCpuDescriptors;
		DxrShadow::ResourcesGpuDescriptors mhResourcesGpuDescriptors;
	};
}

constexpr UINT DxrShadow::DxrShadowClass::Width() const {
	return mWidth;
}

constexpr UINT DxrShadow::DxrShadowClass::Height() const {
	return mHeight;
}

const DxrShadow::ResourcesType& DxrShadow::DxrShadowClass::Resources() const {
	return mResources;
}

const DxrShadow::ResourcesGpuDescriptors& DxrShadow::DxrShadowClass::ResourcesGpuDescriptors() const {
	return mhResourcesGpuDescriptors;
}