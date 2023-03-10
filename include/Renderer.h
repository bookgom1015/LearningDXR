#pragma once

const int gNumFrameResources = 3;

#include "LowRenderer.h"
#include "GameTimer.h"
#include "MathHelper.h"

#include <array>
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
struct AccelerationStructureBuffer;

class ShadowMap;
class GBuffer;
class Ssao;

class DxrShadowMap;
class Rtao;

struct RenderItem {
	RenderItem() = default;

	// World matrix of the shape that describes the object's local space
	// relative to the world space, which defines the position, orientation,
	// and scale of the object in thw world.
	DirectX::XMFLOAT4X4 World = MathHelper::Identity4x4();
	DirectX::XMFLOAT4X4 PrevWolrd = MathHelper::Identity4x4();
	DirectX::XMFLOAT4X4 TexTransform = MathHelper::Identity4x4();

	// Dirty flag indicating the object data has changed and we need to update the constant buffer.
	// Because we have an object cbuffer for each FrameResource, we have to apply the
	// update to each FrameResource. Thus, when we modify object data we should set
	// NumFrameDirty = gNumFrameResources so that each frame resource gets the update.
	int NumFramesDirty = gNumFrameResources << 1;

	// Index into GPU constant buffer corresponding to the ObjectCB for this render item.
	UINT ObjSBIndex = -1;

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
const int gNumObjects = 32;
const int gNumMaterials = 32;

enum class ERenderTypes {
	EOpaque = 0,
	EGizmo,
	Count
};

enum class ERasterRootSignatureLayout {
	EPassCB = 0,
	EConsts,
	EObjSB,
	EMatSB,
	ESrvMaps,
	EUavMaps,
	Count
};

enum class ERasterRootConstantsLayout {
	EInstanceID = 0,
	EIsRaytracing,
	Count
};

enum class EComputeBlurRootSignatureLayout {
	EBlurPassCB = 0,
	ENormalDepth,
	EInput,
	EOutput,
	Count
};

enum class EUaBlurRootSignatureLayout {
	EBlurPassCB = 0,
	EConsts,
	ENormalDepth,
	EInput,
	EOutput,
	Count
};

enum class EUaBlurRootConstansLayout {
	EHorizontal = 0,
	Count
};

enum class ESsaoRootSignatureLayout {
	ESsaoPassCB = 0,
	ENormalDepth,
	ERandomVector,
	Count
};

enum class EBlurRootSignatureLayout {
	EBlurPassCB = 0,
	EConsts,
	ENormalDepth,
	EInput,
	Count
};

enum class EBlurRootConstantsLayout {
	EDotThreshold = 0,
	EDepthThreshold,
	EHorizontal,
	Count
};

enum class EGizmoRootSignatureLayout {
	EPassCB = 0,
	Count
};

enum class EDefaultGlobalRootSignatureLayout {
	EOutput = 0,
	EAccelerationStructure,
	EPassCB,
	EObjSB,
	EMatSB,
	EVertices,
	EIndices,
	ESrvMaps,
	EUavMaps,
	Count
};

enum class EDefaultLocalRootSignatureLayout {
	Count = 0
};

enum class ERtaoGlobalRootSignatureLayout {
	EAccelerationStructure = 0,
	ERtaoPassCB,
	ENormalDepth,
	EOutput,
	Count
};

enum class EGroundTruthAORootSingatureLayout {
	EAmbientMaps = 0,
	ENormalDepth,
	EVelocity,
	EConsts,
	Count
};

enum class EGroundTruthAORootConstantsLayout {
	EAccumulation = 0,
	Count
};

enum class EDescriptors {
	ES_Vertices = 0,
	ES_Indices	= ES_Vertices + gNumGeometryBuffers,
	ES_Font		= ES_Indices + gNumGeometryBuffers,

	ES_Color, Srv_Start = ES_Color,
	ES_Albedo,
	ES_Normal,
	ES_Depth,
	ES_Specular,
	ES_Velocity,
	ES_Shadow,
	ES_DxrShadow0,
	ES_DxrShadow1,
	ES_Ambient0, 
	ES_Ambient1, 
	ES_RandomVector, 
	ES_DxrAmbient0,
	ES_DxrAmbient1, Srv_End = ES_DxrAmbient1,

