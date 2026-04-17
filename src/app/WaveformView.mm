#import "WaveformView.h"

#import "MetalContext.h"
#import "Theme.h"

#import "core/Logger.h"
#import "core/Loop.h"
#import "core/LooperEngine.h"
#import "core/Track.h"

#import <AppKit/AppKit.h>
#import <QuartzCore/CAMetalLayer.h>
#import <QuartzCore/CATextLayer.h>
#import <QuartzCore/CATransaction.h>
#import <simd/simd.h>

#import <algorithm>
#import <cmath>
#import <memory>
#import <vector>

namespace loopa::app {

namespace {

// CPU mirror of the MSL LineVertex.
struct LineVertex {
    simd::float2 position;
    simd::float4 color;
};

// CPU mirror of MSL WaveformUniforms.
struct WaveformUniforms {
    float  samplesPerPeak;
    float  samplePosition;
    float  viewportSamples;
    float  resolutionX;
    float  yScale;
    float  peakCount;
    float  firstVisiblePeakSample;
    float  _pad0;
    simd::float4 colorPlayed;
    simd::float4 colorUpcoming;
};

// CPU mirror of MSL Peak.
struct Peak {
    float mn;
    float mx;
};

constexpr double kViewportSeconds = 4.0;
constexpr int    kSamplesPerPeak  = 32;
constexpr float  kWaveformYScale  = 0.92f;

// Max loop = 16 bars @ 60 BPM @ 48 kHz = 3 072 000 samples → 96 000 peaks.
// Add 10 % headroom; keep it fixed so the GPU buffer never reallocates.
constexpr std::size_t kMaxPeakCount = 112000;

simd::float4 argbToFloat4(juce::uint32 argb) {
    const auto a = static_cast<float>((argb >> 24) & 0xFFU) / 255.0f;
    const auto r = static_cast<float>((argb >> 16) & 0xFFU) / 255.0f;
    const auto g = static_cast<float>((argb >> 8)  & 0xFFU) / 255.0f;
    const auto b = static_cast<float>((argb)       & 0xFFU) / 255.0f;
    return simd::make_float4(r, g, b, a);
}

simd::float4 colToFloat4(juce::Colour c) {
    return argbToFloat4(c.getARGB());
}

}  // namespace

}  // namespace loopa::app

@interface LoopaMetalView : NSView
@end

@implementation LoopaMetalView
- (instancetype)initWithFrame:(NSRect)frameRect {
    self = [super initWithFrame:frameRect];
    if (self != nil) {
        self.wantsLayer = YES;
        self.layer = [CAMetalLayer layer];
        CAMetalLayer* ml = (CAMetalLayer*)self.layer;
        ml.device = loopa::app::MetalContext::instance().device();
        ml.pixelFormat = MTLPixelFormatBGRA8Unorm;
        ml.framebufferOnly = NO;
        ml.opaque = NO;
    }
    return self;
}
- (BOOL)isOpaque { return NO; }
- (BOOL)wantsUpdateLayer { return YES; }
@end

namespace loopa::app {

struct WaveformView::Impl {
    loopa::LooperEngine& engine;
    const int trackId;

    LoopaMetalView* view = nil;
    CAMetalLayer*   layer = nil;

    id<MTLBuffer>   peaksBuffer     = nil;
    id<MTLBuffer>   uniformsBuffer  = nil;
    id<MTLBuffer>   lineBuffer      = nil;
    std::size_t     lineBufferCap   = 0;

    id<MTLTexture>  msaaTexture     = nil;
    CGSize          msaaSize        = CGSizeZero;

    std::vector<LineVertex> lineScratch;
    std::vector<Peak>       peakScratch;

    // Peak cache tracking
    std::shared_ptr<const Loop> peaksLoopKeepalive;
    const Loop*                 peaksLoopPtr    = nullptr;
    std::uint64_t               peaksVersion    = 0;
    std::size_t                 peakCount       = 0;

    // Count-in overlay
    CATextLayer* countInLayer = nil;
    int lastCountIn = -1;

