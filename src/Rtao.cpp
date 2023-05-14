#include "Rtao.h"
#include "Logger.h"
#include "D3D12Util.h"
#include "ShaderManager.h"
#include "ShaderTable.h"
#include "HlslCompaction.h"
#include "ShadingHelpers.h"

#include <DirectXColors.h>

#undef max

using namespace DirectX;
using namespace DirectX::PackedVector;
using namespace Rtao;

bool RtaoClass::Initialize(ID3D12Device5*const device, ID3D12GraphicsCommandList*const cmdList, ShaderManager*const manager, UINT width, UINT height) {
	md3dDevice = device;
	mShaderManager = manager;

	mWidth = width;
	mHeight = height;

	bSwitch = false;
	bResourceState = false;

	CheckIsValid(BuildResource(cmdList));

	mTemporalCurrentFrameResourceIndex = 0;
	mTemporalCurrentFrameTemporalAOCeofficientResourceIndex = 0;

	return true;
}

bool RtaoClass::CompileShaders(const std::wstring& filePath) {
	{
		const auto path = filePath + L"TemporalSupersamplingReverseReprojectCS.hlsl";
		auto shaderInfo = D3D12ShaderInfo(path.c_str(), L"CS", L"cs_6_3");
		CheckIsValid(mShaderManager->CompileShader(shaderInfo, "tsppReprojCS"));
	}
	{
		const auto path = filePath + L"TemporalSupersamplingBlendWithCurrentFrameCS.hlsl";
		auto shaderInfo = D3D12ShaderInfo(path.c_str(), L"CS", L"cs_6_3");
		CheckIsValid(mShaderManager->CompileShader(shaderInfo, "tsppBlendCS"));
	}
	{
		const auto path = filePath + L"CalculatePartialDerivativeCS.hlsl";
		auto shaderInfo = D3D12ShaderInfo(path.c_str(), L"CS", L"cs_6_3");
		CheckIsValid(mShaderManager->CompileShader(shaderInfo, "partialDerivativeCS"));
	}
	{
		const auto path = filePath + L"CalculateLocalMeanVarianceCS.hlsl";
		auto shaderInfo = D3D12ShaderInfo(path.c_str(), L"CS", L"cs_6_3");
		CheckIsValid(mShaderManager->CompileShader(shaderInfo, "calcLocalMeanVarianceCS"));
	}
	{
		const auto path = filePath + L"FillInCheckerboardCS.hlsl";
		auto shaderInfo = D3D12ShaderInfo(path.c_str(), L"CS", L"cs_6_3");
		CheckIsValid(mShaderManager->CompileShader(shaderInfo, "fillInCheckerboardCS"));
	}
	{
		const auto path = filePath + L"EdgeStoppingFilter_Gaussian3x3CS.hlsl";
		auto shaderInfo = D3D12ShaderInfo(path.c_str(), L"CS", L"cs_6_3");
		CheckIsValid(mShaderManager->CompileShader(shaderInfo, "edgeStoppingFilter_Gaussian3x3CS"));
	}
	{
		const auto path = filePath + L"DisocclusionBlur3x3CS.hlsl";
		auto shaderInfo = D3D12ShaderInfo(path.c_str(), L"CS", L"cs_6_3");
		CheckIsValid(mShaderManager->CompileShader(shaderInfo, "disocclusionBlur3x3CS"));
	}

	return true;
}

