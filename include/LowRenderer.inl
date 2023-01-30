#ifndef __LOWRENDERER_INL__
#define __LOWRENDERER_INL__

constexpr float LowRenderer::GetAspectRatio() const {
	return static_cast<float>(mClientWidth) / static_cast<float>(mClientHeight);
}

constexpr UINT LowRenderer::GetClientWidth() const {
	return mClientWidth;
}

constexpr UINT LowRenderer::GetClientHeight() const {
	return mClientHeight;
}

constexpr UINT LowRenderer::GetRtvDescriptorSize() const {
	return mRtvDescriptorSize;
}

constexpr UINT LowRenderer::GetDsvDescriptorSize() const {
	return mDsvDescriptorSize;
}

constexpr UINT LowRenderer::GetCbvSrvUavDescriptorSize() const {
	return mCbvSrvUavDescriptorSize;
}

__forceinline constexpr UINT64 LowRenderer::GetCurrentFence() const {
	return mCurrentFence;
}

#endif // __LOWRENDERER_INL__