    Impl(loopa::LooperEngine& e, int t) : engine(e), trackId(t) {
        @autoreleasepool {
            view = [[LoopaMetalView alloc] initWithFrame:NSMakeRect(0, 0, 200, 100)];
            layer = (CAMetalLayer*)view.layer;

            auto device = MetalContext::instance().device();
            peaksBuffer = [[device newBufferWithLength:kMaxPeakCount * sizeof(Peak)
                                                options:MTLResourceStorageModeShared] retain];
            uniformsBuffer = [[device newBufferWithLength:sizeof(WaveformUniforms)
                                                   options:MTLResourceStorageModeShared] retain];
            // Line buffer starts small; grows on demand.
            lineBufferCap = 2048;
            lineBuffer = [[device newBufferWithLength:lineBufferCap * sizeof(LineVertex)
                                              options:MTLResourceStorageModeShared] retain];

            countInLayer = [[CATextLayer alloc] init];
            countInLayer.alignmentMode = kCAAlignmentCenter;
            countInLayer.fontSize = 64.0;
            countInLayer.font = (__bridge CFTypeRef)[NSFont systemFontOfSize:64 weight:NSFontWeightBold];
            const auto a = argbToFloat4(theme::kAccent);
            CGColorRef accentCG = CGColorCreateGenericRGB(a.x, a.y, a.z, a.w);
            countInLayer.foregroundColor = accentCG;
            CGColorRelease(accentCG);
            countInLayer.backgroundColor = nil;
            countInLayer.hidden = YES;
            [layer addSublayer:countInLayer];
        }
    }

    ~Impl() {
        [msaaTexture release];
        [lineBuffer release];
        [uniformsBuffer release];
        [peaksBuffer release];
        [countInLayer release];
        [view release];
    }

    void ensureLineBufferCapacity(std::size_t needed) {
        if (needed <= lineBufferCap) return;
        [lineBuffer release];
        lineBufferCap = needed * 2;
        auto device = MetalContext::instance().device();
        lineBuffer = [[device newBufferWithLength:lineBufferCap * sizeof(LineVertex)
                                          options:MTLResourceStorageModeShared] retain];
    }

    void ensureMsaaTexture(CGSize drawableSize) {
        const auto sameSize = [](CGFloat a, CGFloat b) {
            return std::fabs(a - b) < 0.5;
        };
        if (msaaTexture != nil
            && sameSize(msaaSize.width,  drawableSize.width)
            && sameSize(msaaSize.height, drawableSize.height)) {
            return;
        }
        [msaaTexture release];
        msaaTexture = nil;
        msaaSize = drawableSize;

        if (drawableSize.width <= 0 || drawableSize.height <= 0) return;

        MTLTextureDescriptor* desc =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                width:(NSUInteger)drawableSize.width
                                                               height:(NSUInteger)drawableSize.height
                                                            mipmapped:NO];
        desc.textureType = MTLTextureType2DMultisample;
        desc.sampleCount = MetalContext::kSampleCount;
        desc.usage = MTLTextureUsageRenderTarget;
        desc.storageMode = MTLStorageModePrivate;
        msaaTexture = [[MetalContext::instance().device() newTextureWithDescriptor:desc] retain];
    }

    void refreshPeaks(const std::shared_ptr<const Loop>& loop) {
        if (!loop || loop->empty()) {
            peaksLoopKeepalive.reset();
            peaksLoopPtr = nullptr;
            peakCount = 0;
            return;
        }
        const auto version = loop->version();
        if (loop.get() == peaksLoopPtr && version == peaksVersion && peakCount > 0) {
            return;  // still current
        }

        const std::size_t len = loop->length();
        const std::size_t count = (len + kSamplesPerPeak - 1) / kSamplesPerPeak;
        const std::size_t writeCount = std::min(count, kMaxPeakCount);

        peakScratch.resize(writeCount);
        const float* s = loop->data();
        for (std::size_t k = 0; k < writeCount; ++k) {
            const std::size_t start = k * kSamplesPerPeak;
            const std::size_t end   = std::min(start + kSamplesPerPeak, len);
            float mn =  1.0f, mx = -1.0f;
            for (std::size_t i = start; i < end; ++i) {
                const float v = s[i];
                if (v < mn) mn = v;
                if (v > mx) mx = v;
            }
            if (mn > mx) { mn = 0.0f; mx = 0.0f; }
            peakScratch[k] = {mn, mx};
        }

        std::memcpy([peaksBuffer contents], peakScratch.data(),
                    writeCount * sizeof(Peak));

        peaksLoopKeepalive = loop;
        peaksLoopPtr       = loop.get();
        peaksVersion       = version;
        peakCount          = writeCount;
    }