bool RtaoClass::BuildRootSignatures(const StaticSamplers& samplers) {
	// Ray-traced ambient occlusion
	{
		CD3DX12_DESCRIPTOR_RANGE texTables[4];
		texTables[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);
		texTables[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2);
		texTables[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);
		texTables[3].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 1);

		CD3DX12_ROOT_PARAMETER slotRootParameter[CalcAmbientOcclusion::RootSignatureLayout::Count];
		slotRootParameter[CalcAmbientOcclusion::RootSignatureLayout::ESI_AccelerationStructure].InitAsShaderResourceView(0);
		slotRootParameter[CalcAmbientOcclusion::RootSignatureLayout::ECB_RtaoPass].InitAsConstantBufferView(0);
		slotRootParameter[CalcAmbientOcclusion::RootSignatureLayout::EC_Consts].InitAsConstants(CalcAmbientOcclusion::RootConstantsLayout::Count, 1, 0);
		slotRootParameter[CalcAmbientOcclusion::RootSignatureLayout::ESI_Normal].InitAsDescriptorTable(1, &texTables[0]);
		slotRootParameter[CalcAmbientOcclusion::RootSignatureLayout::ESI_Depth].InitAsDescriptorTable(1, &texTables[1]);
		slotRootParameter[CalcAmbientOcclusion::RootSignatureLayout::EUO_AOCoefficient].InitAsDescriptorTable(1, &texTables[2]);
		slotRootParameter[CalcAmbientOcclusion::RootSignatureLayout::EUO_RayHitDistance].InitAsDescriptorTable(1, &texTables[3]);

		CD3DX12_ROOT_SIGNATURE_DESC globalRootSignatureDesc(
			_countof(slotRootParameter), slotRootParameter,
			static_cast<UINT>(samplers.size()), samplers.data(),
			D3D12_ROOT_SIGNATURE_FLAG_NONE
		);
		CheckIsValid(D3D12Util::CreateRootSignature(md3dDevice, globalRootSignatureDesc, mRootSignatures["rtao"].GetAddressOf()));
	}
	// Temporal supersampling reverse reproject
	{
		CD3DX12_DESCRIPTOR_RANGE texTables[11];
		texTables[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
		texTables[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);
		texTables[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2);
		texTables[3].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 3);
		texTables[4].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 4);
		texTables[5].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 5);
		texTables[6].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 6);
		texTables[7].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 7);
		texTables[8].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 8);
		texTables[9].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);
		texTables[10].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 1);

		CD3DX12_ROOT_PARAMETER slotRootParameter[TemporalSupersamplingReverseReproject::RootSignatureLayout::Count];
		slotRootParameter[TemporalSupersamplingReverseReproject::RootSignatureLayout::ECB_CrossBilateralFilter].InitAsConstantBufferView(0);
		slotRootParameter[TemporalSupersamplingReverseReproject::RootSignatureLayout::EC_Consts].InitAsConstants(TemporalSupersamplingReverseReproject::RootConstantsLayout::Count, 1);
		slotRootParameter[TemporalSupersamplingReverseReproject::RootSignatureLayout::ESI_NormalDepth].InitAsDescriptorTable(1, &texTables[0]);
		slotRootParameter[TemporalSupersamplingReverseReproject::RootSignatureLayout::ESI_DepthPartialDerivative].InitAsDescriptorTable(1, &texTables[1]);
		slotRootParameter[TemporalSupersamplingReverseReproject::RootSignatureLayout::ESI_ReprojectedNormalDepth].InitAsDescriptorTable(1, &texTables[2]);
		slotRootParameter[TemporalSupersamplingReverseReproject::RootSignatureLayout::ESI_CachedNormalDepth].InitAsDescriptorTable(1, &texTables[3]);
		slotRootParameter[TemporalSupersamplingReverseReproject::RootSignatureLayout::ESI_Velocity].InitAsDescriptorTable(1, &texTables[4]);
		slotRootParameter[TemporalSupersamplingReverseReproject::RootSignatureLayout::ESI_CachedAOCoefficient].InitAsDescriptorTable(1, &texTables[5]);
		slotRootParameter[TemporalSupersamplingReverseReproject::RootSignatureLayout::ESI_CachedTspp].InitAsDescriptorTable(1, &texTables[6]);
		slotRootParameter[TemporalSupersamplingReverseReproject::RootSignatureLayout::ESI_CachedAOCoefficientSquaredMean].InitAsDescriptorTable(1, &texTables[7]);
		slotRootParameter[TemporalSupersamplingReverseReproject::RootSignatureLayout::ESI_CachedRayHitDistance].InitAsDescriptorTable(1, &texTables[8]);
		slotRootParameter[TemporalSupersamplingReverseReproject::RootSignatureLayout::EUO_CachedTspp].InitAsDescriptorTable(1, &texTables[9]);
		slotRootParameter[TemporalSupersamplingReverseReproject::RootSignatureLayout::EUO_TsppCoefficientSquaredMeanRayHitDistacne].InitAsDescriptorTable(1, &texTables[10]);
		
		CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc(
			_countof(slotRootParameter), slotRootParameter,
			static_cast<UINT>(samplers.size()), samplers.data(),
			D3D12_ROOT_SIGNATURE_FLAG_NONE
		);
		CheckIsValid(D3D12Util::CreateRootSignature(md3dDevice, rootSignatureDesc, mRootSignatures["tsppReproj"].GetAddressOf()));
	}
	// Temporal supersampling blend with current frame
	{
		CD3DX12_DESCRIPTOR_RANGE texTables[10];
		texTables[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0);
		texTables[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 0);
		texTables[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2, 0);
		texTables[3].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 3, 0);
		texTables[4].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0);
		texTables[5].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 1, 0);
		texTables[6].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 2, 0);
		texTables[7].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 3, 0);
		texTables[8].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 4, 0);
		texTables[9].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 5, 0);

		CD3DX12_ROOT_PARAMETER slotRootParameter[TemporalSupersamplingBlendWithCurrentFrame::RootSignatureLayout::Count];
		slotRootParameter[TemporalSupersamplingBlendWithCurrentFrame::RootSignatureLayout::ECB_TsspBlendWithCurrentFrame].InitAsConstantBufferView(0);
		slotRootParameter[TemporalSupersamplingBlendWithCurrentFrame::RootSignatureLayout::ESI_AOCoefficient].InitAsDescriptorTable(1, &texTables[0]);
		slotRootParameter[TemporalSupersamplingBlendWithCurrentFrame::RootSignatureLayout::ESI_LocalMeanVaraince].InitAsDescriptorTable(1, &texTables[1]);
		slotRootParameter[TemporalSupersamplingBlendWithCurrentFrame::RootSignatureLayout::ESI_RayHitDistance].InitAsDescriptorTable(1, &texTables[2]);
		slotRootParameter[TemporalSupersamplingBlendWithCurrentFrame::RootSignatureLayout::ESI_TsppCoefficientSquaredMeanRayHitDistance].InitAsDescriptorTable(1, &texTables[3]);
		slotRootParameter[TemporalSupersamplingBlendWithCurrentFrame::RootSignatureLayout::EUIO_TemporalAOCoefficient].InitAsDescriptorTable(1, &texTables[4]);
		slotRootParameter[TemporalSupersamplingBlendWithCurrentFrame::RootSignatureLayout::EUIO_Tspp].InitAsDescriptorTable(1, &texTables[5]);
		slotRootParameter[TemporalSupersamplingBlendWithCurrentFrame::RootSignatureLayout::EUIO_CoefficientSquaredMean].InitAsDescriptorTable(1, &texTables[6]);
		slotRootParameter[TemporalSupersamplingBlendWithCurrentFrame::RootSignatureLayout::EUIO_RayHitDistance].InitAsDescriptorTable(1, &texTables[7]);
		slotRootParameter[TemporalSupersamplingBlendWithCurrentFrame::RootSignatureLayout::EUO_VarianceMap].InitAsDescriptorTable(1, &texTables[8]);
		slotRootParameter[TemporalSupersamplingBlendWithCurrentFrame::RootSignatureLayout::EUO_BlurStrength].InitAsDescriptorTable(1, &texTables[9]);
		
		CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc(
			_countof(slotRootParameter), slotRootParameter,
			static_cast<UINT>(samplers.size()), samplers.data(),
			D3D12_ROOT_SIGNATURE_FLAG_NONE
		);
		CheckIsValid(D3D12Util::CreateRootSignature(md3dDevice, rootSignatureDesc, mRootSignatures["tsppBlend"].GetAddressOf()));
	}
	// CalculateDepthPartialDerivative
	{
		CD3DX12_DESCRIPTOR_RANGE texTables[2];
		texTables[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0);
		texTables[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0);

		CD3DX12_ROOT_PARAMETER slotRootParameter[CalcDepthPartialDerivative::RootSignatureLayout::Count];
		slotRootParameter[CalcDepthPartialDerivative::RootSignatureLayout::EC_Consts].InitAsConstants(CalcDepthPartialDerivative::RootConstantsLayout::Count, 0, 0);
		slotRootParameter[CalcDepthPartialDerivative::RootSignatureLayout::ESI_Depth].InitAsDescriptorTable(1, &texTables[0]);
		slotRootParameter[CalcDepthPartialDerivative::RootSignatureLayout::EUO_DepthPartialDerivative].InitAsDescriptorTable(1, &texTables[1]);
		
		CD3DX12_ROOT_SIGNATURE_DESC globalRootSignatureDesc(
			_countof(slotRootParameter), slotRootParameter,
			static_cast<UINT>(samplers.size()), samplers.data(),
			D3D12_ROOT_SIGNATURE_FLAG_NONE
		);
		CheckIsValid(D3D12Util::CreateRootSignature(md3dDevice, globalRootSignatureDesc, mRootSignatures["partialDerivative"].GetAddressOf()));
	}
	// CalculateMeanVariance
	{
		CD3DX12_DESCRIPTOR_RANGE texTables[2];
		texTables[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0);
		texTables[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0);

		CD3DX12_ROOT_PARAMETER slotRootParameter[CalcLocalMeanVariance::RootSignatureLayout::Count];
		slotRootParameter[CalcLocalMeanVariance::RootSignatureLayout::ECB_LocalMeanVar].InitAsConstantBufferView(0, 0);
		slotRootParameter[CalcLocalMeanVariance::RootSignatureLayout::ESI_AOCoefficient].InitAsDescriptorTable(1, &texTables[0]);
		slotRootParameter[CalcLocalMeanVariance::RootSignatureLayout::EUO_LocalMeanVar].InitAsDescriptorTable(1, &texTables[1]);
		
		CD3DX12_ROOT_SIGNATURE_DESC globalRootSignatureDesc(
			_countof(slotRootParameter), slotRootParameter,
			static_cast<UINT>(samplers.size()), samplers.data(),
			D3D12_ROOT_SIGNATURE_FLAG_NONE
		);
		CheckIsValid(D3D12Util::CreateRootSignature(md3dDevice, globalRootSignatureDesc, mRootSignatures["localMeanVariance"].GetAddressOf()));
	}
	// FillInCheckerboard
	{
		CD3DX12_DESCRIPTOR_RANGE texTables[1];
		texTables[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0);

		CD3DX12_ROOT_PARAMETER slotRootParameter[FillInCheckerboard::RootSignatureLayout::Count];
		slotRootParameter[FillInCheckerboard::RootSignatureLayout::ECB_LocalMeanVar].InitAsConstantBufferView(0, 0);
		slotRootParameter[FillInCheckerboard::RootSignatureLayout::EUIO_LocalMeanVar].InitAsDescriptorTable(1, &texTables[0]);
		
		CD3DX12_ROOT_SIGNATURE_DESC globalRootSignatureDesc(
			_countof(slotRootParameter), slotRootParameter,
			static_cast<UINT>(samplers.size()), samplers.data(),
			D3D12_ROOT_SIGNATURE_FLAG_NONE
		);
		CheckIsValid(D3D12Util::CreateRootSignature(md3dDevice, globalRootSignatureDesc, mRootSignatures["fillInCheckerboard"].GetAddressOf()));
	}
	// Atrous Wavelet transform filter
	{
		CD3DX12_DESCRIPTOR_RANGE texTables[7];
		texTables[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0);
		texTables[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 0);
		texTables[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2, 0);
		texTables[3].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 3, 0);
		texTables[4].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 4, 0);
		texTables[5].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 5, 0);
		texTables[6].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0);

		CD3DX12_ROOT_PARAMETER slotRootParameter[AtrousWaveletTransformFilter::RootSignatureLayout::Count];
		slotRootParameter[AtrousWaveletTransformFilter::RootSignatureLayout::ECB_AtrousFilter].InitAsConstantBufferView(0, 0);
		slotRootParameter[AtrousWaveletTransformFilter::RootSignatureLayout::ESI_TemporalAOCoefficient].InitAsDescriptorTable(1, &texTables[0]);
		slotRootParameter[AtrousWaveletTransformFilter::RootSignatureLayout::ESI_NormalDepth].InitAsDescriptorTable(1, &texTables[1]);
		slotRootParameter[AtrousWaveletTransformFilter::RootSignatureLayout::ESI_Variance].InitAsDescriptorTable(1, &texTables[2]);
		slotRootParameter[AtrousWaveletTransformFilter::RootSignatureLayout::ESI_HitDistance].InitAsDescriptorTable(1, &texTables[3]);
		slotRootParameter[AtrousWaveletTransformFilter::RootSignatureLayout::ESI_DepthPartialDerivative].InitAsDescriptorTable(1, &texTables[4]);
		slotRootParameter[AtrousWaveletTransformFilter::RootSignatureLayout::ESI_Tspp].InitAsDescriptorTable(1, &texTables[5]);
		slotRootParameter[AtrousWaveletTransformFilter::RootSignatureLayout::EUO_TemporalAOCoefficient].InitAsDescriptorTable(1, &texTables[6]);

		CD3DX12_ROOT_SIGNATURE_DESC globalRootSignatureDesc(
			_countof(slotRootParameter), slotRootParameter,
			0, nullptr,
			D3D12_ROOT_SIGNATURE_FLAG_NONE
		);
		CheckIsValid(D3D12Util::CreateRootSignature(md3dDevice, globalRootSignatureDesc, mRootSignatures["atrousWaveletTransformFilter"].GetAddressOf()));
	}
	// Disocclusion blur
	{
		CD3DX12_DESCRIPTOR_RANGE texTables[3];
		texTables[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0);
		texTables[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 0);
		texTables[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0);

		CD3DX12_ROOT_PARAMETER slotRootParameter[DisocclusionBlur::RootSignatureLayout::Count];
		slotRootParameter[DisocclusionBlur::RootSignatureLayout::EC_Consts].InitAsConstants(DisocclusionBlur::RootConstantsLayout::Count, 0);
		slotRootParameter[DisocclusionBlur::RootSignatureLayout::ESI_Depth].InitAsDescriptorTable(1, &texTables[0]);
		slotRootParameter[DisocclusionBlur::RootSignatureLayout::ESI_BlurStrength].InitAsDescriptorTable(1, &texTables[1]);
		slotRootParameter[DisocclusionBlur::RootSignatureLayout::EUIO_AOCoefficient].InitAsDescriptorTable(1, &texTables[2]);

		CD3DX12_ROOT_SIGNATURE_DESC globalRootSignatureDesc(
			_countof(slotRootParameter), slotRootParameter,
			0, nullptr,
			D3D12_ROOT_SIGNATURE_FLAG_NONE
		);
		CheckIsValid(D3D12Util::CreateRootSignature(md3dDevice, globalRootSignatureDesc, mRootSignatures["disocclusionBlur"].GetAddressOf()));
	}
	return true;
}

