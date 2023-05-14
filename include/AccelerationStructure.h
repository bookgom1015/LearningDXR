#pragma once

#include <d3d12.h>
#include <wrl.h>

struct AccelerationStructureBuffer {
	Microsoft::WRL::ComPtr<ID3D12Resource> Scratch;
	Microsoft::WRL::ComPtr<ID3D12Resource> Result;
	Microsoft::WRL::ComPtr<ID3D12Resource> InstanceDesc;	// only used in top-level AS
	UINT64 ResultDataMaxSizeInBytes;
};