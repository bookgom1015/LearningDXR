#include "FrameResource.h"
#include "Logger.h"

FrameResource::FrameResource(ID3D12Device* inDevice, UINT inPassCount, UINT inObjectCount, UINT inMaterialCount) :
		PassCount(inPassCount),
		ObjectCount(inObjectCount),
		MaterialCount(inMaterialCount) {
	Device = inDevice;
	Fence = 0;
}

bool FrameResource::Initialize() {
	CheckHResult(Device->CreateCommandAllocator(
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		IID_PPV_ARGS(CmdListAlloc.GetAddressOf())
	));

	CheckIsValid(PassCB.Initialize(Device, PassCount, true));
	CheckIsValid(ObjectSB.Initialize(Device, ObjectCount, false));
	CheckIsValid(MaterialSB.Initialize(Device, MaterialCount, false));
	CheckIsValid(BlurCB.Initialize(Device, 1, true));
	CheckIsValid(SsaoCB.Initialize(Device, 1, true));

	return true;
}