/*
  ==============================================================================

   This file is part of the JUCE 8 technical preview.
   Copyright (c) Raw Material Software Limited

   You may use this code under the terms of the GPL v3
   (see www.gnu.org/licenses).

   For the technical preview this file cannot be licensed commercially.

   JUCE IS PROVIDED "AS IS" WITHOUT ANY WARRANTY, AND ALL WARRANTIES, WHETHER
   EXPRESSED OR IMPLIED, INCLUDING MERCHANTABILITY AND FITNESS FOR PURPOSE, ARE
   DISCLAIMED.

  ==============================================================================
*/

#pragma once

namespace juce
{
    namespace etw
    {
        enum
        {
            paintKeyword = 1,
            sizeKeyword = 2,
            graphicsKeyword = 4,
            crucialKeyword = 8,
            threadPaintKeyword = 16,
            messageKeyword = 32,
            direct2dKeyword = 64,
            softwareRendererKeyword = 128,
            resourcesKeyword = 256,
            componentKeyword = 512
        };

        enum : uint16
        {
            direct2dPaintStart = 0,
            direct2dPaintEnd,
            present1SwapChainStart,
            present1SwapChainEnd,
            swapChainThreadEvent,
            waitForVBlankDone,
            callVBlankListeners,
            resize,
            createResource,
            presentIdleFrame,

            createLowLevelGraphicsContext,
            createDeviceResources,
            createSwapChain,
            createSwapChainBuffer,
            createPeer,
            mapBitmap,
            unmapBitmap,

            setOrigin,
            addTransform,
            clipToRectangle,
            clipToRectangleList,
            excludeClipRectangle,
            clipToPath,
            clipToImageAlpha,
            saveState,
            restoreState,
            beginTransparencyLayer,
            endTransparencyLayer,
            setFill,
            setOpacity,
            setInterpolationQuality,
            fillRect,
            fillRectReplace,
            fillRectList,
            drawRectTranslated,
            drawRectTransformed,
            drawRect,
            fillPath,
            strokePath,
            drawPath,
            drawImage,
            drawLine,
            setFont,
            drawGlyph,
            drawGlyphRun,
            drawTextLayout,
            drawRoundedRectangle,
            fillRoundedRectangle,
            drawEllipse,
            fillEllipse,
            setOriginDone,
            addTransformDone,
            clipToRectangleDone,
            clipToRectangleListDone,
            excludeClipRectangleDone,
            clipToPathDone,
            clipToImageAlphaDone,
            saveStateDone,
            restoreStateDone,
            beginTransparencyLayerDone,
            endTransparencyLayerDone,
            setFillDone,
            setOpacityDone,
            setInterpolationQualityDone,
            fillRectDone,
            fillRectListDone,
            drawRectDone,
            fillPathDone,
            drawPathDone,
            drawImageDone,
            drawLineDone,
            setFontDone,
            drawGlyphDone,
            drawGlyphRunDone,
            drawTextLayoutDone,
            drawRoundedRectangleDone,
            fillRoundedRectangleDone,
            drawEllipseDone,
            fillEllipseDone,
            drawGeometryDone,
            fillGeometryDone,
            drawGeometryRealizationDone,
            fillGeometryRealizationDone,
            filledGeometryRealizationCacheHit,
            filledGeometryRealizationCreated,
            strokedGeometryRealizationCacheHit,
            strokedGeometryRealizationCreated,
            gradientCacheHit,
            gradientCreated,

            resetToDefaultState,
            reduceClipRegionRectangle,
            reduceClipRegionRectangleList,
            reduceClipRegionImage,
            reduceClipRegionPath,
            excludeClipRegion,
            fillAll,

            paintComponentAndChildren,
            paintWithinParentContext
        };
    }
}

#if JUCE_ETW_TRACELOGGING

#define JUCE_ETW_TRACELOGGING_PROVIDER_HANDLE ETWGlobalTraceLoggingProvider

JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE("-Wc++98-compat-extra-semi", "-Wmissing-prototypes", "-Wgnu-zero-variadic-macro-arguments")
TRACELOGGING_DECLARE_PROVIDER (JUCE_ETW_TRACELOGGING_PROVIDER_HANDLE);
JUCE_END_IGNORE_WARNINGS_GCC_LIKE

#define TraceLoggingWriteWrapper(hProvider, eventName, ...) TraceLoggingWrite(hProvider, eventName, __VA_ARGS__)