    void updateCountInOverlay(const loopa::LooperEngine::Snapshot& snap) {
        const auto ix = static_cast<std::size_t>(trackId);
        const bool inCountIn =
            snap.trackRecordState[ix] == static_cast<int>(loopa::RecordState::CountIn);

        if (!inCountIn) {
            if (!countInLayer.hidden) {
                countInLayer.hidden = YES;
                lastCountIn = -1;
            }
            return;
        }

        int beatsRemaining = 0;
        if (snap.totalLengthSamples > 0 && snap.totalBars > 0) {
            const double samplesPerBeat =
                static_cast<double>(snap.totalLengthSamples) /
                (static_cast<double>(snap.totalBars) * static_cast<double>(snap.beatsPerBar));
            const double samplesLeft =
                static_cast<double>(snap.totalLengthSamples - snap.samplePosition);
            beatsRemaining = static_cast<int>(std::ceil(samplesLeft / std::max(1.0, samplesPerBeat)));
            if (beatsRemaining < 1) beatsRemaining = 1;
        }

        if (beatsRemaining != lastCountIn) {
            lastCountIn = beatsRemaining;
            NSString* txt = [NSString stringWithFormat:@"%d", beatsRemaining];
            [CATransaction begin];
            [CATransaction setDisableActions:YES];
            countInLayer.string = txt;
            countInLayer.hidden = NO;
            [CATransaction commit];
        }
    }

    void layoutOverlay(CGFloat w, CGFloat h) {
        const CGFloat side = std::min(w, h);
        countInLayer.frame = CGRectMake((w - side) * 0.5,
                                        (h - side) * 0.5,
                                        side, side);
    }

    // CPU-side grid + playhead + (for recording) live trace emission.
    void buildLineVertices(const loopa::LooperEngine::Snapshot& snap,
                           const std::shared_ptr<const Loop>& loop,
                           int pixelW) {
        lineScratch.clear();
        if (pixelW <= 0) return;

        const auto ix = static_cast<std::size_t>(trackId);
        const bool isRecording =
            snap.trackRecordState[ix] == static_cast<int>(loopa::RecordState::Recording);

        const double sr = engine.currentSampleRate() > 0.0
                            ? engine.currentSampleRate() : 48000.0;
        const double viewportSamples = kViewportSeconds * sr;

        auto pushLine = [&](float x0, float y0, float x1, float y1, simd::float4 c) {
            lineScratch.push_back({simd::make_float2(x0, y0), c});
            lineScratch.push_back({simd::make_float2(x1, y1), c});
        };

        if (isRecording) {
            // Live recording trace: draw captured samples from recording buffer.
            const float* rec = engine.recordingBufferData(trackId);
            const std::size_t written = static_cast<std::size_t>(
                engine.recordingBufferWritten(trackId));
            const simd::float4 colRec = colToFloat4(theme::trackRecording(trackId));

            if (rec != nullptr && written > 0) {
                const double samplesPerPixel = viewportSamples / static_cast<double>(pixelW);
                const std::size_t center = written;
                for (int x = 0; x <= pixelW / 2; ++x) {
                    const double absOffset = (x - pixelW / 2.0) * samplesPerPixel;
                    const long long rawIdx = static_cast<long long>(
                        static_cast<double>(center) + absOffset);
                    if (rawIdx < 0 || rawIdx >= static_cast<long long>(written)) continue;

                    const std::size_t s0 = static_cast<std::size_t>(rawIdx);
                    const std::size_t span = static_cast<std::size_t>(std::max(1.0, samplesPerPixel));
                    const std::size_t end = (s0 + span > written) ? written : s0 + span;

                    float mn =  1.0f, mx = -1.0f;
                    for (std::size_t k = s0; k < end; ++k) {
                        const float v = rec[k];
                        if (v < mn) mn = v;
                        if (v > mx) mx = v;
                    }
                    if (mn > mx) { mn = 0.0f; mx = 0.0f; }

                    const float xNdc = 2.0f * (static_cast<float>(x) + 0.5f)
                                         / static_cast<float>(pixelW) - 1.0f;
                    pushLine(xNdc, mn * kWaveformYScale, xNdc, mx * kWaveformYScale, colRec);
                }
            }
            // Playhead at centre.
            const simd::float4 colPh = colToFloat4(theme::trackPlayhead(trackId));
            pushLine(0.0f, -1.0f, 0.0f, 1.0f, colPh);
            return;
        }

        // Playback: grid lines derived from the loop's own (bars, length).
        if (loop && loop->bars() > 0 && snap.beatsPerBar > 0 && loop->length() > 0) {
            const simd::float4 colBeat      = colToFloat4(theme::trackBeat(trackId));
            const simd::float4 colBar       = colToFloat4(theme::trackBar(trackId));
            const simd::float4 colLoopStart = colToFloat4(theme::trackLoopStart(trackId));

            const double loopLenD       = static_cast<double>(loop->length());
            const double bars           = static_cast<double>(loop->bars());
            const double bpbD           = static_cast<double>(snap.beatsPerBar);
            const double samplesPerBar  = loopLenD / bars;
            const double samplesPerBeat = samplesPerBar / bpbD;
            const double samplesPerPixel = viewportSamples / static_cast<double>(pixelW);

            const double leftVirt  = static_cast<double>(snap.samplePosition) - viewportSamples * 0.5;
            const double rightVirt = leftVirt + viewportSamples;

            auto emit = [&](double virtualSample, simd::float4 c) {
                const double px = (virtualSample - leftVirt) / samplesPerPixel;
                if (px < 0.0 || px >= static_cast<double>(pixelW)) return;
                const float xNdc = 2.0f * (static_cast<float>(px) + 0.5f)
                                     / static_cast<float>(pixelW) - 1.0f;
                pushLine(xNdc, -1.0f, xNdc, 1.0f, c);
            };

            const int bpbInt  = snap.beatsPerBar;
            const int barsInt = loop->bars();

            // Beats (skip those that coincide with bars).
            const long long firstBeat = static_cast<long long>(std::floor(leftVirt / samplesPerBeat));
            const long long lastBeat  = static_cast<long long>(std::ceil(rightVirt / samplesPerBeat));
            for (long long k = firstBeat; k <= lastBeat; ++k) {
                if (bpbInt > 0 && (k % bpbInt) == 0) continue;
                emit(static_cast<double>(k) * samplesPerBeat, colBeat);
            }

            // Bars (skip those that coincide with loop-start).
            const long long firstBar = static_cast<long long>(std::floor(leftVirt / samplesPerBar));
            const long long lastBar  = static_cast<long long>(std::ceil(rightVirt / samplesPerBar));
            for (long long m = firstBar; m <= lastBar; ++m) {
                if (barsInt > 0 && (m % barsInt) == 0) continue;
                emit(static_cast<double>(m) * samplesPerBar, colBar);
            }

            // Loop starts (strongest).
            const long long firstLoop = static_cast<long long>(std::floor(leftVirt / loopLenD));
            const long long lastLoop  = static_cast<long long>(std::ceil(rightVirt / loopLenD));
            for (long long n = firstLoop; n <= lastLoop; ++n) {
                emit(static_cast<double>(n) * loopLenD, colLoopStart);
            }
        }

        // Playhead at centre — always.
        const simd::float4 colPh = colToFloat4(theme::trackPlayhead(trackId));
        pushLine(0.0f, -1.0f, 0.0f, 1.0f, colPh);
    }

