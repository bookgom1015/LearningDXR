#pragma once

#include <d3dx12.h>

#include "Samplers.h"
#include "HlslCompaction.h"

class ShaderManager;

namespace Debug {
	namespace RootSignatureLayout {
		enum {
			ECB_Debug = 0,
			EC_Consts,
			ESI_Debug0,
			ESI_Debug1,
			ESI_Debug2,
			ESI_Debug3,
			ESI_Debug4,
			Count
		};
	}

	namespace RootConstantsLayout {
		enum {
			ESampleMask0 = 0,
			ESampleMask1,
			ESampleMask2,
			ESampleMask3,
			ESampleMask4,
			Count
		};
	}

	class DebugClass {
	public:
		DebugClass() = default;
		virtual ~DebugClass() = default;

	public:
		bool Initialize(ID3D12Device* device, ShaderManager* const manager, UINT width, UINT height, DXGI_FORMAT backBufferFormat);
		bool CompileShaders(const std::wstring& filePath);
		bool BuildRootSignature(const StaticSamplers& samplers);
		bool BuildPso();
		void Run(
			ID3D12GraphicsCommandList* const cmdList,
			D3D12_CPU_DESCRIPTOR_HANDLE ro_backBuffer,
			D3D12_CPU_DESCRIPTOR_HANDLE dio_dsv,
			D3D12_GPU_VIRTUAL_ADDRESS cbAddress);

		bool OnResize(UINT width, UINT height);

		bool AddDebugMap(
			D3D12_GPU_DESCRIPTOR_HANDLE hGpuSrv,
			DebugShaderParams::SampleMask::Type mask = DebugShaderParams::SampleMask::RGB);
		void RemoveDebugMap(D3D12_GPU_DESCRIPTOR_HANDLE hGpuSrv);

	private:
		ID3D12Device* md3dDevice;
		ShaderManager* mShaderManager;

		Microsoft::WRL::ComPtr<ID3D12RootSignature> mRootSignature;
		Microsoft::WRL::ComPtr<ID3D12PipelineState> mPSO;

		UINT mWidth;
		UINT mHeight;

		D3D12_VIEWPORT mViewport;
		D3D12_RECT mScissorRect;

		DXGI_FORMAT mBackBufferFormat;

		std::array<D3D12_GPU_DESCRIPTOR_HANDLE, 5> mhDebugGpuSrvs;
		std::array< DebugShaderParams::SampleMask::Type, 5> mDebugMasks;
		int mNumEnabledMaps;
	};
}