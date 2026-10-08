// Host stand-in: no caches to keep in step
#pragma once
#include <cstddef>
#include <cstdint>
inline void dsy_dma_clear_cache_for_buffer(uint8_t*, size_t) {}
inline void dsy_dma_invalidate_cache_for_buffer(uint8_t*, size_t) {}