    // Compute per-frame uniforms for the peaks pipeline. Returns the number of
    // visible peak *indices* (draw vertex count will be 2 × that).
    std::size_t writeUniforms(const loopa::LooperEngine::Snapshot& snap,
                              const std::shared_ptr<const Loop>& loop,
                              int pixelW) {
        if (pixelW <= 0 || peakCount == 0 || !loop) return 0;

        const double sr = engine.currentSampleRate() > 0.0
                            ? engine.currentSampleRate() : 48000.0;
        const double viewportSamples = kViewportSeconds * sr;

        const double samplesPerPeakD = static_cast<double>(kSamplesPerPeak);
        const double leftVirt = static_cast<double>(snap.samplePosition) - viewportSamples * 0.5;
        const double rightVirt = leftVirt + viewportSamples;

        const long long firstPeak = static_cast<long long>(std::floor(leftVirt / samplesPerPeakD));
        const long long lastPeak  = static_cast<long long>(std::ceil(rightVirt / samplesPerPeakD));
        if (lastPeak <= firstPeak) return 0;

        const double firstVisibleSample = static_cast<double>(firstPeak) * samplesPerPeakD;
        const std::size_t visiblePeaks =
            static_cast<std::size_t>(lastPeak - firstPeak + 1);

        WaveformUniforms u{};
        u.samplesPerPeak         = static_cast<float>(samplesPerPeakD);
        u.samplePosition         = static_cast<float>(snap.samplePosition);
        u.viewportSamples        = static_cast<float>(viewportSamples);
        u.resolutionX            = static_cast<float>(pixelW);
        u.yScale                 = kWaveformYScale;
        u.peakCount              = static_cast<float>(peakCount);
        u.firstVisiblePeakSample = static_cast<float>(firstVisibleSample);
        u.colorPlayed            = colToFloat4(theme::trackPlayed(trackId));
        u.colorUpcoming          = colToFloat4(theme::trackUpcoming(trackId));

        std::memcpy([uniformsBuffer contents], &u, sizeof(u));
        return visiblePeaks;
    }

