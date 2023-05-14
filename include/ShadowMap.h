#pragma once

#include <d3dx12.h>
#include <array>

#include "Samplers.h"

class ShaderManager;

struct RenderItem;

namespace Shadow {
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
		enum {
			EShadow = 0,
			Count
		};
	}

	const DXGI_FORMAT ShadowFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

	class ShadowClass {
	public:
		ShadowClass() = default;
		virtual ~ShadowClass() = default;

	public:
		bool Initialize(ID3D12Device*const  device, ShaderManager*const manager, UINT width, UINT height);
		bool CompileShaders(const std::wstring& filePath);
		bool BuildRootSignatures(const StaticSamplers& samplers);
		bool BuildPSO(D3D12_INPUT_LAYOUT_DESC inputLayout, DXGI_FORMAT depthFormat);
		void Run(
			ID3D12GraphicsCommandList*const cmdList,
			D3D12_GPU_VIRTUAL_ADDRESS cbAddress,
			D3D12_GPU_VIRTUAL_ADDRESS objSBAddress,
			D3D12_GPU_VIRTUAL_ADDRESS matSBAddress,
			const std::vector<RenderItem*>& ritems);

		__forceinline constexpr UINT Width() const;
		__forceinline constexpr UINT Height() const;

		__forceinline constexpr D3D12_VIEWPORT Viewport() const;
		__forceinline constexpr D3D12_RECT ScissorRect() const;

		__forceinline ID3D12Resource* Resource();
		__forceinline constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE Srv() const;
		__forceinline constexpr CD3DX12_CPU_DESCRIPTOR_HANDLE Dsv() const;

		void BuildDescriptors(
			CD3DX12_CPU_DESCRIPTOR_HANDLE& hCpuSrv,
			CD3DX12_GPU_DESCRIPTOR_HANDLE& hGpuSrv,
			CD3DX12_CPU_DESCRIPTOR_HANDLE& hCpuDsv,
			UINT descSize, UINT dsvDescSize);

	private:
		void BuildDescriptors();
		bool BuildResource();

		void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems);

	private:
		ID3D12Device* md3dDevice;
		ShaderManager* mShaderManager;

		Microsoft::WRL::ComPtr<ID3D12RootSignature> mRootSignature;
		Microsoft::WRL::ComPtr<ID3D12PipelineState> mPSO;

		UINT mWidth;
		UINT mHeight;

		D3D12_VIEWPORT mViewport;
		D3D12_RECT mScissorRect;

		Microsoft::WRL::ComPtr<ID3D12Resource> mShadowMap = nullptr;

		CD3DX12_CPU_DESCRIPTOR_HANDLE mhCpuSrv;
		CD3DX12_GPU_DESCRIPTOR_HANDLE mhGpuSrv;
		CD3DX12_CPU_DESCRIPTOR_HANDLE mhCpuDsv;
	};
}

constexpr UINT Shadow::ShadowClass::Width() const {
	return mWidth;
}

constexpr UINT Shadow::ShadowClass::Height() const {
	return mHeight;
}

constexpr D3D12_VIEWPORT Shadow::ShadowClass::Viewport() const {
	return mViewport;
}

constexpr D3D12_RECT Shadow::ShadowClass::ScissorRect() const {
	return mScissorRect;
}

ID3D12Resource* Shadow::ShadowClass::Resource() {
	return mShadowMap.Get();
}

constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE Shadow::ShadowClass::Srv() const {
	return mhGpuSrv;
}

constexpr CD3DX12_CPU_DESCRIPTOR_HANDLE Shadow::ShadowClass::Dsv() const {
	return mhCpuDsv;
}