bool RtaoClass::BuildPSO() {
	D3D12_COMPUTE_PIPELINE_STATE_DESC tsppReprojPsoDesc = {};
	tsppReprojPsoDesc.pRootSignature = mRootSignatures["tsppReproj"].Get();
	{
		auto cs = mShaderManager->GetDxcShader("tsppReprojCS");
		tsppReprojPsoDesc.CS = {
			reinterpret_cast<BYTE*>(cs->GetBufferPointer()),
			cs->GetBufferSize()
		};
	}
	tsppReprojPsoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
	CheckHResult(md3dDevice->CreateComputePipelineState(&tsppReprojPsoDesc, IID_PPV_ARGS(&mPSOs["tsppReproj"])));

	D3D12_COMPUTE_PIPELINE_STATE_DESC tsppBlendPsoDesc = {};
	tsppBlendPsoDesc.pRootSignature = mRootSignatures["tsppBlend"].Get();
	{
		auto cs = mShaderManager->GetDxcShader("tsppBlendCS");
		tsppBlendPsoDesc.CS = {
			reinterpret_cast<BYTE*>(cs->GetBufferPointer()),
			cs->GetBufferSize()
		};
	}
	tsppBlendPsoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
	CheckHResult(md3dDevice->CreateComputePipelineState(&tsppBlendPsoDesc, IID_PPV_ARGS(&mPSOs["tsppBlend"])));

	D3D12_COMPUTE_PIPELINE_STATE_DESC calcPartialDerivativePsoDesc = {};
	calcPartialDerivativePsoDesc.pRootSignature = mRootSignatures["partialDerivative"].Get();
	{
		auto cs = mShaderManager->GetDxcShader("partialDerivativeCS");
		calcPartialDerivativePsoDesc.CS = {
			reinterpret_cast<BYTE*>(cs->GetBufferPointer()),
			cs->GetBufferSize()
		};
	}
	calcPartialDerivativePsoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
	CheckHResult(md3dDevice->CreateComputePipelineState(&calcPartialDerivativePsoDesc, IID_PPV_ARGS(&mPSOs["partialDerivative"])));

	D3D12_COMPUTE_PIPELINE_STATE_DESC calcLocalMeanVariancePsoDesc = {};
	calcLocalMeanVariancePsoDesc.pRootSignature = mRootSignatures["localMeanVariance"].Get();
	{
		auto cs = mShaderManager->GetDxcShader("calcLocalMeanVarianceCS");
		calcLocalMeanVariancePsoDesc.CS = {
			reinterpret_cast<BYTE*>(cs->GetBufferPointer()),
			cs->GetBufferSize()
		};
	}
	calcLocalMeanVariancePsoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
	CheckHResult(md3dDevice->CreateComputePipelineState(&calcLocalMeanVariancePsoDesc, IID_PPV_ARGS(&mPSOs["localMeanVariance"])));

	D3D12_COMPUTE_PIPELINE_STATE_DESC fillInCheckerboardPsoDesc = {};
	fillInCheckerboardPsoDesc.pRootSignature = mRootSignatures["fillInCheckerboard"].Get();
	{
		auto cs = mShaderManager->GetDxcShader("fillInCheckerboardCS");
		fillInCheckerboardPsoDesc.CS = {
			reinterpret_cast<BYTE*>(cs->GetBufferPointer()),
			cs->GetBufferSize()
		};
	}
	fillInCheckerboardPsoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
	CheckHResult(md3dDevice->CreateComputePipelineState(&fillInCheckerboardPsoDesc, IID_PPV_ARGS(&mPSOs["fillInCheckerboard"])));

	D3D12_COMPUTE_PIPELINE_STATE_DESC atrousWaveletTransformFilterPsoDesc = {};
	atrousWaveletTransformFilterPsoDesc.pRootSignature = mRootSignatures["atrousWaveletTransformFilter"].Get();
	{
		auto cs = mShaderManager->GetDxcShader("edgeStoppingFilter_Gaussian3x3CS");
		atrousWaveletTransformFilterPsoDesc.CS = {
			reinterpret_cast<BYTE*>(cs->GetBufferPointer()),
			cs->GetBufferSize()
		};
	}
	atrousWaveletTransformFilterPsoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
	CheckHResult(md3dDevice->CreateComputePipelineState(&atrousWaveletTransformFilterPsoDesc, IID_PPV_ARGS(&mPSOs["atrousWaveletTransformFilter"])));

	D3D12_COMPUTE_PIPELINE_STATE_DESC disocclusionBlurPsoDesc = {};
	disocclusionBlurPsoDesc.pRootSignature = mRootSignatures["disocclusionBlur"].Get();
	{
		auto cs = mShaderManager->GetDxcShader("disocclusionBlur3x3CS");
		disocclusionBlurPsoDesc.CS = {
			reinterpret_cast<BYTE*>(cs->GetBufferPointer()),
			cs->GetBufferSize()
		};
	}
	disocclusionBlurPsoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
	CheckHResult(md3dDevice->CreateComputePipelineState(&disocclusionBlurPsoDesc, IID_PPV_ARGS(&mPSOs["disocclusionBlur"])));

	return true;
}

bool RtaoClass::BuildDXRPSO() {
	CD3DX12_STATE_OBJECT_DESC rtaoDxrPso = { D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE };

	auto rtaoLib = rtaoDxrPso.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
	auto rtaoShader = mShaderManager->GetDxcShader("rtao");
	D3D12_SHADER_BYTECODE rtaoLibDxil = CD3DX12_SHADER_BYTECODE(rtaoShader->GetBufferPointer(), rtaoShader->GetBufferSize());
	rtaoLib->SetDXILLibrary(&rtaoLibDxil);
	LPCWSTR rtaoExports[] = { L"RtaoRayGen", L"RtaoClosestHit", L"RtaoMiss" };
	rtaoLib->DefineExports(rtaoExports);

	auto rtaoHitGroup = rtaoDxrPso.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();
	rtaoHitGroup->SetClosestHitShaderImport(L"RtaoClosestHit");
	rtaoHitGroup->SetHitGroupExport(L"RtaoHitGroup");
	rtaoHitGroup->SetHitGroupType(D3D12_HIT_GROUP_TYPE_TRIANGLES);

	auto shaderConfig = rtaoDxrPso.CreateSubobject<CD3DX12_RAYTRACING_SHADER_CONFIG_SUBOBJECT>();
	UINT payloadSize = sizeof(float) /* tHit */;
	UINT attribSize = sizeof(XMFLOAT2);
	shaderConfig->Config(payloadSize, attribSize);

	auto glbalRootSig = rtaoDxrPso.CreateSubobject<CD3DX12_GLOBAL_ROOT_SIGNATURE_SUBOBJECT>();
	glbalRootSig->SetRootSignature(mRootSignatures["rtao"].Get());

	auto pipelineConfig = rtaoDxrPso.CreateSubobject<CD3DX12_RAYTRACING_PIPELINE_CONFIG_SUBOBJECT>();
	UINT maxRecursionDepth = 1;
	pipelineConfig->Config(maxRecursionDepth);

	CheckHResult(md3dDevice->CreateStateObject(rtaoDxrPso, IID_PPV_ARGS(&mDXRPSO)));
	CheckHResult(mDXRPSO->QueryInterface(IID_PPV_ARGS(&mDXRPSOProp)));

	return true;
}

bool RtaoClass::BuildShaderTables() {
	UINT shaderIdentifierSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;

	const auto props = mDXRPSOProp.Get();
	void* rtaoRayGenShaderIdentifier = props->GetShaderIdentifier(L"RtaoRayGen");
	void* rtaoMissShaderIdentifier = props->GetShaderIdentifier(L"RtaoMiss");
	void* rtaoHitGroupShaderIdentifier = props->GetShaderIdentifier(L"RtaoHitGroup");

	ShaderTable rtaoRayGenShaderTable(md3dDevice, 1, shaderIdentifierSize);
	CheckIsValid(rtaoRayGenShaderTable.Initialze());
	rtaoRayGenShaderTable.push_back(ShaderRecord(rtaoRayGenShaderIdentifier, shaderIdentifierSize));
	mShaderTables["rtaoRayGen"] = rtaoRayGenShaderTable.GetResource();

	ShaderTable rtaoMissShaderTable(md3dDevice, 1, shaderIdentifierSize);
	CheckIsValid(rtaoMissShaderTable.Initialze());
	rtaoMissShaderTable.push_back(ShaderRecord(rtaoMissShaderIdentifier, shaderIdentifierSize));
	mShaderTables["rtaoMiss"] = rtaoMissShaderTable.GetResource();

	ShaderTable rtaoHitGroupTable(md3dDevice, 1, shaderIdentifierSize);
	CheckIsValid(rtaoHitGroupTable.Initialze());
	rtaoHitGroupTable.push_back(ShaderRecord(rtaoHitGroupShaderIdentifier, shaderIdentifierSize));
	mShaderTables["rtaoHitGroup"] = rtaoHitGroupTable.GetResource();

	return true;
}