    void render(const loopa::LooperEngine::Snapshot& snap,
                int pixelW, int pixelH) {
        if (!MetalContext::instance().isValid() || layer == nil) return;
        if (pixelW <= 0 || pixelH <= 0) return;

        @autoreleasepool {
            auto loopShared = engine.activeLoopForUi(trackId);
            refreshPeaks(loopShared);

            const auto ix = static_cast<std::size_t>(trackId);
            const bool isRecording =
                snap.trackRecordState[ix] == static_cast<int>(loopa::RecordState::Recording);

            const std::size_t visiblePeaks = isRecording
                ? std::size_t{0}
                : writeUniforms(snap, loopShared, pixelW);

            buildLineVertices(snap, loopShared, pixelW);
            if (!lineScratch.empty()) {
                ensureLineBufferCapacity(lineScratch.size());
                std::memcpy([lineBuffer contents], lineScratch.data(),
                            lineScratch.size() * sizeof(LineVertex));
            }

            id<CAMetalDrawable> drawable = [layer nextDrawable];
            if (drawable == nil) return;

            ensureMsaaTexture(CGSizeMake(pixelW, pixelH));
            if (msaaTexture == nil) return;

            MTLRenderPassDescriptor* rpd = [MTLRenderPassDescriptor renderPassDescriptor];
            rpd.colorAttachments[0].texture        = msaaTexture;
            rpd.colorAttachments[0].resolveTexture = drawable.texture;
            rpd.colorAttachments[0].loadAction     = MTLLoadActionClear;
            rpd.colorAttachments[0].storeAction    = MTLStoreActionMultisampleResolve;
            const auto bg = argbToFloat4(theme::kBg2);
            rpd.colorAttachments[0].clearColor     = MTLClearColorMake(bg.x, bg.y, bg.z, bg.w);

            id<MTLCommandBuffer> cmd = [MetalContext::instance().commandQueue() commandBuffer];
            id<MTLRenderCommandEncoder> enc = [cmd renderCommandEncoderWithDescriptor:rpd];

            if (visiblePeaks > 0) {
                [enc setRenderPipelineState:MetalContext::instance().peaksPipeline()];
                [enc setVertexBuffer:peaksBuffer    offset:0 atIndex:0];
                [enc setVertexBuffer:uniformsBuffer offset:0 atIndex:1];
                [enc drawPrimitives:MTLPrimitiveTypeTriangleStrip
                        vertexStart:0
                        vertexCount:visiblePeaks * 2u];
            }

            if (!lineScratch.empty()) {
                [enc setRenderPipelineState:MetalContext::instance().linePipeline()];
                [enc setVertexBuffer:lineBuffer offset:0 atIndex:0];
                [enc drawPrimitives:MTLPrimitiveTypeLine
                        vertexStart:0
                        vertexCount:lineScratch.size()];
            }

            [enc endEncoding];
            [cmd presentDrawable:drawable];
            [cmd commit];
        }

        updateCountInOverlay(snap);
    }
};

WaveformView::WaveformView(loopa::LooperEngine& engine, int trackId)
    : m_impl(std::make_unique<Impl>(engine, trackId)) {
    setView(m_impl->view);
}

WaveformView::~WaveformView() {
    setView(nullptr);
}

void WaveformView::resized() {
    juce::NSViewComponent::resized();
    @autoreleasepool {
        if (m_impl && m_impl->layer) {
            const auto b = getLocalBounds();
            const auto scale = static_cast<CGFloat>(
                juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() != nullptr
                    ? juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->scale
                    : 1.0);
            m_impl->layer.contentsScale = scale;
            m_impl->layer.drawableSize = CGSizeMake(b.getWidth()  * scale,
                                                    b.getHeight() * scale);
            m_impl->countInLayer.contentsScale = scale;
            m_impl->layoutOverlay(b.getWidth(), b.getHeight());
        }
    }
}

void WaveformView::renderNow(const loopa::LooperEngine::Snapshot& snapshot) {
    if (!m_impl || m_impl->layer == nil) return;
    const auto b = getLocalBounds();
    if (b.isEmpty()) return;
    const auto scale = m_impl->layer.contentsScale > 0.0 ? m_impl->layer.contentsScale : 1.0;
    m_impl->render(snapshot,
                   static_cast<int>(b.getWidth()  * scale),
                   static_cast<int>(b.getHeight() * scale));
}

}  // namespace loopa::app