#define SCOPED_TRACE_EVENT(code, etwFrameNumber, keyword) \
struct ScopedTraceEvent \
{ \
    ~ScopedTraceEvent() \
    {\
        int64 elapsedTicks = Time::getHighResolutionTicks() - startTicks;\
        TraceLoggingWriteWrapper(JUCE_ETW_TRACELOGGING_PROVIDER_HANDLE, \
            # code,\
            TraceLoggingLevel(TRACE_LEVEL_INFORMATION), \
            TraceLoggingKeyword(keyword), \
            TraceLoggingInt32(code, "code"), \
            TraceLoggingInt32 (frameNumber, "frame"),\
            TraceLoggingInt64(elapsedTicks, "elapsedTicks"));\
    }\
\
    int64_t startTicks = Time::getHighResolutionTicks();\
    int frameNumber = -1; \
} ste; \
ste.frameNumber = etwFrameNumber;

#define SCOPED_TRACE_EVENT_INT_RECT(code, etwFrameNumber, area, keyword) \
struct ScopedTraceEvent \
{ \
    ScopedTraceEvent(Rectangle<int> area) \
    {\
        array[0] = area.getX(); array[1] = area.getY(); array[2] = area.getWidth(); array[3] = area.getHeight();\
    }\
    ~ScopedTraceEvent()\
    {\
        int64 elapsedTicks = Time::getHighResolutionTicks() - startTicks;\
        TraceLoggingWriteWrapper(JUCE_ETW_TRACELOGGING_PROVIDER_HANDLE, \
            # code,\
            TraceLoggingLevel(TRACE_LEVEL_INFORMATION), \
            TraceLoggingKeyword(keyword), \
            TraceLoggingInt32(code, "code"), \
            TraceLoggingInt32 (frameNumber, "frame"),\
            TraceLoggingInt64(elapsedTicks, "elapsedTicks"), \
            TraceLoggingInt32FixedArray(array, 4, "area"));\
    }\
\
    int64_t startTicks = Time::getHighResolutionTicks();\
    int frameNumber = 0; \
    int32_t array[4];\
} ste{ area }; \
ste.frameNumber = etwFrameNumber;

#define SCOPED_TRACE_EVENT_INT_XYWH(code, etwFrameNumber, x, y, w, h, keyword) \
struct ScopedTraceEvent \
{ \
    ~ScopedTraceEvent()\
    {\
        int64 elapsedTicks = Time::getHighResolutionTicks() - startTicks;\
        TraceLoggingWriteWrapper(JUCE_ETW_TRACELOGGING_PROVIDER_HANDLE, \
            # code,\
            TraceLoggingLevel(TRACE_LEVEL_INFORMATION), \
            TraceLoggingKeyword(keyword), \
            TraceLoggingInt32(code, "code"), \
            TraceLoggingInt32 (frameNumber, "frame"),\
            TraceLoggingInt64(elapsedTicks, "elapsedTicks"), \
            TraceLoggingInt32FixedArray(array, 4, "area"));\
    }\
\
    int64 startTicks = Time::getHighResolutionTicks();\
    int frameNumber = 0; \
    int32_t array[4];\
} ste; \
ste.array[0] = x; ste.array[1] = y; ste.array[2] = w; ste.array[3] = h; \
ste.frameNumber = etwFrameNumber;


#define SCOPED_TRACE_EVENT_FLOAT_RECT(code, etwFrameNumber, area, keyword) \
struct ScopedTraceEvent \
{ \
    ScopedTraceEvent(Rectangle<float> area) \
    {\
        array[0] = area.getX(); array[1] = area.getY(); array[2] = area.getWidth(); array[3] = area.getHeight();\
    }\
    ~ScopedTraceEvent()\
    {\
        int64 elapsedTicks = Time::getHighResolutionTicks() - startTicks;\
        TraceLoggingWriteWrapper(JUCE_ETW_TRACELOGGING_PROVIDER_HANDLE, \
            # code,\
            TraceLoggingLevel(TRACE_LEVEL_INFORMATION), \
            TraceLoggingKeyword(keyword), \
            TraceLoggingInt32(code, "code"), \
            TraceLoggingInt32 (frameNumber, "frame"),\
            TraceLoggingInt64(elapsedTicks, "elapsedTicks"), \
            TraceLoggingFloat32FixedArray(array, 4, "area"));\
    }\
\
    int64 startTicks = Time::getHighResolutionTicks();\
    int frameNumber = 0; \
    float array[4];\
} ste{ area }; \
ste.frameNumber = etwFrameNumber;

