#pragma once

// Obj-C++ header. Only include from .mm files.
#import <Metal/Metal.h>

namespace loopa::app {

// Shared Metal device + command queue + waveform render pipeline.
// All WaveformView instances use the same context.
class MetalContext {
public:
    static constexpr NSUInteger kSampleCount = 4;   // 4× MSAA on both pipelines

    static MetalContext& instance();

    bool isValid() const noexcept { return m_device != nil; }

    id<MTLDevice> device() const noexcept                      { return m_device; }
    id<MTLCommandQueue> commandQueue() const noexcept          { return m_commandQueue; }

    // The original vertex pipeline (position + colour quads). Used for grid
    // lines, playhead, and the recording-mode live trace. Takes a vertex buffer
    // of interleaved {float2 pos; float4 colour}.
    id<MTLRenderPipelineState> linePipeline() const noexcept   { return m_linePipeline; }

    // Peaks-driven waveform pipeline. Takes a Peak buffer (float2 min/max) at
    // [[buffer(0)]] and a WaveformUniforms buffer at [[buffer(1)]]. Drawn as
    // MTLPrimitiveTypeTriangleStrip to produce a filled ribbon.
    id<MTLRenderPipelineState> peaksPipeline() const noexcept  { return m_peaksPipeline; }

    MetalContext(const MetalContext&) = delete;
    MetalContext& operator=(const MetalContext&) = delete;

private:
    MetalContext();
    ~MetalContext();

    id<MTLDevice>              m_device         = nil;
    id<MTLCommandQueue>        m_commandQueue   = nil;
    id<MTLLibrary>             m_library        = nil;
    id<MTLRenderPipelineState> m_linePipeline   = nil;
    id<MTLRenderPipelineState> m_peaksPipeline  = nil;
};

}  // namespace loopa::app
