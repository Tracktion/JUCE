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

#ifdef __INTELLISENSE__

#define JUCE_CORE_INCLUDE_COM_SMART_PTR 1
#define JUCE_WINDOWS                    1

#include <windows.h>
#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>
#include <d2d1_2.h>
#include <d3d11_1.h>
#include <dwrite.h>
#include <dcomp.h>
#include "juce_win32_Direct2DHelpers.cpp"
#include "juce_win32_Direct2DCommandQueue.cpp"

#endif

#define JUCE_DIRECT2D_DIRECT_COMPOSITION 1

namespace juce
{
    namespace direct2d
    {

        //==============================================================================
        //
        // Device context and transform
        //

        struct DeviceContext
        {
            HRESULT createHwndRenderTarget(HWND hwnd)
            {
                HRESULT hr = S_OK;

                if (hwndRenderTarget == nullptr)
                {
                    SharedResourcePointer<DirectX> directX;

                    D2D1_SIZE_U size{ 1, 1 };

                    D2D1_RENDER_TARGET_PROPERTIES renderTargetProps{};
                    renderTargetProps.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
                    renderTargetProps.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;

                    D2D1_HWND_RENDER_TARGET_PROPERTIES hwndRenderTargetProps{};
                    hwndRenderTargetProps.hwnd = hwnd;
                    hwndRenderTargetProps.pixelSize = size;
                    hwndRenderTargetProps.presentOptions = D2D1_PRESENT_OPTIONS_IMMEDIATELY | D2D1_PRESENT_OPTIONS_RETAIN_CONTENTS;
                    hr = directX->direct2D.getFactory()->CreateHwndRenderTarget(&renderTargetProps,
                        &hwndRenderTargetProps,
                        hwndRenderTarget.resetAndGetPointerAddress());
                }

                return hr;
            }

            void resetTransform()
            {
                context->SetTransform(D2D1::IdentityMatrix());
                transform = {};
            }

            //
            // The profiler shows that calling deviceContext->SetTransform is
            // surprisingly expensive. This class only calls SetTransform
            // if the transform is changing
            //
            void setTransform(AffineTransform newTransform)
            {
                if (approximatelyEqual(transform.mat00, newTransform.mat00) && approximatelyEqual(transform.mat01, newTransform.mat01) &&
                    approximatelyEqual(transform.mat02, newTransform.mat02) && approximatelyEqual(transform.mat10, newTransform.mat10) &&
                    approximatelyEqual(transform.mat11, newTransform.mat11) && approximatelyEqual(transform.mat12, newTransform.mat12))
                {
                    return;
                }

                context->SetTransform(transformToMatrix(newTransform));
                transform = newTransform;
            }

            void release()
            {
                hwndRenderTarget = nullptr;
                context = nullptr;
            }

            ComSmartPtr<ID2D1DeviceContext1> context;
            ComSmartPtr<ID2D1HwndRenderTarget> hwndRenderTarget;
            AffineTransform                 transform;
        };

        //==============================================================================
        //
        // Direct2D bitmap
        //

        class Direct2DBitmap
        {
        public:
            static Direct2DBitmap fromImage(Image const& image, ID2D1DeviceContext1* deviceContext, Image::PixelFormat format)
            {
                auto              convertedImage = image.convertedToFormat(format);
                Image::BitmapData bitmapData{ convertedImage, Image::BitmapData::readOnly };

                D2D1_BITMAP_PROPERTIES1 bitmapProperties{};
                bitmapProperties.pixelFormat.format = format == Image::PixelFormat::ARGB ? DXGI_FORMAT_B8G8R8A8_UNORM : DXGI_FORMAT_R8_UNORM;
                bitmapProperties.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;

                D2D1_SIZE_U size = { (UINT32)image.getWidth(), (UINT32)image.getHeight() };

                Direct2DBitmap direct2DBitmap;
                deviceContext->CreateBitmap(size, bitmapData.data, (UINT32) bitmapData.lineStride, bitmapProperties, direct2DBitmap.bitmap.resetAndGetPointerAddress());
                return direct2DBitmap;
            }