#define SCOPED_TRACE_EVENT_FLOAT_XYWH(code, etwFrameNumber, x, y, w, h, keyword) \
struct ScopedTraceEvent \
{ \
    ~ScopedTraceEvent()\
    {\
        int64 elapsedTicks = Time::getHighResolutionTicks() - startTicks;\
        TraceLoggingWriteWrapper(JUCE_ETW_TRACELOGGING_PROVIDER_HANDLE, \
            # code,\
            TraceLoggingLevel(TRACE_LEVEL_INFORMATION), \
            TraceLoggingKeyword(keyword), \
            TraceLoggingInt32(code, "code"), \
            TraceLoggingInt32 (frameNumber, "frame"),\
            TraceLoggingInt64(elapsedTicks, "elapsedTicks"), \
            TraceLoggingFloat32FixedArray(array, 4, "area"));\
    }\
\
    int64 startTicks = Time::getHighResolutionTicks();\
    float array[4];\
    int frameNumber = 0; \
} ste; \
ste.array[0] = x; ste.array[1] = y; ste.array[2] = w; ste.array[3] = h; \
ste.frameNumber = etwFrameNumber;


#define SCOPED_TRACE_EVENT_INT_RECT_LIST(code, etwFrameNumber, list, keyword) \
struct ScopedTraceEvent \
{ \
    ScopedTraceEvent(RectangleList<int> const& list) :\
        array{ list.getNumRectangles() * 4, false }, \
        arrayLength(list.getNumRectangles() * 4) \
    {\
        int32* dest = array.getData();\
\
        for (auto const& r : list)\
        {\
            dest[0] = r.getX(); dest[1] = r.getY(); dest[2] = r.getWidth(); dest[3] = r.getHeight();\
        }\
    }\
    ~ScopedTraceEvent()\
    {\
        int64_t elapsedTicks = Time::getHighResolutionTicks() - startTicks;\
        TraceLoggingWriteWrapper(JUCE_ETW_TRACELOGGING_PROVIDER_HANDLE,\
            # code,\
            TraceLoggingLevel(TRACE_LEVEL_INFORMATION),\
            TraceLoggingKeyword(keyword),\
            TraceLoggingInt32(code, "code"),\
            TraceLoggingInt32 (frameNumber, "frame"),\
            TraceLoggingInt64(elapsedTicks, "elapsedTicks"),\
            TraceLoggingInt32Array(array, (uint16)arrayLength, "list"));\
    }\
    \
    int64 startTicks = Time::getHighResolutionTicks();\
    HeapBlock<int32_t> array;\
    int arrayLength = 0;\
    int frameNumber = 0; \
} ste{ list }; \
ste.frameNumber = etwFrameNumber;

#define SCOPED_TRACE_EVENT_FLOAT_RECT_LIST(code, etwFrameNumber, list, keyword) \
struct ScopedTraceEvent \
{ \
    ScopedTraceEvent(RectangleList<float> const& list) :\
        array{ list.getNumRectangles() * 4, false }, \
        arrayLength(list.getNumRectangles() * 4) \
    {\
        float* dest = array.getData();\
\
        for (auto const& r : list)\
        {\
            dest[0] = r.getX(); dest[1] = r.getY(); dest[2] = r.getWidth(); dest[3] = r.getHeight();\
        }\
    }\
    ~ScopedTraceEvent()\
    {\
        int64_t elapsedTicks = Time::getHighResolutionTicks() - startTicks;\
        TraceLoggingWriteWrapper(JUCE_ETW_TRACELOGGING_PROVIDER_HANDLE,\
            # code,\
            TraceLoggingLevel(TRACE_LEVEL_INFORMATION),\
            TraceLoggingKeyword(keyword),\
            TraceLoggingInt32(code, "code"),\
            TraceLoggingInt32 (frameNumber, "frame"),\
            TraceLoggingInt64(elapsedTicks, "elapsedTicks"),\
            TraceLoggingFloat32Array(array, (uint16)arrayLength, "list"));\
    }\
    \
    int64 startTicks = Time::getHighResolutionTicks();\
    HeapBlock<float> array;\
    int arrayLength = 0;\
    int frameNumber = 0; \
} ste{ list };\
ste.frameNumber = etwFrameNumber;

#else

#define TraceLoggingWriteWrapper(hProvider, eventName, ...)

