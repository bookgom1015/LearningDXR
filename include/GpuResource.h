#pragma once

#include <d3dx12.h>
#include <wrl.h>

class GpuResource {
public:
	GpuResource() = default;
	virtual ~GpuResource() = default;

public:
	bool Initialize(
		ID3D12Device* const device,
		const D3D12_HEAP_PROPERTIES* heapProp,
		D3D12_HEAP_FLAGS heapFlag,
		const D3D12_RESOURCE_DESC* rscDesc,
		D3D12_RESOURCE_STATES initialState,
		const D3D12_CLEAR_VALUE* optClear,
		LPCWSTR name = nullptr);

	void Transite(ID3D12GraphicsCommandList* const cmdList, D3D12_RESOURCE_STATES state);

	ID3D12Resource* const Resource() const;

private:
	Microsoft::WRL::ComPtr<ID3D12Resource> mResource;

	D3D12_RESOURCE_STATES mCurrState;
};