void RtaoClass::RunCalculatingAmbientOcclusion(
		ID3D12GraphicsCommandList4*const cmdList,
		D3D12_GPU_VIRTUAL_ADDRESS accelStruct,
		D3D12_GPU_VIRTUAL_ADDRESS cbAddress,
		D3D12_GPU_DESCRIPTOR_HANDLE si_normal,
		D3D12_GPU_DESCRIPTOR_HANDLE si_depth,
		D3D12_GPU_DESCRIPTOR_HANDLE uo_aoCoefficient,
		D3D12_GPU_DESCRIPTOR_HANDLE uo_rayHitDistance) {
	cmdList->SetPipelineState1(mDXRPSO.Get());
	cmdList->SetComputeRootSignature(mRootSignatures["rtao"].Get());

	cmdList->SetComputeRootShaderResourceView(CalcAmbientOcclusion::RootSignatureLayout::ESI_AccelerationStructure, accelStruct);
	cmdList->SetComputeRootConstantBufferView(CalcAmbientOcclusion::RootSignatureLayout::ECB_RtaoPass, cbAddress);

	const UINT values[CalcAmbientOcclusion::RootConstantsLayout::Count] = { mWidth, mHeight };
	cmdList->SetComputeRoot32BitConstants(CalcAmbientOcclusion::RootSignatureLayout::EC_Consts, _countof(values), values, 0);

	cmdList->SetComputeRootDescriptorTable(CalcAmbientOcclusion::RootSignatureLayout::ESI_Normal, si_normal);
	cmdList->SetComputeRootDescriptorTable(CalcAmbientOcclusion::RootSignatureLayout::ESI_Depth, si_depth);
	cmdList->SetComputeRootDescriptorTable(CalcAmbientOcclusion::RootSignatureLayout::EUO_AOCoefficient, uo_aoCoefficient);
	cmdList->SetComputeRootDescriptorTable(CalcAmbientOcclusion::RootSignatureLayout::EUO_RayHitDistance, uo_rayHitDistance);

	D3D12_DISPATCH_RAYS_DESC dispatchDesc = {};
	const auto& rayGen = mShaderTables["rtaoRayGen"];
	const auto& miss = mShaderTables["rtaoMiss"];
	const auto& hitGroup = mShaderTables["rtaoHitGroup"];
	dispatchDesc.RayGenerationShaderRecord.StartAddress = rayGen->GetGPUVirtualAddress();
	dispatchDesc.RayGenerationShaderRecord.SizeInBytes = rayGen->GetDesc().Width;
	dispatchDesc.MissShaderTable.StartAddress = miss->GetGPUVirtualAddress();
	dispatchDesc.MissShaderTable.SizeInBytes = miss->GetDesc().Width;
	dispatchDesc.MissShaderTable.StrideInBytes = dispatchDesc.MissShaderTable.SizeInBytes;
	dispatchDesc.HitGroupTable.StartAddress = hitGroup->GetGPUVirtualAddress();
	dispatchDesc.HitGroupTable.SizeInBytes = hitGroup->GetDesc().Width;
	dispatchDesc.HitGroupTable.StrideInBytes = dispatchDesc.HitGroupTable.SizeInBytes;
	dispatchDesc.Width = mWidth;
	dispatchDesc.Height = mHeight;
	dispatchDesc.Depth = 1;
	cmdList->DispatchRays(&dispatchDesc);
}

void RtaoClass::RunCalculatingDepthPartialDerivative(
		ID3D12GraphicsCommandList4*const cmdList, 
		D3D12_GPU_DESCRIPTOR_HANDLE i_depth,
		D3D12_GPU_DESCRIPTOR_HANDLE o_depthPartialDerivative,
		UINT width, UINT height) {
	cmdList->SetPipelineState(mPSOs["partialDerivative"].Get());
	cmdList->SetComputeRootSignature(mRootSignatures["partialDerivative"].Get());

	const float values[Rtao::CalcDepthPartialDerivative::RootConstantsLayout::Count] = { 1.0f / width, 1.0f / height };
	cmdList->SetComputeRoot32BitConstants(CalcDepthPartialDerivative::RootSignatureLayout::EC_Consts, CalcDepthPartialDerivative::RootConstantsLayout::Count, values, 0);

	cmdList->SetComputeRootDescriptorTable(CalcDepthPartialDerivative::RootSignatureLayout::ESI_Depth, i_depth);
	cmdList->SetComputeRootDescriptorTable(CalcDepthPartialDerivative::RootSignatureLayout::EUO_DepthPartialDerivative, o_depthPartialDerivative);

	cmdList->Dispatch(
		CeilDivide(width, DefaultComputeShaderParams::ThreadGroup::Width), 
		CeilDivide(height, DefaultComputeShaderParams::ThreadGroup::Height), 1);
}

void RtaoClass::RunCalculatingLocalMeanVariance(
		ID3D12GraphicsCommandList4*const cmdList, 
		D3D12_GPU_VIRTUAL_ADDRESS cbAddress,
		D3D12_GPU_DESCRIPTOR_HANDLE si_aoCoefficient,
		D3D12_GPU_DESCRIPTOR_HANDLE uo_localMeanVariance, 
		UINT width, UINT height,
		bool checkerboardSamplingEnabled) {
	cmdList->SetPipelineState(mPSOs["localMeanVariance"].Get());
	cmdList->SetComputeRootSignature(mRootSignatures["localMeanVariance"].Get());

	cmdList->SetComputeRootConstantBufferView(CalcLocalMeanVariance::RootSignatureLayout::ECB_LocalMeanVar, cbAddress);
	cmdList->SetComputeRootDescriptorTable(CalcLocalMeanVariance::RootSignatureLayout::ESI_AOCoefficient, si_aoCoefficient);
	cmdList->SetComputeRootDescriptorTable(CalcLocalMeanVariance::RootSignatureLayout::EUO_LocalMeanVar, uo_localMeanVariance);

	int pixelStepY = checkerboardSamplingEnabled ? 2 : 1;
	cmdList->Dispatch(
		CeilDivide(width, DefaultComputeShaderParams::ThreadGroup::Width), 
		CeilDivide(height, DefaultComputeShaderParams::ThreadGroup::Height * pixelStepY), 
		1);
}

void RtaoClass::FillInCheckerboard(
		ID3D12GraphicsCommandList4*const cmdList,
		D3D12_GPU_VIRTUAL_ADDRESS cbAddress,
		D3D12_GPU_DESCRIPTOR_HANDLE uio_localMeanVariance) {
	cmdList->SetPipelineState(mPSOs["fillInCheckerboard"].Get());
	cmdList->SetComputeRootSignature(mRootSignatures["fillInCheckerboard"].Get());

	cmdList->SetComputeRootConstantBufferView(FillInCheckerboard::RootSignatureLayout::ECB_LocalMeanVar, cbAddress);
	cmdList->SetComputeRootDescriptorTable(FillInCheckerboard::RootSignatureLayout::EUIO_LocalMeanVar, uio_localMeanVariance);

	cmdList->Dispatch(
		CeilDivide(mWidth, DefaultComputeShaderParams::ThreadGroup::Width), 
		CeilDivide(mHeight, DefaultComputeShaderParams::ThreadGroup::Height * 2), 
		1);
}

