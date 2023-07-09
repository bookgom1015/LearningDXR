#pragma once

const int gNumFrameResources = 3;

#include "LowRenderer.h"
#include "GameTimer.h"
#include "RenderItem.h"

#include <array>
#include <unordered_map>
#include <random>

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

namespace GaussianFilter { class GaussianFilterClass; }
namespace GaussianFilterCS { class GaussianFilterCSClass; }
namespace GaussianFilter3x3CS { class GaussianFilter3x3CSClass; }
namespace GBuffer {	class GBufferClass; }
namespace Shadow { class ShadowClass; }
namespace Ssao { class SsaoClass; }
namespace DxrShadow { class DxrShadowClass; }
namespace Rtao { class RtaoClass; }
namespace Debug { class DebugClass; }
namespace BackBuffer { class BackBufferClass; }

const int gNumGeometryBuffers = 64;
const int gNumObjects = 32;
const int gNumMaterials = 32;

namespace Gizmo {
	namespace RootSignatureLayout {
		enum {
			ECB_Pass = 0,
			Count
		};
	}
}

namespace NonFloatingPointMapDebug {
	namespace RootSignatureLayout {
		enum {
			ECB_Debug = 0,
			EC_Consts,
			ESI_TsppAOCoefficientSquaredMeanRayHitDistance,
			ESI_Tspp,
			Count
		};
	}

	namespace RootConstantsLayout {
		enum {
			ETextureDim_X = 0,
			ETextureDim_Y,
			Count
		};
	}
}

namespace DxrBackBuffer {
	namespace RootSignatureLayout {
		enum {
			ECB_Pass = 0,
			ESI_Color,
			ESI_Albedo,
			ESI_Normal,
			ESI_Depth,
			ESI_Specular,
			ESI_Shadow,
			ESI_AmbientCoefficient,
			Count
		};
	}
}

namespace GroundTruthDenoising {
	namespace RootSingatureLayout {
		enum {
			ESI_AmbientCoefficentMaps = 0,
			EConsts,
			Count
		};
	}
	
	namespace RootConstantsLayout {
		enum {
			EDimensionX = 0,
			EDimensionY,
			EAccumulation,
			Count
		};
	}
}

namespace EDescriptors {
	enum {
		ES_Vertices = 0,
		ES_Indices = ES_Vertices + gNumGeometryBuffers,
		ES_Font = ES_Indices + gNumGeometryBuffers,
		Count
	};
}

namespace DebugDisplay {
	namespace Layout {
		enum Type {
			EColor = 0,
			EAlbedo,
			ENormalDepth,
			EDepth,
			ESpecular,
			EVelocity,
			EScreenAO,
			EShadow,
			EDxrShadow,
			EAOCoefficient,
			ETemporalAOCoefficient,
			ELocalMeanVariance_Mean,
			ELocalMeanVariance_Var,
			EAOVariance,
			EAORayHitDistance,
			ETemporalRayHitDistance,
			EPartialDepthDerivatives,
			ETspp,
			EDisocclusionBlurStrength,
			Count
		};
	}
}

class Renderer : public LowRenderer {	
public:
	struct DebugDisplayMapInfo {
		D3D12_GPU_DESCRIPTOR_HANDLE Handle;
		UINT SampleMask;

		bool operator==(const DebugDisplayMapInfo& other) const {
			return (Handle.ptr == other.Handle.ptr) && (SampleMask == other.SampleMask);
		}
	};

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
	bool UpdateDebugCB(const GameTimer& gt);
	bool UpdateShadowPassCB(const GameTimer& gt);
	bool UpdateMaterialCB(const GameTimer& gt);
	bool UpdateBlurPassCB(const GameTimer& gt);
	bool UpdateSsaoPassCB(const GameTimer& gt);
	bool UpdateRtaoPassCB(const GameTimer& gt);

	// Drawing
	bool Rasterize();
	bool DrawShadowMap();
	bool DrawGBuffer();
	bool DrawSsao();
	bool DrawBackBuffer();

	//
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

	std::array<DirectX::XMFLOAT4, 3> mBlurWeights;

	std::vector<DebugDisplayMapInfo> mDebugDisplayMapInfos;
	std::array<bool, DebugDisplay::Layout::Count> mDebugDisplayMasks;
	UINT mNumDebugMaps;

	std::unique_ptr<GBuffer::GBufferClass> mGBuffer;
	std::unique_ptr<GaussianFilter::GaussianFilterClass> mGaussianFilter;
	std::unique_ptr<GaussianFilterCS::GaussianFilterCSClass> mGaussianFilterCS;
	std::unique_ptr<GaussianFilter3x3CS::GaussianFilter3x3CSClass> mGaussianFilter3x3CS;

	std::mt19937 mGeneratorURNG;

	std::unique_ptr<Debug::DebugClass> mDebug;
	std::unique_ptr<BackBuffer::BackBufferClass> mBackBuffer;

	//
	// Rasterization
	//
	std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3D12PipelineState>> mPSOs;

	std::vector<std::unique_ptr<RenderItem>> mAllRitems;
	std::unordered_map<RenderItem::RenderType, std::vector<RenderItem*>> mRitems;
	
	std::unique_ptr<PassConstants> mMainPassCB;
	std::unique_ptr<PassConstants> mShadowPassCB;

	D3D12_VIEWPORT mDebugViewport;
	D3D12_RECT mDebugScissorRect;

	std::unique_ptr<Shadow::ShadowClass> mShadow;
	std::unique_ptr<Ssao::SsaoClass> mSsao;

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

	std::unique_ptr<DxrShadow::DxrShadowClass> mDxrShadow;
	std::unique_ptr<Rtao::RtaoClass> mRtao;

	bool bCheckerboardSamplingEnabled;
	bool bCheckerboardGenerateRaysForEvenPixels;
};

#include "Renderer.inl"