            void createBitmap(ID2D1DeviceContext1* deviceContext,
                Image::PixelFormat format,
                D2D_SIZE_U size,
                int lineStride,
                float dpiScaleFactor,
                D2D1_BITMAP_OPTIONS options)
            {
                if (! bitmap)
                {
                    D2D1_BITMAP_PROPERTIES1 bitmapProperties = {};
                    bitmapProperties.dpiX = dpiScaleFactor * USER_DEFAULT_SCREEN_DPI;;
                    bitmapProperties.dpiY = bitmapProperties.dpiX;
                    bitmapProperties.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
                    bitmapProperties.pixelFormat.format =
                        (format == Image::SingleChannel) ? DXGI_FORMAT_A8_UNORM : DXGI_FORMAT_B8G8R8A8_UNORM;
                    bitmapProperties.bitmapOptions = options;

                    deviceContext->CreateBitmap(
                        size,
                        nullptr,
                        (UINT32) lineStride,
                        bitmapProperties,
                        bitmap.resetAndGetPointerAddress());
                }
            }

            void set(ID2D1Bitmap1* bitmap_)
            {
                bitmap = bitmap_;
            }

            ID2D1Bitmap1* get() const noexcept
            {
                return bitmap;
            }

            void release()
            {
                bitmap = nullptr;
            }

        protected:
            ComSmartPtr<ID2D1Bitmap1> bitmap;
        };


        //==============================================================================
        //
        // Geometry caching
        //

        class GeometryCache
        {
        public:
            virtual ~GeometryCache()
            {
                release();
            }

            void release()
            {
                hashMap.clear();
            }

#if JUCE_DIRECT2D_METRICS
            StatisticsAccumulator<double>* createGeometryMsecStats = nullptr;
            StatisticsAccumulator<double>* createGeometryRealisationMsecStats = nullptr;
#endif

        protected:

            static float findGeometryFlatteningTolerance(float scaleFactor)
            {
                //
                // Could use D2D1::ComputeFlatteningTolerance here, but that requires defining NTDDI_VERSION and it doesn't do anything special.
                //
                // Direct2D default flattening tolerance is 0.25
                //
                return 0.25f / scaleFactor;
            }

            //==============================================================================
            //
            // Hashing
            //
            static constexpr auto fnvOffsetBasis = 0xcbf29ce484222325;
            static constexpr auto fnvPrime = 0x100000001b3;

            static constexpr uint64 fnv1aHash(uint8 const* data, size_t numBytes, uint64 hash = fnvOffsetBasis)
            {
                while (numBytes > 0)
                {
                    hash = (hash ^ *data++) * fnvPrime;
                    --numBytes;
                }

                return hash;
            }

            //==============================================================================
            //
            // Caching
            //
            struct CachedGeometryRealisation : public ReferenceCountedObject
            {
                CachedGeometryRealisation(uint64 hash_) :
                    hash(hash_)
                {
                }

                CachedGeometryRealisation(CachedGeometryRealisation const& other) :
                    hash(other.hash),
                    pathModificationCount(other.pathModificationCount),
                    geometryRealisation(other.geometryRealisation)
                {
                }

                CachedGeometryRealisation(CachedGeometryRealisation&& other)  noexcept :
                    hash(other.hash),
                    pathModificationCount(other.pathModificationCount),
                    geometryRealisation(other.geometryRealisation)
                {
                }

                ~CachedGeometryRealisation()
                {
                    clear();
                }

                void clear()
                {
                    hash = 0;
                    geometryRealisation = nullptr;
                }

                uint64 hash = 0;
                int pathModificationCount = 0;
                ComSmartPtr<ID2D1GeometryRealization> geometryRealisation;

                using Ptr = ReferenceCountedObjectPtr<CachedGeometryRealisation>;
            };

            class HashMap
            {
            public:
                ~HashMap()
                {
                    clear();
                }

                void clear()
                {
                    lruCache.clear();
                }

                auto size() const noexcept
                {
                    return lruCache.size();
                }

                CachedGeometryRealisation::Ptr getCachedGeometryRealisation(uint64 hash)
                {
                    trim();

                    if (auto entry = lruCache.get(hash))
                    {
                        return entry;
                    }

                    CachedGeometryRealisation::Ptr cachedGeometryRealisation = new CachedGeometryRealisation{ hash };
                    lruCache.set(hash, cachedGeometryRealisation);
                    return cachedGeometryRealisation;
                }

                void trim()
                {
                    //
                    // Remove any expired entries
                    //
                    while (lruCache.size() > maxNumCacheEntries)
                    {
                        lruCache.popBack();
                    }
                }