void RtaoClass::ReverseReprojectPreviousFrame(
		ID3D12GraphicsCommandList4*const cmdList,
		D3D12_GPU_VIRTUAL_ADDRESS cbAddress,
		D3D12_GPU_DESCRIPTOR_HANDLE si_normalDepth,
		D3D12_GPU_DESCRIPTOR_HANDLE si_depthPartialDerivative,
		D3D12_GPU_DESCRIPTOR_HANDLE si_reprojNormalDepth,
		D3D12_GPU_DESCRIPTOR_HANDLE si_cachedNormalDepth,
		D3D12_GPU_DESCRIPTOR_HANDLE si_velocity,
		D3D12_GPU_DESCRIPTOR_HANDLE si_cachedAOCoefficient,
		D3D12_GPU_DESCRIPTOR_HANDLE si_cachedTspp, 
		D3D12_GPU_DESCRIPTOR_HANDLE si_cachedAOCoefficientSquaredMean, 
		D3D12_GPU_DESCRIPTOR_HANDLE si_cachedRayHitDistance,
		D3D12_GPU_DESCRIPTOR_HANDLE uo_cachedTspp,
		D3D12_GPU_DESCRIPTOR_HANDLE uo_tsppCoefficientSquaredMeanRayHitDistance) {
	cmdList->SetPipelineState(mPSOs["tsppReproj"].Get());
	cmdList->SetComputeRootSignature(mRootSignatures["tsppReproj"].Get());	

	cmdList->SetComputeRootConstantBufferView(TemporalSupersamplingReverseReproject::RootSignatureLayout::ECB_CrossBilateralFilter, cbAddress);
	cmdList->SetComputeRootDescriptorTable(TemporalSupersamplingReverseReproject::RootSignatureLayout::ESI_NormalDepth, si_normalDepth);
	cmdList->SetComputeRootDescriptorTable(TemporalSupersamplingReverseReproject::RootSignatureLayout::ESI_DepthPartialDerivative, si_depthPartialDerivative);
	cmdList->SetComputeRootDescriptorTable(TemporalSupersamplingReverseReproject::RootSignatureLayout::ESI_ReprojectedNormalDepth, si_reprojNormalDepth);
	cmdList->SetComputeRootDescriptorTable(TemporalSupersamplingReverseReproject::RootSignatureLayout::ESI_CachedNormalDepth, si_cachedNormalDepth);
	cmdList->SetComputeRootDescriptorTable(TemporalSupersamplingReverseReproject::RootSignatureLayout::ESI_Velocity, si_velocity);
	cmdList->SetComputeRootDescriptorTable(TemporalSupersamplingReverseReproject::RootSignatureLayout::ESI_CachedAOCoefficient, si_cachedAOCoefficient);
	cmdList->SetComputeRootDescriptorTable(TemporalSupersamplingReverseReproject::RootSignatureLayout::ESI_CachedTspp, si_cachedTspp);
	cmdList->SetComputeRootDescriptorTable(TemporalSupersamplingReverseReproject::RootSignatureLayout::ESI_CachedAOCoefficientSquaredMean, si_cachedAOCoefficientSquaredMean);
	cmdList->SetComputeRootDescriptorTable(TemporalSupersamplingReverseReproject::RootSignatureLayout::ESI_CachedRayHitDistance, si_cachedRayHitDistance);
	cmdList->SetComputeRootDescriptorTable(TemporalSupersamplingReverseReproject::RootSignatureLayout::EUO_CachedTspp, uo_cachedTspp);
	cmdList->SetComputeRootDescriptorTable(TemporalSupersamplingReverseReproject::RootSignatureLayout::EUO_TsppCoefficientSquaredMeanRayHitDistacne, uo_tsppCoefficientSquaredMeanRayHitDistance);

	{
		UINT values[] = { mWidth, mHeight };
		cmdList->SetComputeRoot32BitConstants(
			TemporalSupersamplingReverseReproject::RootSignatureLayout::EC_Consts, 
			_countof(values), values, 
			TemporalSupersamplingReverseReproject::RootConstantsLayout::ETextureDim_X
		);
	}
	{
		float values[] = { 1.0f / mWidth, 1.0f / mHeight };
		cmdList->SetComputeRoot32BitConstants(
			TemporalSupersamplingReverseReproject::RootSignatureLayout::EC_Consts, 
			_countof(values), values, 
			TemporalSupersamplingReverseReproject::RootConstantsLayout::EInvTextureDim_X
		);
	}

	cmdList->Dispatch(
		CeilDivide(mWidth, DefaultComputeShaderParams::ThreadGroup::Width), 
		CeilDivide(mHeight, DefaultComputeShaderParams::ThreadGroup::Height), 1);
}

void RtaoClass::BlendWithCurrentFrame(
		ID3D12GraphicsCommandList4*const cmdList,
		D3D12_GPU_VIRTUAL_ADDRESS cbAddress,
		D3D12_GPU_DESCRIPTOR_HANDLE si_aoCoefficient,
		D3D12_GPU_DESCRIPTOR_HANDLE si_localMeanVariance, 
		D3D12_GPU_DESCRIPTOR_HANDLE si_rayHitDistance, 
		D3D12_GPU_DESCRIPTOR_HANDLE si_tsppCoefficientSquaredMeanRayHitDistance, 
		D3D12_GPU_DESCRIPTOR_HANDLE uio_temporalAOCoefficient, 
		D3D12_GPU_DESCRIPTOR_HANDLE uio_tspp, 
		D3D12_GPU_DESCRIPTOR_HANDLE uio_coefficientSquaredMean, 
		D3D12_GPU_DESCRIPTOR_HANDLE uio_rayHitDistance, 
		D3D12_GPU_DESCRIPTOR_HANDLE uo_variance, 
		D3D12_GPU_DESCRIPTOR_HANDLE uo_blurStrength) {
	cmdList->SetPipelineState(mPSOs["tsppBlend"].Get());
	cmdList->SetComputeRootSignature(mRootSignatures["tsppBlend"].Get());

	cmdList->SetComputeRootConstantBufferView(TemporalSupersamplingBlendWithCurrentFrame::RootSignatureLayout::ECB_TsspBlendWithCurrentFrame, cbAddress);
	cmdList->SetComputeRootDescriptorTable(TemporalSupersamplingBlendWithCurrentFrame::RootSignatureLayout::ESI_AOCoefficient, si_aoCoefficient);
	cmdList->SetComputeRootDescriptorTable(TemporalSupersamplingBlendWithCurrentFrame::RootSignatureLayout::ESI_LocalMeanVaraince, si_localMeanVariance);
	cmdList->SetComputeRootDescriptorTable(TemporalSupersamplingBlendWithCurrentFrame::RootSignatureLayout::ESI_RayHitDistance, si_rayHitDistance);
	cmdList->SetComputeRootDescriptorTable(TemporalSupersamplingBlendWithCurrentFrame::RootSignatureLayout::ESI_TsppCoefficientSquaredMeanRayHitDistance, si_tsppCoefficientSquaredMeanRayHitDistance);
	cmdList->SetComputeRootDescriptorTable(TemporalSupersamplingBlendWithCurrentFrame::RootSignatureLayout::EUIO_TemporalAOCoefficient, uio_temporalAOCoefficient);
	cmdList->SetComputeRootDescriptorTable(TemporalSupersamplingBlendWithCurrentFrame::RootSignatureLayout::EUIO_Tspp, uio_tspp);
	cmdList->SetComputeRootDescriptorTable(TemporalSupersamplingBlendWithCurrentFrame::RootSignatureLayout::EUIO_CoefficientSquaredMean, uio_coefficientSquaredMean);
	cmdList->SetComputeRootDescriptorTable(TemporalSupersamplingBlendWithCurrentFrame::RootSignatureLayout::EUIO_RayHitDistance, uio_rayHitDistance);
	cmdList->SetComputeRootDescriptorTable(TemporalSupersamplingBlendWithCurrentFrame::RootSignatureLayout::EUO_VarianceMap, uo_variance);
	cmdList->SetComputeRootDescriptorTable(TemporalSupersamplingBlendWithCurrentFrame::RootSignatureLayout::EUO_BlurStrength, uo_blurStrength);

	cmdList->Dispatch(
		CeilDivide(mWidth, DefaultComputeShaderParams::ThreadGroup::Width), 
		CeilDivide(mHeight, DefaultComputeShaderParams::ThreadGroup::Height), 1);
}

void RtaoClass::ApplyAtrousWaveletTransformFilter(
		ID3D12GraphicsCommandList4*const cmdList,
		D3D12_GPU_VIRTUAL_ADDRESS cbAddress,
		D3D12_GPU_DESCRIPTOR_HANDLE si_temporalAOCoefficient,
		D3D12_GPU_DESCRIPTOR_HANDLE si_normalDepth,
		D3D12_GPU_DESCRIPTOR_HANDLE si_variance,
		D3D12_GPU_DESCRIPTOR_HANDLE si_hitDistance,
		D3D12_GPU_DESCRIPTOR_HANDLE si_depthPartialDerivative,
		D3D12_GPU_DESCRIPTOR_HANDLE si_tspp, 
		D3D12_GPU_DESCRIPTOR_HANDLE uo_temporalAOCoefficient) {
	cmdList->SetPipelineState(mPSOs["atrousWaveletTransformFilter"].Get());
	cmdList->SetComputeRootSignature(mRootSignatures["atrousWaveletTransformFilter"].Get());

	cmdList->SetComputeRootConstantBufferView(AtrousWaveletTransformFilter::RootSignatureLayout::ECB_AtrousFilter, cbAddress);
	cmdList->SetComputeRootDescriptorTable(AtrousWaveletTransformFilter::RootSignatureLayout::ESI_TemporalAOCoefficient, si_temporalAOCoefficient);
	cmdList->SetComputeRootDescriptorTable(AtrousWaveletTransformFilter::RootSignatureLayout::ESI_NormalDepth, si_normalDepth);
	cmdList->SetComputeRootDescriptorTable(AtrousWaveletTransformFilter::RootSignatureLayout::ESI_Variance, si_variance);
	cmdList->SetComputeRootDescriptorTable(AtrousWaveletTransformFilter::RootSignatureLayout::ESI_HitDistance, si_hitDistance);
	cmdList->SetComputeRootDescriptorTable(AtrousWaveletTransformFilter::RootSignatureLayout::ESI_DepthPartialDerivative, si_depthPartialDerivative);
	cmdList->SetComputeRootDescriptorTable(AtrousWaveletTransformFilter::RootSignatureLayout::ESI_Tspp, si_tspp);
	cmdList->SetComputeRootDescriptorTable(AtrousWaveletTransformFilter::RootSignatureLayout::EUO_TemporalAOCoefficient, uo_temporalAOCoefficient);

	cmdList->Dispatch(
		CeilDivide(mWidth, AtrousWaveletTransformFilterShaderParams::ThreadGroup::Width), 
		CeilDivide(mHeight, AtrousWaveletTransformFilterShaderParams::ThreadGroup::Height), 
		1);
}

