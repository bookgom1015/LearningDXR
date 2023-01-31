#pragma once

const int gNumFrameResources = 3;

#include "LowRenderer.h"
#include "GameTimer.h"
#include "MathHelper.h"

#include <unordered_map>

const std::wstring ShaderFilePathW = L".\\..\\..\\assets\\shaders\\";
const std::string ShaderFilePath = ".\\..\\..\\assets\\shaders\\";

class Camera;
class ShaderManager;

struct MeshGeometry;
struct Material;
struct FrameResource;
struct DXRObjectCB;
struct PassConstants;
struct DebugPassConstants;
struct AccelerationStructureBuffer;

struct RenderItem {
	RenderItem() = default;

	// World matrix of the shape that describes the object's local space
	// relative to the world space, which defines the position, orientation,
	// and scale of the object in thw world.
	DirectX::XMFLOAT4X4 World = MathHelper::Identity4x4();

	DirectX::XMFLOAT4X4 TexTransform = MathHelper::Identity4x4();

	// Dirty flag indicating the object data has changed and we need to update the constant buffer.
	// Because we have an object cbuffer for each FrameResource, we have to apply the
	// update to each FrameResource. Thus, when we modify object data we should set
	// NumFrameDirty = gNumFrameResources so that each frame resource gets the update.
	int NumFramesDirty = gNumFrameResources;

	// Index into GPU constant buffer corresponding to the ObjectCB for this render item.
	UINT ObjCBIndex = -1;

	Material* Mat = nullptr;
	MeshGeometry* Geo = nullptr;

	// Primitive topology.
	D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	// DrawIndexedInstanced parameters.
	UINT IndexCount = 0;
	UINT StartIndexLocation = 0;
	int BaseVertexLocation = 0;
};

const int gNumGeometryBuffers = 64;

enum class ERenderTypes {
	EOpaque = 0,
	EGizmo,
	Count
};

enum class ERasterRootSignatureParams {
	EObjectCB = 0,
	EPassCB,
	EMatCB,
	Count
};

enum class EDebugRootSignatureParams {
	EPassCB = 0,
	Count
};

enum class EGlobalRootSignatureParams {
	EOutput = 0,
	EAccelerationStructure,
	EPassCB,
	EVertices,
	EIndices,
	Count
};

enum class ELocalRootSignatureParams {
	EMatCB = 0,
	Count
};

enum class EDescriptors {
	ES_Vertices = 0,
	ES_Indices	= ES_Vertices + gNumGeometryBuffers,
	EU_Output	= ES_Indices + gNumGeometryBuffers,
	ES_Font,
	Count
};


class Renderer : public LowRenderer {	
public:
	Renderer();
	virtual ~Renderer();

private:
	Renderer(const Renderer& ref) = delete;
	Renderer(Renderer&& rval) = delete;
	Renderer& operator=(const Renderer& ref) = delete;
	Renderer& operator=(Renderer&& rval) = delete;

public:
	virtual bool Initialize(HWND hMainWnd, UINT width, UINT height) override;
	virtual void CleanUp() override;

	bool Update(const GameTimer& gt);
	bool Draw();

	virtual bool OnResize(UINT width, UINT height) override;

	__forceinline constexpr bool GetRenderType() const;
	void SetRenderType(bool bRaytrace);

	void SetCamera(Camera* pCam);

	__forceinline constexpr bool Initialized() const;

	void ShowImGui(bool state);

protected:
	virtual bool CreateRtvAndDsvDescriptorHeaps() override;

	bool InitImGui();
	void CleanUpImGui();
	void BuildDebugViewport();

	// Shared
	bool CompileShaders();
	bool BuildFrameResources();
	bool BuildGeometries();
	bool BuildMaterials();
	bool BuildResources();
	bool BuildDescriptorHeaps();
	bool BuildDescriptors();

	// Raterization
	bool BuildRootSignatures();
	bool BuildPSOs();
	bool BuildRenderItems();

	// Raytracing
	bool BuildDXRRootSignatures();
	bool BuildBLAS();
	bool BuildTLAS();
	bool BuildDXRPSOs();
	bool BuildShaderTables();

	bool UpdateObjectCB(const GameTimer& gt);
	bool UpdatePassCB(const GameTimer& gt);
	bool UpdateMaterialCB(const GameTimer& gt);
	bool UpdateDebugPassCB(const GameTimer& gt);

	bool Rasterize();
	bool Raytrace();
	bool DrawDebugLayer();
	bool DrawImGui();

	bool DrawRenderItems(const std::vector<RenderItem*>& ritems);

private:
	bool bIsCleanedUp;
	bool bInitialized;
	bool bRaytracing;
	bool bShowImgGui;

	std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;
	std::unordered_map<std::string, std::unique_ptr<Material>> mMaterials;

	std::vector<std::unique_ptr<FrameResource>> mFrameResources;
	FrameResource* mCurrFrameResource;
	int mCurrFrameResourceIndex;

	std::unique_ptr<ShaderManager> mShaderManager;

	std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3D12RootSignature>> mRootSignatures;

	Camera* mCamera;

	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mDescriptorHeap;

	//
	// Rasterization
	//
	std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3D12PipelineState>> mPSOs;

	std::vector<std::unique_ptr<RenderItem>> mAllRitems;
	std::unordered_map<ERenderTypes, std::vector<RenderItem*>> mRitems;
	
	std::unique_ptr<PassConstants> mMainPassCB;
	std::unique_ptr <DebugPassConstants> mDebugPassCB;

	D3D12_VIEWPORT mDebugViewport;
	D3D12_RECT mDebugScissorRect;

	//
	// Raytracing
	//
	std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> mDXROutputs;
	
	std::unordered_map<std::string, std::unique_ptr<AccelerationStructureBuffer>> mBLASs;
	std::unique_ptr<AccelerationStructureBuffer> mTLAS;

	std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3D12StateObject>> mDXRPSOs;
	std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3D12StateObjectProperties>> mDXRPSOProps;

	std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3D12Resource>> mShaderTables;

	int mGeometryBufferCount;
};

#include "Renderer.inl"