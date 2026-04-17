#import "MetalContext.h"

#include "core/Logger.h"

#include <string>

namespace loopa::app {

namespace {

// Metal Shading Language. Two vertex/fragment pairs in one library.
//
//   line_vertex / line_fragment — raw position + colour vertex array
//   peak_vertex / peak_fragment — samples a peaks buffer and emits a ribbon of
//                                 triangles between each peak's min and max
constexpr const char* kShaderSource = R"metal(
#include <metal_stdlib>
using namespace metal;

// ------------ Shared fragment output -----------------------------------------

struct VtxOut {
    float4 position [[position]];
    float4 color;
};

// ------------ Line pipeline (grid / playhead / recording trace) --------------

struct LineVertex {
    float2 position;
    float4 color;
};

vertex VtxOut line_vertex(uint vid [[vertex_id]],
                          constant LineVertex* vs [[buffer(0)]]) {
    VtxOut o;
    o.position = float4(vs[vid].position, 0.0, 1.0);
    o.color    = vs[vid].color;
    return o;
}

fragment float4 line_fragment(VtxOut in [[stage_in]]) {
    return in.color;
}

// ------------ Peaks pipeline (playback ribbon) -------------------------------

struct Peak { float mn; float mx; };

struct WaveformUniforms {
    float  samplesPerPeak;
    float  samplePosition;
    float  viewportSamples;
    float  resolutionX;
    float  yScale;
    float  peakCount;
    float  firstVisiblePeakSample;
    float  _pad0;
    float4 colorPlayed;
    float4 colorUpcoming;
};

vertex VtxOut peak_vertex(uint vid [[vertex_id]],
                          constant Peak* peaks           [[buffer(0)]],
                          constant WaveformUniforms& u   [[buffer(1)]]) {
    const uint peakIndex = vid / 2u;
    const bool isMax     = (vid & 1u) == 1u;

    const float peakSample = u.firstVisiblePeakSample
                              + float(peakIndex) * u.samplesPerPeak;

    // Wrap peakIndexInLoop into [0, peakCount) using floor division so
    // negative "virtual" indices map correctly.
    const int cnt = int(u.peakCount);
    int idx       = int(floor(peakSample / u.samplesPerPeak));
    idx = ((idx % cnt) + cnt) % cnt;
    const Peak p = peaks[uint(idx)];

    const float y = (isMax ? p.mx : p.mn) * u.yScale;

    const float samplesPerPixel = u.viewportSamples / u.resolutionX;
    const float xPx  = (peakSample - u.samplePosition) / samplesPerPixel
                          + u.resolutionX * 0.5;
    const float xNdc = 2.0 * xPx / u.resolutionX - 1.0;

    VtxOut o;
    o.position = float4(xNdc, y, 0.0, 1.0);
    o.color    = xNdc < 0.0 ? u.colorPlayed : u.colorUpcoming;
    return o;
}

fragment float4 peak_fragment(VtxOut in [[stage_in]]) {
    return in.color;
}
)metal";

id<MTLRenderPipelineState> makePipeline(id<MTLDevice> device,
                                        id<MTLLibrary> library,
                                        NSString* vertexName,
                                        NSString* fragmentName,
                                        NSError** outError) {
    id<MTLFunction> vs = [library newFunctionWithName:vertexName];
    id<MTLFunction> fs = [library newFunctionWithName:fragmentName];
    if (vs == nil || fs == nil) {
        return nil;
    }

    MTLRenderPipelineDescriptor* desc = [[MTLRenderPipelineDescriptor alloc] init];
    desc.vertexFunction   = vs;
    desc.fragmentFunction = fs;
    desc.rasterSampleCount = MetalContext::kSampleCount;
    desc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    desc.colorAttachments[0].blendingEnabled = YES;
    desc.colorAttachments[0].rgbBlendOperation   = MTLBlendOperationAdd;
    desc.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
    desc.colorAttachments[0].sourceRGBBlendFactor        = MTLBlendFactorSourceAlpha;
    desc.colorAttachments[0].sourceAlphaBlendFactor      = MTLBlendFactorOne;
    desc.colorAttachments[0].destinationRGBBlendFactor   = MTLBlendFactorOneMinusSourceAlpha;
    desc.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;

    id<MTLRenderPipelineState> pipeline =
        [device newRenderPipelineStateWithDescriptor:desc error:outError];

    [desc release];
    [vs release];
    [fs release];
    return pipeline;
}

}  // namespace

MetalContext& MetalContext::instance() {
    static MetalContext s;
    return s;
}

MetalContext::MetalContext() {
    @autoreleasepool {
        m_device = MTLCreateSystemDefaultDevice();
        if (m_device == nil) {
            LOG_ERROR("Metal: no default device");
            return;
        }
        [m_device retain];

        m_commandQueue = [m_device newCommandQueue];
        if (m_commandQueue == nil) {
            LOG_ERROR("Metal: failed to create command queue");
            return;
        }

        NSError* err = nil;
        NSString* src = [NSString stringWithUTF8String:kShaderSource];
        m_library = [m_device newLibraryWithSource:src options:nil error:&err];
        if (m_library == nil) {
            LOG_ERROR(std::string("Metal: shader compile failed: ")
                      + std::string(err.localizedDescription.UTF8String
                                        ? err.localizedDescription.UTF8String
                                        : "unknown"));
            return;
        }

        m_linePipeline  = makePipeline(m_device, m_library, @"line_vertex",  @"line_fragment",  &err);
        if (m_linePipeline == nil) {
            LOG_ERROR(std::string("Metal: line pipeline creation failed: ")
                      + std::string(err.localizedDescription.UTF8String
                                        ? err.localizedDescription.UTF8String
                                        : "unknown"));
            return;
        }

        m_peaksPipeline = makePipeline(m_device, m_library, @"peak_vertex",  @"peak_fragment",  &err);
        if (m_peaksPipeline == nil) {
            LOG_ERROR(std::string("Metal: peaks pipeline creation failed: ")
                      + std::string(err.localizedDescription.UTF8String
                                        ? err.localizedDescription.UTF8String
                                        : "unknown"));
            return;
        }

        LOG_INFO(std::string("Metal ready: ") + m_device.name.UTF8String
                 + std::string("  (MSAA x") + std::to_string(kSampleCount) + ")");
    }
}

MetalContext::~MetalContext() {
    [m_peaksPipeline release];
    [m_linePipeline release];
    [m_library release];
    [m_commandQueue release];
    [m_device release];
}

}  // namespace loopa::app