void RtaoClass::BlurDisocclusion(
		ID3D12GraphicsCommandList4*const cmdList,
		ID3D12Resource* aoCoefficient,
		D3D12_GPU_DESCRIPTOR_HANDLE si_depth,
		D3D12_GPU_DESCRIPTOR_HANDLE si_blurStrength,
		D3D12_GPU_DESCRIPTOR_HANDLE uio_aoCoefficient,
		UINT width, UINT height,
		UINT lowTsppBlurPasses) {
	cmdList->SetPipelineState(mPSOs["disocclusionBlur"].Get());
	cmdList->SetComputeRootSignature(mRootSignatures["disocclusionBlur"].Get());

	UINT values[2] = { width, height };
	cmdList->SetComputeRoot32BitConstants(DisocclusionBlur::RootSignatureLayout::EC_Consts, _countof(values), values, 0);

	cmdList->SetComputeRootDescriptorTable(DisocclusionBlur::RootSignatureLayout::ESI_Depth, si_depth);
	cmdList->SetComputeRootDescriptorTable(DisocclusionBlur::RootSignatureLayout::ESI_BlurStrength, si_blurStrength);
	cmdList->SetComputeRootDescriptorTable(DisocclusionBlur::RootSignatureLayout::EUIO_AOCoefficient, uio_aoCoefficient);

	UINT filterStep = 1;
	for (UINT i = 0; i < lowTsppBlurPasses; ++i) {
		cmdList->SetComputeRoot32BitConstant(DisocclusionBlur::RootSignatureLayout::EC_Consts, filterStep, DisocclusionBlur::RootConstantsLayout::EStep);

		// Account for interleaved Group execution
		UINT widthCS = filterStep * DefaultComputeShaderParams::ThreadGroup::Width * CeilDivide(width, filterStep * DefaultComputeShaderParams::ThreadGroup::Width);
		UINT heightCS = filterStep * DefaultComputeShaderParams::ThreadGroup::Height * CeilDivide(height, filterStep * DefaultComputeShaderParams::ThreadGroup::Height);

		// Dispatch.
		XMUINT2 groupSize(CeilDivide(widthCS, DefaultComputeShaderParams::ThreadGroup::Width), CeilDivide(heightCS, DefaultComputeShaderParams::ThreadGroup::Height));
		cmdList->Dispatch(groupSize.x, groupSize.y, 1);
		D3D12Util::UavBarrier(cmdList, aoCoefficient);

		filterStep *= 2;
	}
}

UINT RtaoClass::MoveToNextFrame() {
	mTemporalCurrentFrameResourceIndex = (mTemporalCurrentFrameResourceIndex + 1) % 2;
	return mTemporalCurrentFrameResourceIndex;
}

UINT RtaoClass::MoveToNextFrameTemporalAOCoefficient() {
	mTemporalCurrentFrameTemporalAOCeofficientResourceIndex = (mTemporalCurrentFrameTemporalAOCeofficientResourceIndex + 1) % 2;
	return mTemporalCurrentFrameTemporalAOCeofficientResourceIndex;
}

void RtaoClass::BuildDescriptors(CD3DX12_CPU_DESCRIPTOR_HANDLE& hCpu, CD3DX12_GPU_DESCRIPTOR_HANDLE& hGpu, UINT descSize) {
	mhAOResourcesCpus[AOResources::Descriptors::ES_AmbientCoefficient] = hCpu;
	mhAOResourcesGpus[AOResources::Descriptors::ES_AmbientCoefficient] = hGpu;
	mhAOResourcesCpus[AOResources::Descriptors::EU_AmbientCoefficient] = hCpu.Offset(1, descSize);
	mhAOResourcesGpus[AOResources::Descriptors::EU_AmbientCoefficient] = hGpu.Offset(1, descSize);
	mhAOResourcesCpus[AOResources::Descriptors::ES_RayHitDistance] = hCpu.Offset(1, descSize);
	mhAOResourcesGpus[AOResources::Descriptors::ES_RayHitDistance] = hGpu.Offset(1, descSize);
	mhAOResourcesCpus[AOResources::Descriptors::EU_RayHitDistance] = hCpu.Offset(1, descSize);
	mhAOResourcesGpus[AOResources::Descriptors::EU_RayHitDistance] = hGpu.Offset(1, descSize);
	
	mhLocalMeanVarianceResourcesCpus[LocalMeanVarianceResources::Descriptors::ES_Raw] = hCpu.Offset(1, descSize);
	mhLocalMeanVarianceResourcesGpus[LocalMeanVarianceResources::Descriptors::ES_Raw] = hGpu.Offset(1, descSize);
	mhLocalMeanVarianceResourcesCpus[LocalMeanVarianceResources::Descriptors::EU_Raw] = hCpu.Offset(1, descSize);
	mhLocalMeanVarianceResourcesGpus[LocalMeanVarianceResources::Descriptors::EU_Raw] = hGpu.Offset(1, descSize);
	mhLocalMeanVarianceResourcesCpus[LocalMeanVarianceResources::Descriptors::ES_Smoothed] = hCpu.Offset(1, descSize);
	mhLocalMeanVarianceResourcesGpus[LocalMeanVarianceResources::Descriptors::ES_Smoothed] = hGpu.Offset(1, descSize);
	mhLocalMeanVarianceResourcesCpus[LocalMeanVarianceResources::Descriptors::EU_Smoothed] = hCpu.Offset(1, descSize);
	mhLocalMeanVarianceResourcesGpus[LocalMeanVarianceResources::Descriptors::EU_Smoothed] = hGpu.Offset(1, descSize);

	mhAOVarianceResourcesCpus[AOVarianceResources::Descriptors::ES_Raw] = hCpu.Offset(1, descSize);
	mhAOVarianceResourcesGpus[AOVarianceResources::Descriptors::ES_Raw] = hGpu.Offset(1, descSize);
	mhAOVarianceResourcesCpus[AOVarianceResources::Descriptors::EU_Raw] = hCpu.Offset(1, descSize);
	mhAOVarianceResourcesGpus[AOVarianceResources::Descriptors::EU_Raw] = hGpu.Offset(1, descSize);
	mhAOVarianceResourcesCpus[AOVarianceResources::Descriptors::ES_Smoothed] = hCpu.Offset(1, descSize);
	mhAOVarianceResourcesGpus[AOVarianceResources::Descriptors::ES_Smoothed] = hGpu.Offset(1, descSize);
	mhAOVarianceResourcesCpus[AOVarianceResources::Descriptors::EU_Smoothed] = hCpu.Offset(1, descSize);
	mhAOVarianceResourcesGpus[AOVarianceResources::Descriptors::EU_Smoothed] = hGpu.Offset(1, descSize);

	for (size_t i = 0; i < 2; ++i) {
		mhTemporalCachesCpus[i][TemporalCaches::Descriptors::ES_Tspp] = hCpu.Offset(1, descSize);
		mhTemporalCachesGpus[i][TemporalCaches::Descriptors::ES_Tspp] = hGpu.Offset(1, descSize);
		mhTemporalCachesCpus[i][TemporalCaches::Descriptors::EU_Tspp] = hCpu.Offset(1, descSize);
		mhTemporalCachesGpus[i][TemporalCaches::Descriptors::EU_Tspp] = hGpu.Offset(1, descSize);

		mhTemporalCachesCpus[i][TemporalCaches::Descriptors::ES_RayHitDistance] = hCpu.Offset(1, descSize);
		mhTemporalCachesGpus[i][TemporalCaches::Descriptors::ES_RayHitDistance] = hGpu.Offset(1, descSize);
		mhTemporalCachesCpus[i][TemporalCaches::Descriptors::EU_RayHitDistance] = hCpu.Offset(1, descSize);
		mhTemporalCachesGpus[i][TemporalCaches::Descriptors::EU_RayHitDistance] = hGpu.Offset(1, descSize);

		mhTemporalCachesCpus[i][TemporalCaches::Descriptors::ES_CoefficientSquaredMean] = hCpu.Offset(1, descSize);
		mhTemporalCachesGpus[i][TemporalCaches::Descriptors::ES_CoefficientSquaredMean] = hGpu.Offset(1, descSize);
		mhTemporalCachesCpus[i][TemporalCaches::Descriptors::EU_CoefficientSquaredMean] = hCpu.Offset(1, descSize);
		mhTemporalCachesGpus[i][TemporalCaches::Descriptors::EU_CoefficientSquaredMean] = hGpu.Offset(1, descSize);
	}

	mhPrevFrameNormalDepthCpuSrv = hCpu.Offset(1, descSize);
	mhPrevFrameNormalDepthGpuSrv = hGpu.Offset(1, descSize);

	mhTsppCoefficientSquaredMeanRayHitDistanceCpuSrv = hCpu.Offset(1, descSize);
	mhTsppCoefficientSquaredMeanRayHitDistanceGpuSrv = hGpu.Offset(1, descSize);
	mhTsppCoefficientSquaredMeanRayHitDistanceCpuUav = hCpu.Offset(1, descSize);
	mhTsppCoefficientSquaredMeanRayHitDistanceGpuUav = hGpu.Offset(1, descSize);

	mhDisocclusionBlurStrengthCpuSrv = hCpu.Offset(1, descSize);
	mhDisocclusionBlurStrengthGpuSrv = hGpu.Offset(1, descSize);
	mhDisocclusionBlurStrengthCpuUav = hCpu.Offset(1, descSize);
	mhDisocclusionBlurStrengthGpuUav = hGpu.Offset(1, descSize);

	for (size_t i = 0; i < 2; ++i) {
		mhTemporalAOCoefficientsCpus[i][TemporalAOCoefficients::Descriptors::Srv] = hCpu.Offset(1, descSize);
		mhTemporalAOCoefficientsGpus[i][TemporalAOCoefficients::Descriptors::Srv] = hGpu.Offset(1, descSize);
		mhTemporalAOCoefficientsCpus[i][TemporalAOCoefficients::Descriptors::Uav] = hCpu.Offset(1, descSize);
		mhTemporalAOCoefficientsGpus[i][TemporalAOCoefficients::Descriptors::Uav] = hGpu.Offset(1, descSize);
	}

	mhDepthPartialDerivativeCpuSrv = hCpu.Offset(1, descSize);
	mhDepthPartialDerivativeGpuSrv = hGpu.Offset(1, descSize);
	mhDepthPartialDerivativeCpuUav = hCpu.Offset(1, descSize);
	mhDepthPartialDerivativeGpuUav = hGpu.Offset(1, descSize);

	BuildDescriptors();

	hCpu.Offset(1, descSize);
	hGpu.Offset(1, descSize);
}