            private:
                static int constexpr maxNumCacheEntries = 128;

                direct2d::LeastRecentlyUsedCache<uint64, CachedGeometryRealisation::Ptr> lruCache;
            } hashMap;
        };

        class FilledGeometryCache : public GeometryCache
        {
        public:
            ~FilledGeometryCache() override = default;

            ID2D1GeometryRealization* getGeometryRealisation(const Path& path,
                ID2D1Factory2* factory,
                ID2D1DeviceContext1* deviceContext,
                float dpiScaleFactor)
            {
                if (path.getModificationCount() == 0 || !path.isCacheEnabled() || !path.shouldBeCached())
                {
                    return nullptr;
                }

                auto flatteningTolerance = findGeometryFlatteningTolerance(dpiScaleFactor);
                auto hash = calculatePathHash(path, flatteningTolerance);

                if (auto cachedGeometry = hashMap.getCachedGeometryRealisation(hash))
                {
                    if (cachedGeometry->geometryRealisation && cachedGeometry->pathModificationCount != path.getModificationCount())
                    {
                        cachedGeometry->geometryRealisation = nullptr;
                        cachedGeometry->pathModificationCount = 0;
                    }

                    if (!cachedGeometry->geometryRealisation)
                    {
#if JUCE_DIRECT2D_METRICS
                        auto t1 = Time::getHighResolutionTicks();
#endif
                        if (auto geometry = direct2d::pathToPathGeometry(factory, path, D2D1_FIGURE_BEGIN_FILLED))
                        {
#if JUCE_DIRECT2D_METRICS
                            auto t2 = Time::getHighResolutionTicks();
#endif

                            auto hr = deviceContext->CreateFilledGeometryRealization(geometry, flatteningTolerance, cachedGeometry->geometryRealisation.resetAndGetPointerAddress());

#if JUCE_DIRECT2D_METRICS
                            auto t3 = Time::getHighResolutionTicks();

                            if (createGeometryMsecStats) createGeometryMsecStats->addValue(Time::highResolutionTicksToSeconds(t2 - t1) * 1000.0);
                            if (createGeometryRealisationMsecStats) createGeometryRealisationMsecStats->addValue(Time::highResolutionTicksToSeconds(t3 - t2) * 1000.0);
#endif

                            switch (hr)
                            {
                            case S_OK:
                                cachedGeometry->pathModificationCount = path.getModificationCount();
                                break;

                            case E_OUTOFMEMORY:
                                return nullptr;
                            }
                        }
                    }

                    return cachedGeometry->geometryRealisation;
                }

                return nullptr;
            }

        private:

            //==============================================================================
            //
            // Hashing
            //

            uint64 calculatePathHash(Path const& path, float flatteningTolerance)
            {
                return fnv1aHash(reinterpret_cast<uint8 const*>(&flatteningTolerance), sizeof(flatteningTolerance), path.getUniqueID());
            }

        };

        class StrokeGeometryCache : public GeometryCache
        {
        public:
            ~StrokeGeometryCache() override = default;

