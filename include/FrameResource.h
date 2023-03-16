#pragma once

#include <DirectXMath.h>

#include "UploadBuffer.h"
#include "HlslCompaction.h"

struct FrameResource {
public:
	FrameResource(
		ID3D12Device* inDevice,
		UINT inPassCount,
		UINT inObjectCount,
		UINT inMaterialCount);
	virtual ~FrameResource() = default;

public:
	bool Initialize();

public:
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> CmdListAlloc;

	UploadBuffer<PassConstants> PassCB;
	UploadBuffer<ObjectData> ObjectSB;
	UploadBuffer<MaterialData> MaterialSB;
	UploadBuffer<BlurConstants> BlurCB;
	UploadBuffer<SsaoConstants> SsaoCB;
	UploadBuffer<RtaoConstants> RtaoCB;
	UploadBuffer<CrossBilateralFilterConstants> CrossBilateralFilterCB;
	UploadBuffer<CalcLocalMeanVarianceConstants> CalcLocalMeanVarCB;

	UINT64 Fence;

	ID3D12Device* Device;
	UINT PassCount;
	UINT ObjectCount;
	UINT MaterialCount;
};