	EU_Output0,
	EU_Output1,
	EU_Output2,

	EU_DxrShadow0, Uav_Start = EU_DxrShadow0,
	EU_DxrShadow1,
	EU_DxrAmbient0,
	EU_DxrAmbient1,
	EU_Accumulation, Uav_End = EU_Accumulation,

	Count
};

enum class ERtvHeapLayout {
	EBackBuffer0 = 0,
	EBackBuffer1,
	EColor,
	EAlbedo,
	ENormal,
	ESpecular,
	EVelocity,
	EAmbient0,
	EAmbient1,
	Count
};

enum class EDsvHeapLayout {
	EDefault = 0,
	EShaadow,
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

	void DisplayImGui(bool state);

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
	bool BuildRootSignatures();
	bool BuildDescriptorHeaps();
	bool BuildDescriptors();

	// Raterization
	bool BuildPSOs();
	bool BuildRenderItems();

	// Raytracing
	bool BuildBLAS();
	bool BuildTLAS();
	bool BuildDXRPSOs();
	bool BuildShaderTables();

	// Update
	bool UpdateObjectCB(const GameTimer& gt);
	bool UpdatePassCB(const GameTimer& gt);
	bool UpdateShadowPassCB(const GameTimer& gt);
	bool UpdateMaterialCB(const GameTimer& gt);
	bool UpdateBlurPassCB(const GameTimer& gt);
	bool UpdateSsaoPassCB(const GameTimer& gt);
	bool UpdateRtaoPassCB(const GameTimer& gt);

	// Drawing
	bool Rasterize();
	bool DrawRenderItems(const std::vector<RenderItem*>& ritems);
	bool DrawShadowMap();
	bool DrawGBuffer();
	bool DrawSsao();
	bool DrawBackBuffer();


	bool DrawDebugLayer();
	bool DrawImGui();

	// Drawing for raytracing
	bool Raytrace();
	bool dxrDrawShadowMap();
	bool dxrDrawRtao();
	bool dxrDrawBackBuffer();

private:
	bool bIsCleanedUp;
	bool bInitialized;
	bool bRaytracing;
	bool bDisplayImgGui;
	bool bDisplayMaps;

	std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;
	std::unordered_map<std::string, std::unique_ptr<Material>> mMaterials;

	std::vector<std::unique_ptr<FrameResource>> mFrameResources;
	FrameResource* mCurrFrameResource;
	int mCurrFrameResourceIndex;

	std::unique_ptr<ShaderManager> mShaderManager;

	std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3D12RootSignature>> mRootSignatures;

	Camera* mCamera;

	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mCbvSrvUavHeap;

	DirectX::BoundingSphere mSceneBounds;
	DirectX::XMFLOAT3 mLightDir;

	std::unique_ptr<GBuffer> mGBuffer;

	std::array<DirectX::XMFLOAT4, 3> mBlurWeights;

	//
	// Rasterization
	//
	std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3D12PipelineState>> mPSOs;

	std::vector<std::unique_ptr<RenderItem>> mAllRitems;
	std::unordered_map<ERenderTypes, std::vector<RenderItem*>> mRitems;
	
	std::unique_ptr<PassConstants> mMainPassCB;
	std::unique_ptr<PassConstants> mShadowPassCB;

	D3D12_VIEWPORT mDebugViewport;
	D3D12_RECT mDebugScissorRect;

	std::unique_ptr<ShadowMap> mShadowMap;
	std::unique_ptr<Ssao> mSsao;
	float mSsaoRadius;
	float mSsaoFadeStart;
	float mSsaoFadeEnd;
	float mSsaoEpsilon;
	float mSsaoDotThreshold;
	float mSsaoDepthThreshold;
	int mNumSsaoBlurs;

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

	std::unique_ptr<DxrShadowMap> mDxrShadowMap;
	std::unique_ptr<Rtao> mRtao;
	float mRtaoRadius;
	float mRtaoFadeStart;
	float mRtaoFadeEnd;
	float mRtaoEpsilon;	
	UINT mRtaoAccum;
	UINT mRtaoSampleCount;

	int mNumDxrShadowBlurs;
};

#include "Renderer.inl"