            ID2D1GeometryRealization* getGeometryRealisation(const Path& path,
                const PathStrokeType& strokeType,
                ID2D1Factory2* factory,
                ID2D1DeviceContext1* deviceContext,
                float transformScaleFactor,
                float dpiScaleFactor)
            {
                if (path.getModificationCount() == 0 || !path.isCacheEnabled() || !path.shouldBeCached())
                {
                    return nullptr;
                }

                auto flatteningTolerance = findGeometryFlatteningTolerance(dpiScaleFactor * transformScaleFactor);
                auto hash = calculatePathHash(path, strokeType, flatteningTolerance, transformScaleFactor);

                if (auto cachedGeometry = hashMap.getCachedGeometryRealisation(hash))
                {
                    if (cachedGeometry->geometryRealisation && cachedGeometry->pathModificationCount != path.getModificationCount())
                    {
                        cachedGeometry->geometryRealisation = nullptr;
                        cachedGeometry->pathModificationCount = 0;
                    }

                    if (!cachedGeometry->geometryRealisation)
                    {
#if JUCE_DIRECT2D_METRICS
                        auto t1 = Time::getHighResolutionTicks();
#endif
                        if (auto geometry = direct2d::pathToPathGeometry(factory, path, D2D1_FIGURE_BEGIN_HOLLOW))
                        {
#if JUCE_DIRECT2D_METRICS
                            auto t2 = Time::getHighResolutionTicks();
#endif
                            if (auto strokeStyle = direct2d::pathStrokeTypeToStrokeStyle(factory, strokeType))
                            {
                                //
                                // For stroked paths, the transform will affect the thickness of the path as well
                                // as the dimensions of the path. Divide the stroke thickness by the scale factor
                                // of the transform to compensate
                                //
                                auto hr = deviceContext->CreateStrokedGeometryRealization(geometry,
                                    flatteningTolerance,
                                    strokeType.getStrokeThickness() / transformScaleFactor,
                                    strokeStyle,
                                    cachedGeometry->geometryRealisation.resetAndGetPointerAddress());

#if JUCE_DIRECT2D_METRICS
                                auto t3 = Time::getHighResolutionTicks();

                                if (createGeometryMsecStats) createGeometryMsecStats->addValue(Time::highResolutionTicksToSeconds(t2 - t1) * 1000.0);
                                if (createGeometryRealisationMsecStats) createGeometryRealisationMsecStats->addValue(Time::highResolutionTicksToSeconds(t3 - t2) * 1000.0);
#endif

                                switch (hr)
                                {
                                case S_OK:
                                    cachedGeometry->pathModificationCount = path.getModificationCount();
                                    break;

                                case E_OUTOFMEMORY:
                                    return nullptr;
                                }
                            }
                        }
                    }

                    return cachedGeometry->geometryRealisation;
                }

                return nullptr;
            }

        private:

            uint64 calculatePathHash(Path const& path, PathStrokeType const& strokeType, float flatteningTolerance, float transformScaleFactor)
            {
                struct
                {
                    float transformScaleFactor, flatteningTolerance, strokeThickness;
                    int8 jointStyle, endStyle;
                } extraHashData;

                extraHashData.transformScaleFactor = transformScaleFactor;
                extraHashData.flatteningTolerance = flatteningTolerance;
                extraHashData.strokeThickness = strokeType.getStrokeThickness();
                extraHashData.jointStyle = (int8)strokeType.getJointStyle();
                extraHashData.endStyle = (int8)strokeType.getEndStyle();

                return fnv1aHash(reinterpret_cast<uint8 const*>(&extraHashData), sizeof(extraHashData), path.getUniqueID());
            }
        };


        //==============================================================================
        //
        // Device resources
        //

        class DeviceResources
        {
        public:
            DeviceResources() = default;

            ~DeviceResources()
            {
                release();
            }

            //
            // Create a Direct2D device context for a DXGI adapter
            //
            HRESULT create(DirectX::DXGI::Adapter::Ptr adapter, double dpiScalingFactor)
            {
                HRESULT hr = S_OK;

                jassert(adapter);

                if (adapter->direct2DDevice == nullptr)
                {
                    SharedResourcePointer<DirectX> directX;
                    if (hr = adapter->createDirect2DResources(directX->direct2D.getFactory()); FAILED(hr)) return hr;
                }

                if (deviceContext.context == nullptr)
                    {
                        hr = adapter->direct2DDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE,
                            deviceContext.context.resetAndGetPointerAddress());
                    if (FAILED(hr)) return hr;
                    }

                        deviceContext.context->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);

                        float dpi = (float)(USER_DEFAULT_SCREEN_DPI * dpiScalingFactor);
                        deviceContext.context->SetDpi(dpi, dpi);

