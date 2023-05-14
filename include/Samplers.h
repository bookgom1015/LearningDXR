#pragma once

#include <d3dx12.h>
#include <array>

using StaticSamplers = std::array<const CD3DX12_STATIC_SAMPLER_DESC, 11>;

namespace Samplers {
	StaticSamplers  GetStaticSamplers();
}