bool RtaoClass::OnResize(ID3D12GraphicsCommandList* cmdList, UINT width, UINT height) {
	if ((mWidth != width) || (mHeight != height)) {
		mWidth = width;
		mHeight = height;

		CheckIsValid(BuildResource(cmdList));
		BuildDescriptors();
	}

	return true;
}

void RtaoClass::Transite(ID3D12GraphicsCommandList* cmdList, bool srvToUav) {
	
}

void RtaoClass::Switch() {
	bSwitch = !bSwitch;
}

void RtaoClass::BuildDescriptors() {
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
	srvDesc.Texture2D.MipLevels = 1;

	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;

	{
		srvDesc.Format = AOCoefficientMapFormat;
		uavDesc.Format = AOCoefficientMapFormat;

		auto pResource = mAOResources[AOResources::EAmbientCoefficient].Get();
		md3dDevice->CreateShaderResourceView(pResource, &srvDesc, mhAOResourcesCpus[AOResources::Descriptors::ES_AmbientCoefficient]);
		md3dDevice->CreateUnorderedAccessView(pResource, nullptr, &uavDesc, mhAOResourcesCpus[AOResources::Descriptors::EU_AmbientCoefficient]);
	}
	{
		srvDesc.Format = RayHitDistanceFormat;
		uavDesc.Format = RayHitDistanceFormat;

		auto pResource = mAOResources[AOResources::ERayHitDistance].Get();
		md3dDevice->CreateShaderResourceView(pResource, &srvDesc, mhAOResourcesCpus[AOResources::Descriptors::ES_RayHitDistance]);
		md3dDevice->CreateUnorderedAccessView(pResource, nullptr, &uavDesc, mhAOResourcesCpus[AOResources::Descriptors::EU_RayHitDistance]);
	}

	{
		srvDesc.Format = TsppMapFormat;
		uavDesc.Format = TsppMapFormat;
		for (size_t i = 0; i < 2; ++i) {
			auto pResource = mTemporalCaches[i][TemporalCaches::ETspp].Get();
			auto& cpus = mhTemporalCachesCpus[i];
			md3dDevice->CreateShaderResourceView(pResource, &srvDesc, cpus[TemporalCaches::Descriptors::ES_Tspp]);
			md3dDevice->CreateUnorderedAccessView(pResource, nullptr, &uavDesc, cpus[TemporalCaches::Descriptors::EU_Tspp]);
		}
	}
	{
		srvDesc.Format = RayHitDistanceFormat;
		uavDesc.Format = RayHitDistanceFormat;
		for (size_t i = 0; i < 2; ++i) {
			auto pResource = mTemporalCaches[i][TemporalCaches::ERayHitDistance].Get();
			auto& cpus = mhTemporalCachesCpus[i];
			md3dDevice->CreateShaderResourceView(pResource, &srvDesc, cpus[TemporalCaches::Descriptors::ES_RayHitDistance]);
			md3dDevice->CreateUnorderedAccessView(pResource, nullptr, &uavDesc, cpus[TemporalCaches::Descriptors::EU_RayHitDistance]);
		}
	}
	{
		srvDesc.Format = CoefficientSquaredMeanMapFormat;
		uavDesc.Format = CoefficientSquaredMeanMapFormat;
		for (size_t i = 0; i < 2; ++i) {
			auto pResource = mTemporalCaches[i][TemporalCaches::ECoefficientSquaredMean].Get();
			auto& cpus = mhTemporalCachesCpus[i];
			md3dDevice->CreateShaderResourceView(pResource, &srvDesc, cpus[TemporalCaches::Descriptors::ES_CoefficientSquaredMean]);
			md3dDevice->CreateUnorderedAccessView(pResource, nullptr, &uavDesc, cpus[TemporalCaches::Descriptors::EU_CoefficientSquaredMean]);
		}
	}
	{
		srvDesc.Format = LocalMeanVarianceMapFormat;
		uavDesc.Format = LocalMeanVarianceMapFormat;
		auto pRawResource = mLocalMeanVarianceResources[LocalMeanVarianceResources::ERaw].Get();
		md3dDevice->CreateShaderResourceView(pRawResource, &srvDesc, mhLocalMeanVarianceResourcesCpus[LocalMeanVarianceResources::Descriptors::ES_Raw]);
		md3dDevice->CreateUnorderedAccessView(pRawResource, nullptr, &uavDesc, mhLocalMeanVarianceResourcesCpus[LocalMeanVarianceResources::Descriptors::EU_Raw]);

		auto pSmoothedResource = mLocalMeanVarianceResources[LocalMeanVarianceResources::ESmoothed].Get();
		md3dDevice->CreateShaderResourceView(pSmoothedResource, &srvDesc, mhLocalMeanVarianceResourcesCpus[LocalMeanVarianceResources::Descriptors::ES_Smoothed]);
		md3dDevice->CreateUnorderedAccessView(pSmoothedResource, nullptr, &uavDesc, mhLocalMeanVarianceResourcesCpus[LocalMeanVarianceResources::Descriptors::EU_Smoothed]);
	}
	{
		srvDesc.Format = VarianceMapFormat;
		uavDesc.Format = VarianceMapFormat;

		auto pRawResource = mAOVarianceResources[AOVarianceResources::ERaw].Get();
		md3dDevice->CreateShaderResourceView(pRawResource, &srvDesc, mhAOVarianceResourcesCpus[AOVarianceResources::Descriptors::ES_Raw]);
		md3dDevice->CreateUnorderedAccessView(pRawResource, nullptr, &uavDesc, mhAOVarianceResourcesCpus[AOVarianceResources::Descriptors::EU_Raw]);

		auto pSmoothedResource = mAOVarianceResources[AOVarianceResources::ESmoothed].Get();
		md3dDevice->CreateShaderResourceView(pSmoothedResource, &srvDesc, mhAOVarianceResourcesCpus[AOVarianceResources::Descriptors::ES_Smoothed]);
		md3dDevice->CreateUnorderedAccessView(pSmoothedResource, nullptr, &uavDesc, mhAOVarianceResourcesCpus[AOVarianceResources::Descriptors::EU_Smoothed]);
	}
	{
		srvDesc.Format = NormalDepthMapFormat;
		md3dDevice->CreateShaderResourceView(mPrevFrameNormalDepth.Get(), &srvDesc, mhPrevFrameNormalDepthCpuSrv);

		srvDesc.Format = TsppCoefficientSquaredMeanRayHitDistanceFormat;
		uavDesc.Format = TsppCoefficientSquaredMeanRayHitDistanceFormat;
		md3dDevice->CreateShaderResourceView(mTsppCoefficientSquaredMeanRayHitDistance.Get(), &srvDesc, mhTsppCoefficientSquaredMeanRayHitDistanceCpuSrv);
		md3dDevice->CreateUnorderedAccessView(mTsppCoefficientSquaredMeanRayHitDistance.Get(), nullptr, &uavDesc, mhTsppCoefficientSquaredMeanRayHitDistanceCpuUav);
	}
	{
		srvDesc.Format = DisocclusionBlurStrengthMapFormat;
		md3dDevice->CreateShaderResourceView(mDisocclusionBlurStrength.Get(), &srvDesc, mhDisocclusionBlurStrengthCpuSrv);

		uavDesc.Format = DisocclusionBlurStrengthMapFormat;
		md3dDevice->CreateUnorderedAccessView(mDisocclusionBlurStrength.Get(), nullptr, &uavDesc, mhDisocclusionBlurStrengthCpuUav);
	}
	{
		srvDesc.Format = AOCoefficientMapFormat;
		uavDesc.Format = AOCoefficientMapFormat;
		for (size_t i = 0; i < 2; ++i) {
			md3dDevice->CreateShaderResourceView(
				mTemporalAOCoefficients[i].Get(), &srvDesc, 
				mhTemporalAOCoefficientsCpus[i][TemporalAOCoefficients::Descriptors::Srv]
			);
			md3dDevice->CreateUnorderedAccessView(
				mTemporalAOCoefficients[i].Get(), nullptr, &uavDesc, 
				mhTemporalAOCoefficientsCpus[i][TemporalAOCoefficients::Descriptors::Uav]
			);
		}
	}
	{
		srvDesc.Format = DepthPartialDerivativeMapFormat;
		md3dDevice->CreateShaderResourceView(mDepthPartialDerivative.Get(), &srvDesc, mhDepthPartialDerivativeCpuSrv);

		uavDesc.Format = DepthPartialDerivativeMapFormat;
		md3dDevice->CreateUnorderedAccessView(mDepthPartialDerivative.Get(), nullptr, &uavDesc, mhDepthPartialDerivativeCpuUav);
	}
}