                        if (colourBrush == nullptr)
                        {
                            hr = deviceContext.context->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 1.0f),
                                colourBrush.resetAndGetPointerAddress());
                            jassertquiet(SUCCEEDED(hr));
                        }

                return hr;
            }

            void release()
            {
                filledGeometryCache.release();
                strokedGeometryCache.release();
                colourBrush = nullptr;
                deviceContext.release();
            }

            bool canPaint(DirectX::DXGI::Adapter::Ptr adapter)
            {
                return adapter->direct2DDevice != nullptr && deviceContext.context != nullptr && colourBrush != nullptr;
            }

            DeviceContext                     deviceContext;
            ComSmartPtr<ID2D1SolidColorBrush> colourBrush;
            FilledGeometryCache               filledGeometryCache;
            StrokeGeometryCache               strokedGeometryCache;
        };

        //==============================================================================
        //
        // Swap chain
        //

        class SwapChain
        {
        public:
            SwapChain() = default;

            ~SwapChain()
            {
                release();
            }

            HRESULT create(HWND hwnd, Rectangle<int> size, DirectX::DXGI::Adapter::Ptr adapter)
            {
                if (!chain && hwnd)
                {
                    SharedResourcePointer<DirectX> directX;
                    auto dxgiFactory = directX->dxgi.getFactory();

                    if (dxgiFactory == nullptr || adapter->direct3DDevice == nullptr)
                    {
                        return E_FAIL;
                    }

                    HRESULT hr = S_OK;

                    buffer = nullptr;
                    chain = nullptr;

                    //
                    // Make the waitable swap chain
                    //
                    // Create the swap chain with premultiplied alpha support for transparent windows
                    //
                    DXGI_SWAP_CHAIN_DESC1 swapChainDescription = {};
                    swapChainDescription.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
                    swapChainDescription.Width = (UINT) size.getWidth();
                    swapChainDescription.Height = (UINT) size.getHeight();
                    swapChainDescription.SampleDesc.Count = 1;
                    swapChainDescription.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
                    swapChainDescription.BufferCount = bufferCount;
                    swapChainDescription.SwapEffect = swapEffect;
                    swapChainDescription.Flags = swapChainFlags;

                    swapChainDescription.Scaling = DXGI_SCALING_STRETCH;
                    swapChainDescription.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
                    hr = dxgiFactory->CreateSwapChainForComposition(adapter->direct3DDevice,
                        &swapChainDescription,
                        nullptr,
                        chain.resetAndGetPointerAddress());
                    jassert(SUCCEEDED(hr));

                    std::optional<ScopedEvent> swapChainEvent;

                    if (SUCCEEDED(hr))
                    {
                        TRACE_LOG_D2D_CREATE_RESOURCE("swapchain");

                        //
                        // Get the waitable swap chain presentation event and set the maximum frame latency
                        //
                        ComSmartPtr<IDXGISwapChain2> chain2;
                        chain.QueryInterface<IDXGISwapChain2>(chain2);
                        if (chain2)
                        {
                            swapChainEvent.emplace(chain2->GetFrameLatencyWaitableObject());
                            if (swapChainEvent->getHandle() == INVALID_HANDLE_VALUE)
                                return E_NOINTERFACE;

                            hr = chain2->SetMaximumFrameLatency(2);
                            if (SUCCEEDED(hr))
                            {
                                state = State::chainAllocated;

                                TRACE_LOG_D2D_RESOURCE(etw::createSwapChain);
                            }
                        }
                    }
                    else
                    {
                        return E_NOINTERFACE;
                    }

                    if (swapChainEvent.has_value() && swapChainEvent->getHandle() != nullptr)
                        swapChainDispatcher.emplace(std::move(*swapChainEvent));

                    return hr;
                }

                return S_OK;
            }

            HRESULT createBuffer(ID2D1DeviceContext* const deviceContext)
            {
                if (deviceContext && chain && !buffer)
                {
                    ComSmartPtr<IDXGISurface> surface;
                    JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE("-Wlanguage-extension-token")
                    auto hr = chain->GetBuffer(0, __uuidof (surface), reinterpret_cast<void**> (surface.resetAndGetPointerAddress()));
                    JUCE_END_IGNORE_WARNINGS_GCC_LIKE
                    if (SUCCEEDED(hr))
                    {
                        D2D1_BITMAP_PROPERTIES1 bitmapProperties = {};
                        bitmapProperties.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
                        bitmapProperties.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
                        bitmapProperties.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;

                        hr = deviceContext->CreateBitmapFromDxgiSurface(surface, bitmapProperties, buffer.resetAndGetPointerAddress());
                        jassert(SUCCEEDED(hr));

                        if (SUCCEEDED(hr))
                        {
                            TRACE_LOG_D2D_RESOURCE(etw::createSwapChainBuffer);

                            state = State::bufferAllocated;
                        }
                    }

                    return hr;
                }

                return S_OK;
            }

            void release()
            {
                swapChainDispatcher.reset();
                buffer = nullptr;
                chain = nullptr;
                state = State::idle;
            }

            bool canPaint()
            {
                return chain != nullptr && buffer != nullptr && state >= State::bufferAllocated;
            }

            HRESULT resize(Rectangle<int> newSize, float dpiScalingFactor, ID2D1DeviceContext* const deviceContext)
            {
                if (chain)
                {
                    auto scaledSize = newSize * dpiScalingFactor;
                    scaledSize =
                        scaledSize.getUnion({ Direct2DGraphicsContext::minFrameSize, Direct2DGraphicsContext::minFrameSize })
                        .getIntersection({ Direct2DGraphicsContext::maxFrameSize, Direct2DGraphicsContext::maxFrameSize });

                    buffer = nullptr;
                    state = State::chainAllocated;

                    auto dpi = USER_DEFAULT_SCREEN_DPI * dpiScalingFactor;
                    deviceContext->SetDpi(dpi, dpi);

                    auto hr = chain->ResizeBuffers(0, (UINT) scaledSize.getWidth(), (UINT) scaledSize.getHeight(), DXGI_FORMAT_B8G8R8A8_UNORM, swapChainFlags);
                    if (SUCCEEDED(hr))
                    {
                        hr = createBuffer(deviceContext);
                    }

                    if (FAILED(hr))
                    {
                        release();
                    }

                    return hr;
                }

                return E_FAIL;
            }

            Rectangle<int> getSize() const
            {
                if (buffer)
                {
                    auto size = buffer->GetPixelSize();
                    return { (int)size.width, (int)size.height };
                }

                return {};
            }

            DXGI_SWAP_EFFECT const                     swapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
            UINT const                                 bufferCount = 2;
            uint32 const                               swapChainFlags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
            uint32 const                               presentSyncInterval = 1;
            uint32 const                               presentFlags = 0;
            ComSmartPtr<IDXGISwapChain1>               chain;
            ComSmartPtr<ID2D1Bitmap1>                  buffer;

            std::optional<SwapChainDispatcher> swapChainDispatcher;

            enum class State
            {
                idle,
                chainAllocated,
                bufferAllocated,
                bufferFilled
            };
            State state = State::idle;
        };

        //==============================================================================
        //
        // DirectComposition
        //
        // Using DirectComposition enables transparent windows and smoother window
        // resizing
        //
        // This class builds a simple DirectComposition tree that ultimately contains
        // the swap chain
        //

