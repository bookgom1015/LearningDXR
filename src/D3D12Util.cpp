#include "D3D12Util.h"
#include "Logger.h"
#include "d3dx12.h"

#include <d3dcompiler.h>
#include <fstream>

using namespace Microsoft::WRL;

const size_t D3D12Util::SizeOfUint = sizeof(UINT);

bool D3D12Util::LoadBinary(const std::wstring& inFilename, ComPtr<ID3DBlob>& outBlob) {
	std::ifstream fin(inFilename, std::ios::binary);

	fin.seekg(0, std::ios_base::end);
	std::ifstream::pos_type size = (int)fin.tellg();
	fin.seekg(0, std::ios_base::beg);

	CheckHResult(D3DCreateBlob(size, outBlob.GetAddressOf()));

	fin.read(reinterpret_cast<char*>(outBlob->GetBufferPointer()), size);
	fin.close();

	return true;
}

bool D3D12Util::CreateDefaultBuffer(
	ID3D12Device* inDevice,
	ID3D12GraphicsCommandList* inCmdList,
	const void* inInitData,
	UINT64 inByteSize,
	ComPtr<ID3D12Resource>& outUploadBuffer,
	ComPtr<ID3D12Resource>& outDefaultBuffer) {
	// Create the actual default buffer resource.
	CheckHResult(inDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(inByteSize),
		D3D12_RESOURCE_STATE_COMMON,
		nullptr,
		IID_PPV_ARGS(outDefaultBuffer.GetAddressOf()))
	);

	// In order to copy CPU memory data into our default buffer, we need to create
	// an intermediate upload heap. 
	CheckHResult(inDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(inByteSize),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(outUploadBuffer.GetAddressOf()))
	);

	// Describe the data we want to copy into the default buffer.
	D3D12_SUBRESOURCE_DATA subResourceData = {};
	subResourceData.pData = inInitData;
	subResourceData.RowPitch = inByteSize;
	subResourceData.SlicePitch = subResourceData.RowPitch;

	// Schedule to copy the data to the default buffer resource.  At a high level, the helper function UpdateSubresources
	// will copy the CPU memory into the intermediate upload heap.  Then, using ID3D12CommandList::CopySubresourceRegion,
	// the intermediate upload heap data will be copied to mBuffer.
	inCmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(outDefaultBuffer.Get(),
		D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST));
	UpdateSubresources<1>(inCmdList, outDefaultBuffer.Get(), outUploadBuffer.Get(), 0, 0, 1, &subResourceData);
	inCmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(outDefaultBuffer.Get(),
		D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ));

	// Note: uploadBuffer has to be kept alive after the above function calls because
	// the command list has not been executed yet that performs the actual copy.
	// The caller can Release the uploadBuffer after it knows the copy has been executed.

	return true;
}

bool D3D12Util::CreateRootSignature(ID3D12Device* pDevice, const D3D12_ROOT_SIGNATURE_DESC& inRootSignatureDesc, ID3D12RootSignature** ppRootSignature) {
	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(
		&inRootSignatureDesc,
		D3D_ROOT_SIGNATURE_VERSION_1,
		serializedRootSig.GetAddressOf(),
		errorBlob.GetAddressOf()
	);

	std::wstringstream wsstream;
	if (errorBlob != nullptr)
		wsstream << reinterpret_cast<char*>(errorBlob->GetBufferPointer());

	if (FAILED(hr))
		ReturnFalse(wsstream.str());

	CheckHResult(pDevice->CreateRootSignature(
		0,
		serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(ppRootSignature)
	));

	return true;
}

bool D3D12Util::CreateBuffer(ID3D12Device* pDevice, D3D12BufferCreateInfo& inInfo, ID3D12Resource** ppResource, ID3D12InfoQueue* pInfoQueue) {
	D3D12_HEAP_PROPERTIES heapDesc = {};
	heapDesc.Type = inInfo.HeapType;
	heapDesc.CreationNodeMask = 1;
	heapDesc.VisibleNodeMask = 1;

	D3D12_RESOURCE_DESC resourceDesc = {};
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	resourceDesc.Alignment = inInfo.Alignment;
	resourceDesc.Height = 1;
	resourceDesc.DepthOrArraySize = 1;
	resourceDesc.MipLevels = 1;
	resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.SampleDesc.Quality = 0;
	resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	resourceDesc.Width = inInfo.Size;
	resourceDesc.Flags = inInfo.Flags;
	
	if (pInfoQueue != nullptr) {
		CheckHResult(pDevice->CreateCommittedResource(&heapDesc, inInfo.HeapFlags, &resourceDesc, inInfo.State, nullptr, IID_PPV_ARGS(ppResource)));
		//CheckHResult(pInfoQueue, pDevice->CreateCommittedResource(&heapDesc, inInfo.HeapFlags, &resourceDesc, inInfo.State, nullptr, IID_PPV_ARGS(ppResource)));
	}
	else {
		CheckHResult(pDevice->CreateCommittedResource(&heapDesc, inInfo.HeapFlags, &resourceDesc, inInfo.State, nullptr, IID_PPV_ARGS(ppResource)));
	}

	return true;
}

bool D3D12Util::CreateConstantBuffer(ID3D12Device* pDevice, ID3D12Resource** ppResource, UINT64 inSize) {
	D3D12BufferCreateInfo bufferInfo((inSize + 255) & ~255, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
	CheckIsValid(CreateBuffer(pDevice, bufferInfo, ppResource));

	return true;
}

D3D12_CPU_DESCRIPTOR_HANDLE D3D12Util::GetCpuHandle(ID3D12DescriptorHeap* descHeap, INT index, UINT descriptorSize) {
	auto handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(descHeap->GetCPUDescriptorHandleForHeapStart());
	handle.Offset(index, descriptorSize);
	return handle;
}

D3D12_GPU_DESCRIPTOR_HANDLE D3D12Util::GetGpuHandle(ID3D12DescriptorHeap* descHeap, INT index, UINT descriptorSize) {
	auto handle = CD3DX12_GPU_DESCRIPTOR_HANDLE(descHeap->GetGPUDescriptorHandleForHeapStart());
	handle.Offset(index, descriptorSize);
	return handle;
}

void D3D12Util::UavBarrier(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* resource) {
	D3D12_RESOURCE_BARRIER uavBarrier;
	uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
	uavBarrier.UAV.pResource = resource;
	uavBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	cmdList->ResourceBarrier(1, &uavBarrier);
}

void D3D12Util::UavBarriers(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* resource[], size_t length) {
	std::vector<D3D12_RESOURCE_BARRIER> uavBarriers;
	for (size_t i = 0; i < length; ++i) {
		D3D12_RESOURCE_BARRIER uavBarrier;
		uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
		uavBarrier.UAV.pResource = resource[i];
		uavBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		uavBarriers.push_back(uavBarrier);
	}
	cmdList->ResourceBarrier(static_cast<UINT>(uavBarriers.size()), uavBarriers.data());
}