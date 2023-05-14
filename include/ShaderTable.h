#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <vector>
#include <unordered_map>

class GpuUploadBuffer {
protected:
	GpuUploadBuffer() = default;
	virtual ~GpuUploadBuffer();

public:
	Microsoft::WRL::ComPtr<ID3D12Resource> GetResource();

protected:
	bool Allocate(ID3D12Device* pDevice, UINT bufferSize, LPCWSTR resourceName = nullptr);
	bool MapCpuWriteOnly(std::uint8_t*& pData);

protected:
	Microsoft::WRL::ComPtr<ID3D12Resource> mResource;
};

// Shader record = {{Shader ID}, {RootArguments}}
class ShaderRecord {
public:
	ShaderRecord(void* pShaderIdentifier, UINT shaderIdentifierSize);
	ShaderRecord(void* pShaderIdentifier, UINT shaderIdentifierSize, void* pLocalRootArguments, UINT localRootArgumentsSize);

public:
	void CopyTo(void* dest) const;

	struct PointerWithSize {
		void* Ptr;
		UINT Size;

		PointerWithSize();
		PointerWithSize(void* ptr, UINT size);
	};

public:
	PointerWithSize mShaderIdentifier;
	PointerWithSize mLocalRootArguments;
};

// Shader table = {{ ShaderRecord 1}, {ShaderRecord 2}, ...}
class ShaderTable : public GpuUploadBuffer {
public:
	ShaderTable(ID3D12Device* device, UINT numShaderRecords, UINT shaderRecordSize, LPCWSTR resourceName = nullptr);

public:
	bool Initialze();
	bool push_back(const ShaderRecord& shaderRecord);

	std::uint8_t* GetMappedShaderRecords();
	UINT GetShaderRecordSize();

	// Pretty-print the shader records.
	void DebugPrint(std::unordered_map<void*, std::wstring>& shaderIdToStringMap);

protected:
	ID3D12Device* mDevice;

	std::uint8_t* mMappedShaderRecords;
	UINT mShaderRecordSize;
	UINT mBufferSize;

	// Debug support
	std::wstring mName;
	std::vector<ShaderRecord> mShaderRecords;
};