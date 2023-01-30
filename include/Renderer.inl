#ifndef __RENDERER_INL__
#define __RENDERER_INL__

__forceinline constexpr bool Renderer::GetRenderType() const {
	return bRaytracing;
}

__forceinline constexpr bool Renderer::Initialized() const {
	return bInitialized;
}

#endif // __RENDERER_INL__