#pragma once

#include <d3dx12.h>

#include "Samplers.h"
#include "HlslCompaction.h"

class ShaderManager;

namespace BackBuffer {
	namespace RootSignatureLayout {
		enum {
			ECB_Pass = 0,
			ESI_Color,
			ESI_Albedo,
			ESI_Normal,
			ESI_Depth,
			ESI_Specular,
			ESI_Shadow,
			ESI_AmbientCoefficient,
			Count
		};
	}

	class BackBufferClass {
	public:
		BackBufferClass() = default;
		virtual ~BackBufferClass() = default;

	public:
		bool Initialize(ID3D12Device* const device, ShaderManager* const manager, UINT width, UINT height,
			DXGI_FORMAT backBufferFormat, UINT bufferCount);
		bool CompileShaders(const std::wstring& filePath);
		bool BuildRootSignature(const StaticSamplers& samplers);
		bool BuildPso();
		void Run(
			ID3D12GraphicsCommandList* const cmdList,
			D3D12_GPU_VIRTUAL_ADDRESS cbAddress,
			D3D12_GPU_DESCRIPTOR_HANDLE si_color,
			D3D12_GPU_DESCRIPTOR_HANDLE si_albedo,
			D3D12_GPU_DESCRIPTOR_HANDLE si_normal,
			D3D12_GPU_DESCRIPTOR_HANDLE si_depth,
			D3D12_GPU_DESCRIPTOR_HANDLE si_specular,
			D3D12_GPU_DESCRIPTOR_HANDLE si_shadow,
			D3D12_GPU_DESCRIPTOR_HANDLE si_aoCoefficient);

		void BuildDescriptors(
			ID3D12Resource* const buffers[],
			CD3DX12_CPU_DESCRIPTOR_HANDLE& hCpuSrv,
			CD3DX12_GPU_DESCRIPTOR_HANDLE& hGpuSrv,
			UINT descSize);
		bool OnResize(ID3D12Resource* const buffers[], UINT width, UINT height);

	private:
		void BuildDescriptors(ID3D12Resource* const buffers[]);

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
		UINT mBackBufferCount;

		std::vector<CD3DX12_CPU_DESCRIPTOR_HANDLE> mhBackBufferCpuSrvs;
		std::vector<CD3DX12_GPU_DESCRIPTOR_HANDLE> mhBackBufferGpuSrvs;
	};
}