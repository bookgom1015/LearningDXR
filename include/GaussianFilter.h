#pragma once

#include <memory>
#include <string>
#include <array>
#include <unordered_map>
#include <d3dx12.h>

#include "Samplers.h"

class ShaderManager;

namespace GaussianFilter {
	namespace RootSignatureLayout {
		enum {
			ECB_BlurPass = 0,
			EC_Consts,
			ESI_NormalAndDepth,
			ESI_Input,
			Count
		};
	}

	namespace RootConstantsLayout {
		enum {
			EDotThreshold = 0,
			EDepthThreshold,
			EHorizontal,
			Count
		};
	}

	enum FilterType {
		R8G8B8A8,
		R16,
		Count
	};

	class GaussianFilterClass {
	public:
		GaussianFilterClass() = default;
		virtual ~GaussianFilterClass() = default;

	public:
		bool CompileShaders(ShaderManager*const manager, const std::wstring& filePath);
		bool BuildRootSignature(ID3D12Device*const device, const StaticSamplers& samplers);
		bool BuildPso(ID3D12Device*const device, ShaderManager*const manager);
		void Run(
			ID3D12GraphicsCommandList*const cmdList, 
			D3D12_GPU_VIRTUAL_ADDRESS cbAddress,
			D3D12_GPU_DESCRIPTOR_HANDLE normalSrv,
			ID3D12Resource*const primary,
			ID3D12Resource*const secondary,
			D3D12_CPU_DESCRIPTOR_HANDLE primaryRtv,
			D3D12_GPU_DESCRIPTOR_HANDLE primarySrv, 
			D3D12_CPU_DESCRIPTOR_HANDLE secondaryRtv,
			D3D12_GPU_DESCRIPTOR_HANDLE secondarySrv,
			float rootConstants[2],
			FilterType type,
			size_t blurCount = 3);

	private:
		void Blur(
			ID3D12GraphicsCommandList* cmdList, 
			ID3D12Resource*const output,
			D3D12_CPU_DESCRIPTOR_HANDLE outputRtv,
			D3D12_GPU_DESCRIPTOR_HANDLE inputSrv,
			bool horzBlur);

	private:
		Microsoft::WRL::ComPtr<ID3D12RootSignature> mRootSignature;
		std::unordered_map<FilterType, Microsoft::WRL::ComPtr<ID3D12PipelineState>> mPSOs;
	};
}