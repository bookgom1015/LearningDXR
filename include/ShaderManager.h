#pragma once

#include <d3d12.h>
#include <dxcapi.h>
#include <dxc/Support/dxcapi.use.h>

#include <string>
#include <unordered_map>
#include <wrl.h>

struct D3D12ShaderInfo {
	LPCWSTR		FileName		= nullptr;
	LPCWSTR		EntryPoint		= nullptr;
	LPCWSTR		TargetProfile	= nullptr;
	LPCWSTR*	Arguments		= nullptr;
	DxcDefine*	Defines			= nullptr;
	UINT32		ArgCount		= 0;
	UINT32		DefineCount		= 0;

	D3D12ShaderInfo() = default;
	D3D12ShaderInfo(LPCWSTR fileName, LPCWSTR entryPoint, LPCWSTR profile) {
		FileName = fileName;
		EntryPoint = entryPoint;
		TargetProfile = profile;
	}
	D3D12ShaderInfo(LPCWSTR fileName, LPCWSTR entryPoint, LPCWSTR profile, DxcDefine* defines, UINT32 defCount) {
		FileName = fileName;
		EntryPoint = entryPoint;
		TargetProfile = profile;
		Defines = defines;
		DefineCount = defCount;
	}
};

class ShaderManager {
public:
	ShaderManager() = default;
	virtual ~ShaderManager();

public:
	bool Initialize();
	void CleanUp();

	bool CompileShader(
		const std::wstring& inFilePath,
		const D3D_SHADER_MACRO* inDefines,
		const std::string& inEntryPoint,
		const std::string& inTarget,
		const std::string& inName);

	bool CompileShader(const D3D12ShaderInfo& inShaderInfo, const std::string& inName);

	ID3DBlob* GetShader(const std::string& inName);
	IDxcBlob* GetDxcShader(const std::string& inName);

private:
	bool bIsCleanedUp = false;

	dxc::DxcDllSupport mDxcDllHelper;
	IDxcCompiler* mCompiler = nullptr;
	IDxcLibrary* mLibrary = nullptr;

	std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3DBlob>> mShaders;
	std::unordered_map<std::string, Microsoft::WRL::ComPtr<IDxcBlob>> mDxcShaders;
};