bool RtaoClass::BuildResource(ID3D12GraphicsCommandList* cmdList) {
	D3D12_RESOURCE_DESC texDesc;
	ZeroMemory(&texDesc, sizeof(D3D12_RESOURCE_DESC));
	texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	texDesc.Alignment = 0;
	// Ambient occlusion maps are at half resolution.
	texDesc.Width = mWidth;
	texDesc.Height = mHeight;
	texDesc.DepthOrArraySize = 1;
	texDesc.MipLevels = 1;
	texDesc.SampleDesc.Count = 1;
	texDesc.SampleDesc.Quality = 0;
	texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	
	{
		texDesc.Format = AOCoefficientMapFormat;
		CheckHResult(md3dDevice->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&texDesc,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
			nullptr,
			IID_PPV_ARGS(&mAOResources[AOResources::EAmbientCoefficient])
		));
		mAOResources[AOResources::EAmbientCoefficient].Get()->SetName(L"AOCoefficient");
	}
	{
		texDesc.Format = RayHitDistanceFormat;
		CheckHResult(md3dDevice->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&texDesc,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
			nullptr,
			IID_PPV_ARGS(&mAOResources[AOResources::ERayHitDistance])
		));
		mAOResources[AOResources::ERayHitDistance].Get()->SetName(L"RayHitDistance");
	}
	{
		texDesc.Format = TsppMapFormat;
		for (int i = 0; i < 2; ++i) {
			CheckHResult(md3dDevice->CreateCommittedResource(
				&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
				D3D12_HEAP_FLAG_NONE,
				&texDesc,
				D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
				nullptr,
				IID_PPV_ARGS(&mTemporalCaches[i][TemporalCaches::ETspp])
			));
			std::wstring name = L"Tspp_";
			name.append(std::to_wstring(i));
			mTemporalCaches[i][TemporalCaches::ETspp]->SetName(name.c_str());
		}
	}
	{
		texDesc.Format = RayHitDistanceFormat;
		for (int i = 0; i < 2; ++i) {
			CheckHResult(md3dDevice->CreateCommittedResource(
				&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
				D3D12_HEAP_FLAG_NONE,
				&texDesc,
				D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
				nullptr,
				IID_PPV_ARGS(&mTemporalCaches[i][TemporalCaches::ERayHitDistance])
			));
			std::wstring name = L"TemporalRayHitDistance_";
			name.append(std::to_wstring(i));
			mTemporalCaches[i][TemporalCaches::ERayHitDistance]->SetName(name.c_str());
		}
	}
	{
		texDesc.Format = CoefficientSquaredMeanMapFormat;
		for (int i = 0; i < 2; ++i) {
			CheckHResult(md3dDevice->CreateCommittedResource(
				&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
				D3D12_HEAP_FLAG_NONE,
				&texDesc,
				D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
				nullptr,
				IID_PPV_ARGS(&mTemporalCaches[i][TemporalCaches::ECoefficientSquaredMean])
			));
			std::wstring name = L"AOCoefficientSquaredMean_";
			name.append(std::to_wstring(i));
			mTemporalCaches[i][TemporalCaches::ECoefficientSquaredMean]->SetName(name.c_str());
		}
	}
	{
		texDesc.Format = LocalMeanVarianceMapFormat;

		CheckHResult(md3dDevice->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&texDesc,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
			nullptr,
			IID_PPV_ARGS(&mLocalMeanVarianceResources[LocalMeanVarianceResources::ERaw])
		));
		mLocalMeanVarianceResources[LocalMeanVarianceResources::ERaw]->SetName(L"RawLocalMeanVariance");

		CheckHResult(md3dDevice->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&texDesc,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
			nullptr,
			IID_PPV_ARGS(&mLocalMeanVarianceResources[LocalMeanVarianceResources::ESmoothed])
		));
		mLocalMeanVarianceResources[LocalMeanVarianceResources::ESmoothed]->SetName(L"SmoothedLocalMeanVariance");
	}
	{
		texDesc.Format = VarianceMapFormat;

		CheckHResult(md3dDevice->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&texDesc,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
			nullptr,
			IID_PPV_ARGS(&mAOVarianceResources[AOVarianceResources::ERaw])
		));
		mAOVarianceResources[AOVarianceResources::ERaw]->SetName(L"RawVariance");

		CheckHResult(md3dDevice->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&texDesc,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
			nullptr,
			IID_PPV_ARGS(&mAOVarianceResources[AOVarianceResources::ESmoothed])
		));
		mAOVarianceResources[AOVarianceResources::ESmoothed]->SetName(L"SmoothedVariance");
	}
	{
		auto _texDesc = texDesc;
		_texDesc.Format = NormalDepthMapFormat;
		_texDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
		CheckHResult(md3dDevice->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&_texDesc,
			D3D12_RESOURCE_STATE_COPY_DEST,
			nullptr,
			IID_PPV_ARGS(&mPrevFrameNormalDepth)
		));
		mPrevFrameNormalDepth->SetName(L"PrevFrameNormalDepth");
	}
	{
		texDesc.Format = TsppCoefficientSquaredMeanRayHitDistanceFormat;
		CheckHResult(md3dDevice->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&texDesc,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
			nullptr,
			IID_PPV_ARGS(&mTsppCoefficientSquaredMeanRayHitDistance)
		));
		mTsppCoefficientSquaredMeanRayHitDistance->SetName(L"TsppAOCoefficientSquaredMeanRayHitDistance");
	}
	{
		texDesc.Format = DisocclusionBlurStrengthMapFormat;
		CheckHResult(md3dDevice->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&texDesc,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
			nullptr,
			IID_PPV_ARGS(&mDisocclusionBlurStrength)
		));
		mDisocclusionBlurStrength->SetName(L"DisocclusionBlurStrength");
	}
	{
		texDesc.Format = AOCoefficientMapFormat;
		for (int i = 0; i < 2; ++i) {
			CheckHResult(md3dDevice->CreateCommittedResource(
				&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
				D3D12_HEAP_FLAG_NONE,
				&texDesc,
				D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
				nullptr,
				IID_PPV_ARGS(&mTemporalAOCoefficients[i])
			));
			mTemporalAOCoefficients[i]->SetName(L"TemporalAOCoefficient_" + i);
		}
	}
	{
		texDesc.Format = DepthPartialDerivativeMapFormat;
		CheckHResult(md3dDevice->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&texDesc,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
			nullptr,
			IID_PPV_ARGS(&mDepthPartialDerivative)
		));
		mDepthPartialDerivative->SetName(L"DepthPartialDerivative");
	}

	{
		const UINT num2DSubresources = texDesc.DepthOrArraySize * texDesc.MipLevels;
		const UINT64 uploadBufferSize = GetRequiredIntermediateSize(mPrevFrameNormalDepth.Get(), 0, num2DSubresources);

		CheckHResult(md3dDevice->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize),
			D3D12_RESOURCE_STATE_COPY_SOURCE,
			nullptr,
			IID_PPV_ARGS(mPrevFrameNormalDepthUploadBuffer.GetAddressOf())
		));

		const UINT size = mWidth * mHeight * 4;
		std::vector<BYTE> data(size);

		for (UINT i = 0; i < size; i += 4) {
			data[i] = data[i + 1] = data[i + 2] = 0;	// rgb-channels(normal) = 0 / 128;
			data[i + 3] = 127;							// a-channel(depth) = 127 / 128;
		}

		D3D12_SUBRESOURCE_DATA subResourceData = {};
		subResourceData.pData = data.data();
		subResourceData.RowPitch = mWidth * 4;
		subResourceData.SlicePitch = subResourceData.RowPitch * mHeight;

		UpdateSubresources(
			cmdList,
			mPrevFrameNormalDepth.Get(),
			mPrevFrameNormalDepthUploadBuffer.Get(),
			0,
			0,
			num2DSubresources,
			&subResourceData
		);
		cmdList->ResourceBarrier(
			1,
			&CD3DX12_RESOURCE_BARRIER::Transition(
				mPrevFrameNormalDepth.Get(),
				D3D12_RESOURCE_STATE_COPY_DEST,
				D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
			)
		);
	}
	
	return true;
}