#define JUCE_EARLY_EXIT(expr) \
    JUCE_BLOCK_WITH_FORCED_SEMICOLON ( if (const auto hr = (expr); ! SUCCEEDED (hr)) return hr; )

        class CompositionTree
        {
        public:
            HRESULT create(IDXGIDevice* const dxgiDevice, HWND hwnd, IDXGISwapChain1* const swapChain)
            {
                if (compositionDevice != nullptr)
                    return S_OK;

                if (dxgiDevice == nullptr)
                    return S_FALSE;

                JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE("-Wlanguage-extension-token")
                JUCE_EARLY_EXIT(DCompositionCreateDevice(dxgiDevice,
                    __uuidof (IDCompositionDevice),
                    reinterpret_cast<void**> (compositionDevice.resetAndGetPointerAddress())));
                JUCE_END_IGNORE_WARNINGS_GCC_LIKE
                JUCE_EARLY_EXIT(compositionDevice->CreateTargetForHwnd(hwnd, FALSE, compositionTarget.resetAndGetPointerAddress()));
                JUCE_EARLY_EXIT(compositionDevice->CreateVisual(compositionVisual.resetAndGetPointerAddress()));
                JUCE_EARLY_EXIT(compositionTarget->SetRoot(compositionVisual));
                JUCE_EARLY_EXIT(compositionVisual->SetContent(swapChain));
                JUCE_EARLY_EXIT(compositionDevice->Commit());
                return S_OK;
            }

            void release()
            {
                compositionVisual = nullptr;
                compositionTarget = nullptr;
                compositionDevice = nullptr;
            }

            bool canPaint() const
            {
                return compositionVisual != nullptr;
            }

        private:
            ComSmartPtr<IDCompositionDevice> compositionDevice;
            ComSmartPtr<IDCompositionTarget> compositionTarget;
            ComSmartPtr<IDCompositionVisual> compositionVisual;
        };

#undef JUCE_EARLY_EXIT

    } // namespace direct2d

} // namespace juce
