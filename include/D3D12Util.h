#pragma once

#include <d3dx12.h>
#include <string>
#include <wrl.h>

struct D3D12BufferCreateInfo {
	UINT64					Size		= 0;
	UINT64					Alignment	= 0;
	D3D12_HEAP_TYPE			HeapType	= D3D12_HEAP_TYPE_DEFAULT;
	D3D12_HEAP_FLAGS		HeapFlags	= D3D12_HEAP_FLAG_NONE;
	D3D12_RESOURCE_FLAGS	Flags		= D3D12_RESOURCE_FLAG_NONE;
	D3D12_RESOURCE_STATES	State		= D3D12_RESOURCE_STATE_COMMON;

	D3D12BufferCreateInfo() {}
	D3D12BufferCreateInfo(UINT64 size, D3D12_RESOURCE_FLAGS flags) :
		Size(size), Flags(flags) {}
	D3D12BufferCreateInfo(UINT64 size, D3D12_HEAP_TYPE heapType, D3D12_RESOURCE_STATES state) :
		Size(size), HeapType(heapType), State(state) {}
	D3D12BufferCreateInfo(UINT64 size, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES state) :
		Size(size), Flags(flags), State(state) {}
	D3D12BufferCreateInfo(UINT64 size, UINT64 alignment, D3D12_HEAP_TYPE heapType, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES state) :
		Size(size), Alignment(alignment), HeapType(heapType), Flags(flags), State(state) {}
	D3D12BufferCreateInfo(UINT64 size, UINT64 alignment, D3D12_HEAP_TYPE heapType, D3D12_HEAP_FLAGS heapFlags, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES state) :
		Size(size), Alignment(alignment), HeapType(heapType), HeapFlags(heapFlags), Flags(flags), State(state) {}
};

class D3D12Util {
public:
	static UINT CalcConstantBufferByteSize(UINT byteSize) {
		// Constant buffers must be a multiple of the minimum hardware
		// allocation size (usually 256 bytes).  So round up to nearest
		// multiple of 256.  We do this by adding 255 and then masking off
		// the lower 2 bytes which store all bits < 256.
		// Example: Suppose byteSize = 300.
		// (300 + 255) & ~255
		// 555 & ~255
		// 0x022B & ~0x00ff
		// 0x022B & 0xff00
		// 0x0200
		// 512
		return (byteSize + 255) & ~255;
	}

	static bool LoadBinary(const std::wstring& inFilename, Microsoft::WRL::ComPtr<ID3DBlob>& outBlob);

	static bool CreateDefaultBuffer(
		ID3D12Device* inDevice,
		ID3D12GraphicsCommandList* inCmdList,
		const void* inInitData,
		UINT64 inByteSize,
		Microsoft::WRL::ComPtr<ID3D12Resource>& outUploadBuffer,
		Microsoft::WRL::ComPtr<ID3D12Resource>& outDefaultBuffer);

	static bool CreateRootSignature(ID3D12Device* pDevice, const D3D12_ROOT_SIGNATURE_DESC& inRootSignatureDesc, ID3D12RootSignature** ppRootSignature);
	static bool CreateVersionedRootSignature(ID3D12Device* pDevice, const D3D12_VERSIONED_ROOT_SIGNATURE_DESC& inRootSignatureDesc, ID3D12RootSignature** ppRootSignature);

	static bool CreateBuffer(ID3D12Device* pDevice, D3D12BufferCreateInfo& inInfo, ID3D12Resource** ppResource, ID3D12InfoQueue* pInfoQueue = nullptr);
	static bool CreateConstantBuffer(ID3D12Device* pDevice, ID3D12Resource** ppResource, UINT64 inSize);

	static D3D12_CPU_DESCRIPTOR_HANDLE GetCpuHandle(ID3D12DescriptorHeap* descHeap, INT index, UINT descriptorSize);
	static D3D12_GPU_DESCRIPTOR_HANDLE GetGpuHandle(ID3D12DescriptorHeap* descHeap, INT index, UINT descriptorSize);

	template <typename T>
	static __forceinline constexpr UINT CalcNumUintValues() {
		return static_cast<UINT>((sizeof(T) - 1) / SizeOfUint + 1);
	}

	static void UavBarrier(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* resource);
	static void UavBarriers(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* resource[], size_t length);

	static __forceinline UINT NumMantissaBitsInFloatFormat(UINT FloatFormatBitLength)	{
		switch (FloatFormatBitLength) {
		case 32: return 23;
		case 16: return 10;
		case 11: return 6;
		case 10: return 5;
		}
		return 0;
	}

	static __forceinline D3D12_GRAPHICS_PIPELINE_STATE_DESC DefaultPsoDesc(D3D12_INPUT_LAYOUT_DESC inputLayout, DXGI_FORMAT dsvFormat);
	static __forceinline D3D12_GRAPHICS_PIPELINE_STATE_DESC QuadPsoDesc();

private:
	static const size_t SizeOfUint;
};

D3D12_GRAPHICS_PIPELINE_STATE_DESC D3D12Util::DefaultPsoDesc(D3D12_INPUT_LAYOUT_DESC inputLayout, DXGI_FORMAT dsvFormat) {
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.InputLayout = inputLayout;
	psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.SampleMask = UINT_MAX;
	psoDesc.SampleDesc.Count = 1;
	psoDesc.SampleDesc.Quality = 0;
	psoDesc.DSVFormat = dsvFormat;
	return psoDesc;
}

D3D12_GRAPHICS_PIPELINE_STATE_DESC D3D12Util::QuadPsoDesc() {
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.InputLayout = { nullptr, 0 };
	psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.SampleMask = UINT_MAX;
	psoDesc.SampleDesc.Count = 1;
	psoDesc.SampleDesc.Quality = 0;
	psoDesc.NumRenderTargets = 1;
	psoDesc.DepthStencilState.DepthEnable = FALSE;
	psoDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;
	return psoDesc;
}