#define SCOPED_TRACE_EVENT(code, etwFrameNumber, keyword)
#define SCOPED_TRACE_EVENT_INT_RECT(code, etwFrameNumber, area, keyword)
#define SCOPED_TRACE_EVENT_INT_XYWH(code, etwFrameNumber, x, y, w, h, keyword)
#define SCOPED_TRACE_EVENT_FLOAT_RECT(code, etwFrameNumber, area, keyword)
#define SCOPED_TRACE_EVENT_FLOAT_XYWH(code, etwFrameNumber, x, y, w, h, keyword)
#define SCOPED_TRACE_EVENT_INT_RECT_LIST(code, etwFrameNumber, list, keyword)
#define SCOPED_TRACE_EVENT_FLOAT_RECT_LIST(code, etwFrameNumber, list, keyword)

#endif

#define TRACE_LOG_D2D_PAINT_CALL(code, frameNumber) \
    TraceLoggingWriteWrapper (JUCE_ETW_TRACELOGGING_PROVIDER_HANDLE, \
                   "D2D paint start", \
                   TraceLoggingLevel (TRACE_LEVEL_INFORMATION), \
                   TraceLoggingKeyword (etw::paintKeyword | etw::direct2dKeyword), \
                   TraceLoggingInt32 (frameNumber, "frame"), \
                   TraceLoggingInt32 (code, "code"))

#define TRACE_LOG_D2D_PAINT_START(frameNumber) \
    TraceLoggingWriteWrapper (JUCE_ETW_TRACELOGGING_PROVIDER_HANDLE, \
                   "D2D paint start", \
                   TraceLoggingLevel (TRACE_LEVEL_INFORMATION), \
                   TraceLoggingKeyword (etw::paintKeyword | etw::direct2dKeyword), \
                   TraceLoggingInt32 (frameNumber, "frame"), \
                   TraceLoggingInt32 (etw::direct2dPaintStart, "code"))

#define TRACE_LOG_D2D_PAINT_END(frameNumber) \
    TraceLoggingWriteWrapper (JUCE_ETW_TRACELOGGING_PROVIDER_HANDLE, \
                       "D2D paint end", \
                       TraceLoggingLevel (TRACE_LEVEL_INFORMATION), \
                       TraceLoggingKeyword (etw::paintKeyword | etw::direct2dKeyword), \
                       TraceLoggingInt32 (frameNumber, "frame"),                       \
                       TraceLoggingInt32 (etw::direct2dPaintEnd, "code"))

#define TRACE_LOG_JUCE_VBLANK_THREAD_EVENT                          \
    TraceLoggingWriteWrapper (JUCE_ETW_TRACELOGGING_PROVIDER_HANDLE,       \
                       "VBlankThread WaitForVBlank done",           \
                       TraceLoggingLevel (TRACE_LEVEL_INFORMATION), \
                       TraceLoggingKeyword (etw::softwareRendererKeyword), \
                        TraceLoggingInt32(etw::waitForVBlankDone, "code"))

#define TRACE_LOG_JUCE_VBLANK_CALL_LISTENERS                         \
    TraceLoggingWriteWrapper (JUCE_ETW_TRACELOGGING_PROVIDER_HANDLE,       \
                       "VBlankThread call listeners",           \
                       TraceLoggingLevel (TRACE_LEVEL_INFORMATION), \
                       TraceLoggingKeyword (etw::softwareRendererKeyword), \
                        TraceLoggingInt32(etw::callVBlankListeners, "code"))

#define TRACE_LOG_D2D_RESIZE(message)                                                \
    TraceLoggingWriteWrapper (JUCE_ETW_TRACELOGGING_PROVIDER_HANDLE,                          \
                              "D2D resize",                                              \
                              TraceLoggingLevel (TRACE_LEVEL_INFORMATION),                    \
                              TraceLoggingKeyword (etw::paintKeyword | etw::direct2dKeyword), \
                               TraceLoggingInt32 (message, "message"),\
                              TraceLoggingInt32 (etw::resize, "code"))

#define TRACE_LOG_D2D_IMAGE_MAP_DATA \
    TraceLoggingWriteWrapper (JUCE_ETW_TRACELOGGING_PROVIDER_HANDLE, \
                   "Map bitmap", \
                   TraceLoggingLevel (TRACE_LEVEL_INFORMATION), \
                   TraceLoggingKeyword (etw::direct2dKeyword), \
                   TraceLoggingInt32 (etw::mapBitmap, "code"))

#define TRACE_LOG_D2D_IMAGE_UNMAP_DATA \
    TraceLoggingWriteWrapper (JUCE_ETW_TRACELOGGING_PROVIDER_HANDLE, \
                   "Unmap bitmap", \
                   TraceLoggingLevel (TRACE_LEVEL_INFORMATION), \
                   TraceLoggingKeyword (etw::direct2dKeyword), \
                   TraceLoggingInt32 (etw::unmapBitmap, "code"))
