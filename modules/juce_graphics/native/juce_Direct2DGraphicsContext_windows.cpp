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

/*

    don't mix DXGI factory types

    get rid of CS_OWNDC?

    -child window clipping?

    -minimize calls to SetTransform
    -text analyzer?
    -recycle state structs
    -don't paint occluded windows
    -Multithreaded device context?
    -reusable geometry for exclude clip rectangle

    handle device context creation error / paint errors
        watchdog timer?

    OK EndDraw D2DERR_RECREATE_TARGET
    OK JUCE 7.0.6 merge
    OK when to start threads in general
    OK use std::stack for layers
    OK Check use of InvalidateRect & ValidateRect
    OK drawGlyphUnderline
    OK DPI scaling
    OK start/stop thread when window is visible
    OK logo highlights in juce animation demo
    OK check resize when auto-arranging windows
    OK single-channel bitmap for clip to image alpha
    OK transparency layer in software mode?
    OK check for empty dirty rectangles
    OK vblank in software mode
    OK fix ScopedBrushTransformInverter
    OK vblank attachment
    OK Always present

    WM_DISPLAYCHANGE / WM_SETTINGCHANGE rebuild resources

    */

#ifdef __INTELLISENSE__

#define JUCE_CORE_INCLUDE_COM_SMART_PTR 1
#define JUCE_WINDOWS                    1

#include <d2d1_2.h>
#include <d3d11_1.h>
#include <dcomp.h>
#include <dwrite.h>
#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>
#include <windows.h>
#include "juce_ETW_windows.h"

#endif

namespace juce
{

    struct Direct2DGraphicsContext::SavedState
    {
    private:
        //==============================================================================
        //
        // PushedLayer represents a Direct2D clipping or transparency layer
        //
        // D2D layers have to be pushed into the device context. Every push has to be
        // matched with a pop.
        //
        // D2D has special layers called "axis aligned clip layers" which clip to an
        // axis-aligned rectangle. Pushing an axis-aligned clip layer must be matched
        // with a call to deviceContext->PopAxisAlignedClip() in the reverse order
        // in which the layers were pushed.
        //
        // So if the pushed layer stack is built like this:
        //
        // PushLayer()
        // PushLayer()
        // PushAxisAlignedClip()
        // PushLayer()
        //
        // the layer stack must be popped like this:
        //
        // PopLayer()
        // PopAxisAlignedClip()
        // PopLayer()
        // PopLayer()
        //
        // PushedLayer, PushedAxisAlignedClipLayer, and LayerPopper all exist just to unwind the
        // layer stack accordingly.
        //
        using PopLayer = void (*) (ID2D1DeviceContext*);
        std::stack<PopLayer> pushedLayers;

    public:
        //
        // Constructor for first stack entry
        //
        SavedState(Rectangle<int> frameSize_, ComSmartPtr<ID2D1SolidColorBrush>& colourBrush_, direct2d::DeviceContext& deviceContext_)
            : deviceContext(deviceContext_),
            clipRegion(frameSize_),
            colourBrush(colourBrush_)
        {
            currentBrush = colourBrush;
        }

        //
        // Constructor for subsequent entries
        //
        SavedState(SavedState const* const previousState_)
            : currentTransform(previousState_->currentTransform),
            deviceContext(previousState_->deviceContext),
            clipRegion(previousState_->clipRegion),
            font(previousState_->font),
            currentBrush(previousState_->currentBrush),
            colourBrush(previousState_->colourBrush),
            bitmapBrush(previousState_->bitmapBrush),
            linearGradient(previousState_->linearGradient),
            radialGradient(previousState_->radialGradient),
            gradientStops(previousState_->gradientStops),
            fillType(previousState_->fillType),
            interpolationMode(previousState_->interpolationMode)
        {
        }

        ~SavedState()
        {
            jassert(pushedLayers.empty());
            clearFill();
        }

        void pushLayer(const D2D1_LAYER_PARAMETERS& layerParameters)
        {
            //
            // Clipping and transparency are all handled by pushing Direct2D layers. The SavedState creates an internal stack
            // of Layer objects to keep track of how many layers need to be popped.
            //
            // Pass nullptr for the PushLayer layer parameter to allow Direct2D to manage the layers (Windows 8 or later)
            //
            deviceContext.resetTransform();
            deviceContext.context->PushLayer(layerParameters, nullptr);

            pushedLayers.push ([] (ID2D1DeviceContext* ctx) { ctx->PopLayer(); });
        }

        void pushGeometryClipLayer(ComSmartPtr<ID2D1Geometry> geometry)
        {
            if (geometry != nullptr)
            {
                pushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), geometry));
            }
        }

        void pushTransformedRectangleGeometryClipLayer(ComSmartPtr<ID2D1RectangleGeometry> geometry, AffineTransform const& transform)
        {
            jassert(geometry != nullptr);
            auto layerParameters = D2D1::LayerParameters(D2D1::InfiniteRect(), geometry);
            layerParameters.maskTransform = direct2d::transformToMatrix(transform);
            pushLayer(layerParameters);
        }

        void pushAxisAlignedClipLayer(Rectangle<int> r)
        {
            deviceContext.setTransform(currentTransform.getTransform());
            deviceContext.context->PushAxisAlignedClip(direct2d::rectangleToRectF(r), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

            pushedLayers.push ([] (ID2D1DeviceContext* ctx) { ctx->PopAxisAlignedClip(); });
        }

        void pushTransparencyLayer(float opacity)
        {
            pushLayer({ D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(), opacity, {}, {} });
        }

        void popLayers()
        {
            while (! pushedLayers.empty())
                popTopLayer();
        }

        void popTopLayer()
        {
            if (! pushedLayers.empty())
            {
                pushedLayers.top() (deviceContext.context);
                pushedLayers.pop();
            }
        }

        void setFont(const Font& newFont)
        {
            font = newFont;
        }

        void setOpacity(float newOpacity)
        {
            fillType.setOpacity(newOpacity);
        }

        void clearFill()
        {
            gradientStops = nullptr;
            linearGradient = nullptr;
            radialGradient = nullptr;
            bitmapBrush = nullptr;
            currentBrush = nullptr;
        }

        //
        // Translate a JUCE FillType to a Direct2D brush
        //
        void updateCurrentBrush()
        {
            if (fillType.isColour())
            {
                //
                // Reuse the same colour brush
                //
                currentBrush = (ID2D1Brush*)colourBrush;
            }
            else if (fillType.isTiledImage())
            {
                if (fillType.image.isNull())
                {
                    return;
                }

                auto direct2DBitmap = direct2d::Direct2DBitmap::fromImage(fillType.image, deviceContext.context, Image::ARGB);
                if (auto bitmap = direct2DBitmap.get())
                {
                    D2D1_BRUSH_PROPERTIES brushProps = { fillType.getOpacity(), direct2d::transformToMatrix(fillType.transform) };
                    auto                  bmProps = D2D1::BitmapBrushProperties(D2D1_EXTEND_MODE_WRAP, D2D1_EXTEND_MODE_WRAP);
                    auto hr = deviceContext.context->CreateBitmapBrush(bitmap,
                        bmProps,
                        brushProps,
                        bitmapBrush.resetAndGetPointerAddress());
                    jassert(SUCCEEDED(hr));
                    if (SUCCEEDED(hr))
                    {
                        currentBrush = bitmapBrush;
                    }
                }
            }
            else if (fillType.isGradient())
            {
                D2D1_BRUSH_PROPERTIES brushProps = { fillType.getOpacity(), direct2d::transformToMatrix(fillType.transform) };
                const int             numColors = fillType.gradient->getNumColours();

                HeapBlock<D2D1_GRADIENT_STOP> stops(numColors);

                for (int i = fillType.gradient->getNumColours(); --i >= 0;)
                {
                    stops[i].color = direct2d::colourToD2D(fillType.gradient->getColour(i));
                    stops[i].position = (FLOAT)fillType.gradient->getColourPosition(i);
                }

                deviceContext.context->CreateGradientStopCollection(stops.getData(), (UINT32) numColors, gradientStops.resetAndGetPointerAddress());

                if (fillType.gradient->isRadial)
                {
                    const auto p1 = fillType.gradient->point1;
                    const auto p2 = fillType.gradient->point2;
                    const auto r = p1.getDistanceFrom(p2);
                    const auto props = D2D1::RadialGradientBrushProperties({ p1.x, p1.y }, {}, r, r);

                    deviceContext.context->CreateRadialGradientBrush(props,
                        brushProps,
                        gradientStops,
                        radialGradient.resetAndGetPointerAddress());
                    currentBrush = radialGradient;
                }
                else
                {
                    const auto p1 = fillType.gradient->point1;
                    const auto p2 = fillType.gradient->point2;
                    const auto props = D2D1::LinearGradientBrushProperties({ p1.x, p1.y }, { p2.x, p2.y });

                    deviceContext.context->CreateLinearGradientBrush(props,
                        brushProps,
                        gradientStops,
                        linearGradient.resetAndGetPointerAddress());

                    currentBrush = linearGradient;
                }
            }

            updateColourBrush();
        }

        void updateColourBrush()
        {
            if (colourBrush && fillType.isColour())
            {
                auto colour = direct2d::colourToD2D(fillType.colour);
                colourBrush->SetColor(colour);
            }
        }

        bool isCurrentBrushUsable() const noexcept
        {
            return currentBrush != nullptr && ! fillType.isInvisible();
        }

        struct TranslationOrTransform : public RenderingHelpers::TranslationOrTransform
        {
            bool isAxisAligned() const noexcept
            {
                return isOnlyTranslated || (complexTransform.mat01 == 0.0f && complexTransform.mat10 == 0.0f);
            }
        } currentTransform;
        direct2d::DeviceContext& deviceContext;
        Rectangle<int>           clipRegion;

        Font font;

        ID2D1Brush* currentBrush = nullptr;
        ComSmartPtr<ID2D1SolidColorBrush>& colourBrush; // reference to shared colour brush
        ComSmartPtr<ID2D1BitmapBrush>            bitmapBrush;
        ComSmartPtr<ID2D1LinearGradientBrush>    linearGradient;
        ComSmartPtr<ID2D1RadialGradientBrush>    radialGradient;
        ComSmartPtr<ID2D1GradientStopCollection> gradientStops;

        FillType fillType;

        D2D1_INTERPOLATION_MODE interpolationMode = D2D1_INTERPOLATION_MODE_LINEAR;

        //
        // Bitmap & gradient brushes are position-dependent and are therefore affected by transforms
        //
        // Drawing text affects the world transform, so those brushes need an inverse transform to undo the world transform
        //
        struct ScopedBrushTransformInverter
        {
            ScopedBrushTransformInverter(SavedState const* const state_, AffineTransform const& transformToInvert_)
                : state(state_)
            {
                //
                // Set the brush transform if the current brush is not the solid color brush
                //
                if (state_->currentBrush && state_->currentBrush != state_->colourBrush)
                {
                    state_->currentBrush->SetTransform(direct2d::transformToMatrix(transformToInvert_.inverted()));
                    resetTransform = true;
                }
            }

            ~ScopedBrushTransformInverter()
            {
                if (resetTransform)
                {
                    state->currentBrush->SetTransform(D2D1::IdentityMatrix());
                }
            }

            SavedState const* const state;
            bool                    resetTransform = false;
        };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SavedState)
    };

    //==============================================================================

    struct Direct2DGraphicsContext::Pimpl : public DirectX::DXGI::AdapterListener
    {
    protected:
        Direct2DGraphicsContext& owner;
        SharedResourcePointer<DirectX> directX;
        float                                   dpiScalingFactor = 1.0f;
        juce::RectangleList<int> paintAreas;

        DirectX::DXGI::Adapter::Ptr adapter;
        direct2d::DeviceResources              deviceResources;

        std::stack<std::unique_ptr<Direct2DGraphicsContext::SavedState>> savedClientStates;

        virtual HRESULT prepare()
        {
            if (!deviceResources.canPaint(adapter))
            {
                if (auto hr = deviceResources.create(adapter, dpiScalingFactor); FAILED(hr))
                {
                    return hr;
                }
            }

            return S_OK;
        }

        virtual void teardown()
        {
            deviceResources.release();
        }

        virtual Rectangle<int> getFrameSize() = 0;
        virtual ID2D1Image* getDeviceContextTarget() = 0;

        virtual void updatePaintAreas() = 0;

        virtual bool checkPaintReady()
        {
            return deviceResources.canPaint(adapter);
        }

    public:
        Pimpl(Direct2DGraphicsContext& owner_, bool opaque_)
            : owner(owner_), opaque(opaque_)
        {
            setTargetAlpha(1.0f);

            D2D1_RECT_F rect{ 0.0f, 0.0f, 1.0f, 1.0f };
            directX->direct2D.getFactory()->CreateRectangleGeometry(rect, rectangleGeometryUnitSize.resetAndGetPointerAddress());

            directX->dxgi.adapters.listeners.add(this);

#if JUCE_DIRECT2D_METRICS
            deviceResources.filledGeometryCache.createGeometryMsecStats = &owner.paintStats->getAccumulator(direct2d::PaintStats::createGeometryTime);
            deviceResources.filledGeometryCache.createGeometryRealisationMsecStats = &owner.paintStats->getAccumulator(direct2d::PaintStats::createFilledGRTime);
            deviceResources.strokedGeometryCache.createGeometryMsecStats = &owner.paintStats->getAccumulator(direct2d::PaintStats::createGeometryTime);
            deviceResources.strokedGeometryCache.createGeometryRealisationMsecStats = &owner.paintStats->getAccumulator(direct2d::PaintStats::createStrokedGRTime);
#endif
        }

        virtual ~Pimpl() override
        {
            directX->dxgi.adapters.listeners.remove(this);

            popAllSavedStates();

            teardown();
        }

        void setTargetAlpha(float alpha)
        {
            backgroundColor = direct2d::colourToD2D(Colours::black.withAlpha(opaque ? targetAlpha : 0.0f));
            targetAlpha = alpha;
        }

        virtual void clearBackground()
        {
            deviceResources.deviceContext.context->Clear(backgroundColor);
        }

        virtual SavedState* startFrame()
        {
            prepare();

            //
            // Anything to paint?
            //
            updatePaintAreas();
            auto paintBounds = paintAreas.getBounds();
            if (!getFrameSize().intersects(paintBounds) || paintBounds.isEmpty())
            {
                return nullptr;
            }

            //
            // Is Direct2D ready to paint?
            //
            if (!checkPaintReady())
            {
                return nullptr;
            }

            //
            // Init device context transform
            //
            deviceResources.deviceContext.resetTransform();

            //
            // Start drawing
            //
            deviceResources.deviceContext.context->SetTarget(getDeviceContextTarget());
            deviceResources.deviceContext.context->BeginDraw();

            //
            // Init the save state stack and return the first saved state
            //
            return pushFirstSavedState(paintBounds);
        }

        virtual HRESULT finishFrame()
        {
            //
            // Fully pop the state stack
            //
            popAllSavedStates();

            //
            // Finish drawing
            //
            // SetTarget(nullptr) so the device context doesn't hold a reference to the swap chain buffer
            //
            auto hr = deviceResources.deviceContext.context->EndDraw();
            deviceResources.deviceContext.context->SetTarget(nullptr);

            jassert(SUCCEEDED(hr));

            if (FAILED(hr))
            {
                teardown();
            }

            return hr;
        }

        virtual void setScaleFactor(float scale_)
        {
            dpiScalingFactor = scale_;
        }

        float getScaleFactor() const
        {
            return dpiScalingFactor;
        }

        SavedState* getCurrentSavedState() const
        {
            return savedClientStates.size() > 0 ? savedClientStates.top().get() : nullptr;
        }

        SavedState* pushFirstSavedState(Rectangle<int> initialClipRegion)
        {
            jassert(savedClientStates.size() == 0);

            savedClientStates.push(
                std::make_unique<SavedState>(initialClipRegion, deviceResources.colourBrush, deviceResources.deviceContext));

            return getCurrentSavedState();
        }

        SavedState* pushSavedState()
        {
            jassert(savedClientStates.size() > 0);

            savedClientStates.push(std::make_unique<SavedState>(savedClientStates.top().get()));

            return getCurrentSavedState();
        }

        SavedState* popSavedState()
        {
            savedClientStates.top()->popLayers();
            savedClientStates.pop();

            return getCurrentSavedState();
        }

        void popAllSavedStates()
        {
            while (savedClientStates.size() > 0)
            {
                popSavedState();
            }
        }

        DirectX::DXGI::Adapter& getAdapter() const noexcept
        {
            return *adapter;
        }

        inline ID2D1DeviceContext1* getDeviceContext() const noexcept
        {
            return deviceResources.deviceContext.context;
        }

        auto const& getPaintAreas() const noexcept
        {
            return paintAreas;
        }

        void setDeviceContextTransform(AffineTransform transform)
        {
            deviceResources.deviceContext.setTransform(transform);
        }

        auto getDirect2DFactory()
        {
            return directX->direct2D.getFactory();
        }

        auto getDirectWriteFactory()
        {
            return directX->directWrite.getFactory();
        }

        auto getSystemFonts()
        {
            return directX->directWrite.getSystemFonts();
        }

        auto& getFilledGeometryCache()
        {
            return deviceResources.filledGeometryCache;
        }

        auto& getStrokedGeometryCache()
        {
            return deviceResources.strokedGeometryCache;
        }

        void adapterCreated(DirectX::DXGI::Adapter::Ptr newAdapter) override
        {
            if (!adapter || adapter->uniqueIDMatches(newAdapter))
            {
                teardown();

                adapter = newAdapter;
            }
        }

        void adapterRemoved(DirectX::DXGI::Adapter::Ptr expiringAdapter) override
        {
            if (adapter && adapter->uniqueIDMatches(expiringAdapter))
            {
                teardown();

                adapter = nullptr;
            }
        }

        ComSmartPtr<ID2D1RectangleGeometry> rectangleGeometryUnitSize;
        direct2d::DirectWriteGlyphRun       glyphRun;
        bool                                opaque = true;
        float                               targetAlpha = 1.0f;
        D2D1_COLOR_F                        backgroundColor{};

    private:
        HWND                                hwnd = nullptr;

#if JUCE_DIRECT2D_METRICS
        int64 paintStartTicks = 0;
        int64 paintEndTicks = 0;
#endif

        JUCE_DECLARE_WEAK_REFERENCEABLE(Pimpl)
    };

    //==============================================================================
    Direct2DGraphicsContext::Direct2DGraphicsContext() = default;

    Direct2DGraphicsContext::~Direct2DGraphicsContext() = default;

    bool Direct2DGraphicsContext::startFrame()
    {
        TRACE_LOG_D2D_START_FRAME;

        if (currentState = getPimpl()->startFrame(); currentState != nullptr)
        {
            if (auto deviceContext = getPimpl()->getDeviceContext())
            {
                //
                // Clip without transforming
                //
                // Clear() only works with axis-aligned clip layers, so if the window alpha is less than 1.0f, the clip region has to be the union
                // of all the paint areas
                //
                auto const& paintAreas = getPimpl()->getPaintAreas();
                if (paintAreas.getNumRectangles() == 1)
                {
                    currentState->pushAxisAlignedClipLayer(paintAreas.getRectangle(0));
                }
                else
                {
                    currentState->pushGeometryClipLayer(
                        direct2d::rectListToPathGeometry(getPimpl()->getDirect2DFactory(), paintAreas, AffineTransform{}, D2D1_FILL_MODE_WINDING, D2D1_FIGURE_BEGIN_FILLED));
                }

                //
                // Clear the buffer *after* setting the clip region
                //
                clearTargetBuffer();

                //
                // Init font & brush
                //
                setFont(currentState->font);
                currentState->updateCurrentBrush();
            }

            return true;
        }

        return false;
    }

    void Direct2DGraphicsContext::endFrame()
    {
        getPimpl()->finishFrame();

        currentState = nullptr;
    }

    void Direct2DGraphicsContext::setOrigin(Point<int> o)
    {
        TRACE_LOG_D2D_PAINT_CALL(etw::setOrigin);
        currentState->currentTransform.setOrigin(o);
    }

    void Direct2DGraphicsContext::addTransform(const AffineTransform& transform)
    {
        TRACE_LOG_D2D_PAINT_CALL(etw::addTransform);
        currentState->currentTransform.addTransform(transform);
    }

    bool Direct2DGraphicsContext::clipToRectangle(const Rectangle<int>& r)
    {
        TRACE_LOG_D2D_PAINT_CALL(etw::clipToRectangle);

        //
        // Transform the rectangle and update the current clip region
        //
        auto currentTransform = currentState->currentTransform.getTransform();
        auto transformedR = r.transformedBy(currentTransform);
        currentState->clipRegion = currentState->clipRegion.getIntersection(transformedR);

        if (auto deviceContext = getPimpl()->getDeviceContext())
        {
            if (currentState->currentTransform.isAxisAligned())
            {
                //
                // The current world transform is axis-aligned; push an axis aligned clip layer for better
                // performance
                //
                currentState->pushAxisAlignedClipLayer(r);
            }
            else
            {
                //
                // The current world transform is more complex; push a transformed geometry clip layer
                //
                // Instead of allocating a Geometry and then discarding it, use the ID2D1RectangleGeometry already
                // created by the pimpl. rectangleGeometryUnitSize is a 1x1 rectangle at the origin,
                // so pass a transform that scales, translates, and then applies the world transform.
                //
                auto transform = AffineTransform::scale(static_cast<float> (r.getWidth()), static_cast<float> (r.getHeight()))
                    .translated(r.toFloat().getTopLeft())
                    .followedBy(currentState->currentTransform.getTransform());

                currentState->pushTransformedRectangleGeometryClipLayer(getPimpl()->rectangleGeometryUnitSize, transform);
            }
        }

        return !isClipEmpty();
    }

    bool Direct2DGraphicsContext::clipToRectangleList(const RectangleList<int>& clipRegion)
    {
        TRACE_LOG_D2D_PAINT_CALL(etw::clipToRectangleList);

        //
        // Just one rectangle?
        //
        if (clipRegion.getNumRectangles() == 1)
        {
            return clipToRectangle(clipRegion.getRectangle(0));
        }

        //
        // Transform the rectangles and update the current clip region
        //
        auto const currentTransform = currentState->currentTransform.getTransform();
        auto       transformedR = clipRegion.getBounds().transformedBy(currentTransform);
        currentState->clipRegion = currentState->clipRegion.getIntersection(transformedR);

        if (auto deviceContext = getPimpl()->getDeviceContext())
        {
            currentState->pushGeometryClipLayer(direct2d::rectListToPathGeometry(getPimpl()->getDirect2DFactory(),
                clipRegion,
                currentState->currentTransform.getTransform(),
                D2D1_FILL_MODE_WINDING,
                D2D1_FIGURE_BEGIN_FILLED));
        }

        return !isClipEmpty();
    }

    void Direct2DGraphicsContext::excludeClipRectangle(const Rectangle<int>& r)
    {
        if (r.isEmpty())
        {
            return;
        }

        TRACE_LOG_D2D_PAINT_CALL(etw::excludeClipRectangle);

        //
        // To exclude the rectangle r, build a rectangle list with r as the first rectangle and a very large rectangle as the second.
        //
        // Then, convert that rectangle list to a geometry, but specify D2D1_FILL_MODE_ALTERNATE so the inside of r is *outside*
        // the geometry and everything else on the screen is inside the geometry.
        //
        // Have to use addWithoutMerging to build the rectangle list to keep the rectangles separate.
        //
        RectangleList<int> rectangles{ r };
        rectangles.addWithoutMerging({ -maxFrameSize, -maxFrameSize, maxFrameSize * 2, maxFrameSize * 2 });

        if (auto deviceContext = getPimpl()->getDeviceContext())
        {
            currentState->pushGeometryClipLayer(direct2d::rectListToPathGeometry(getPimpl()->getDirect2DFactory(),
                rectangles,
                currentState->currentTransform.getTransform(),
                D2D1_FILL_MODE_ALTERNATE,
                D2D1_FIGURE_BEGIN_FILLED));
        }
    }

    void Direct2DGraphicsContext::clipToPath(const Path& path, const AffineTransform& transform)
    {
        TRACE_LOG_D2D_PAINT_CALL(etw::clipToPath);

        if (auto deviceContext = getPimpl()->getDeviceContext())
        {
            currentState->pushGeometryClipLayer(
                direct2d::pathToPathGeometry(getPimpl()->getDirect2DFactory(), path, currentState->currentTransform.getTransformWith(transform), D2D1_FIGURE_BEGIN_FILLED));
        }
    }

    void Direct2DGraphicsContext::clipToImageAlpha(const Image& sourceImage, const AffineTransform& transform)
    {
        TRACE_LOG_D2D_PAINT_CALL(etw::clipToImageAlpha);

        if (sourceImage.isNull())
        {
            return;
        }

        if (auto deviceContext = getPimpl()->getDeviceContext())
        {
            //
            // Is this a Direct2D image already?
            //
            ComSmartPtr<ID2D1Bitmap> sourceBitmap;

            if (auto direct2DPixelData = dynamic_cast<Direct2DPixelData*> (sourceImage.getPixelData()))
            {
                sourceBitmap = direct2DPixelData->getAdapterD2D1Bitmap(getPimpl()->getAdapter());
            }
            else
            {
                //
                // Convert sourceImage to single-channel alpha-only maskImage
                //
                direct2d::Direct2DBitmap direct2DBitmap = direct2d::Direct2DBitmap::fromImage(sourceImage, deviceContext, Image::SingleChannel);
                sourceBitmap = direct2DBitmap.get();
            }

            if (sourceBitmap)
            {
                //
                // Make a transformed bitmap brush using the bitmap
                //
                // As usual, apply the current transform first *then* the transform parameter
                //
                ComSmartPtr<ID2D1BitmapBrush> brush;
                auto                          brushTransform = currentState->currentTransform.getTransformWith(transform);
                auto                          matrix = direct2d::transformToMatrix(brushTransform);
                D2D1_BRUSH_PROPERTIES         brushProps = { 1.0f, matrix };

                auto bitmapBrushProps = D2D1::BitmapBrushProperties(D2D1_EXTEND_MODE_CLAMP, D2D1_EXTEND_MODE_CLAMP);
                auto hr = deviceContext->CreateBitmapBrush(sourceBitmap, bitmapBrushProps, brushProps, brush.resetAndGetPointerAddress());

                if (SUCCEEDED(hr))
                {
                    //
                    // Push the clipping layer onto the layer stack
                    //
                    // Don't maskTransform in the LayerParameters struct; that only applies to geometry clipping
                    // Do set the contentBounds member, transformed appropriately
                    //
                    auto layerParams = D2D1::LayerParameters();
                    auto transformedBounds = sourceImage.getBounds().toFloat().transformedBy(brushTransform);
                    layerParams.contentBounds = direct2d::rectangleToRectF(transformedBounds);
                    layerParams.opacityBrush = brush;

                    currentState->pushLayer(layerParams);
                }
            }
        }
    }

    bool Direct2DGraphicsContext::clipRegionIntersects(const Rectangle<int>& r)
    {
        return getClipBounds().intersects(r);
    }

    Rectangle<int> Direct2DGraphicsContext::getClipBounds() const
    {
        return currentState->currentTransform.deviceSpaceToUserSpace(currentState->clipRegion);
    }

    bool Direct2DGraphicsContext::isClipEmpty() const
    {
        return getClipBounds().isEmpty();
    }

    void Direct2DGraphicsContext::saveState()
    {
        TRACE_LOG_D2D_PAINT_CALL(etw::saveState);

        currentState = getPimpl()->pushSavedState();
    }

    void Direct2DGraphicsContext::restoreState()
    {
        TRACE_LOG_D2D_PAINT_CALL(etw::restoreState);

        currentState = getPimpl()->popSavedState();
        currentState->updateColourBrush();
        jassert(currentState);
    }

    void Direct2DGraphicsContext::beginTransparencyLayer(float opacity)
    {
        TRACE_LOG_D2D_PAINT_CALL(etw::beginTransparencyLayer);

        if (auto deviceContext = getPimpl()->getDeviceContext())
        {
            currentState->pushTransparencyLayer(opacity);
        }
    }

    void Direct2DGraphicsContext::endTransparencyLayer()
    {
        TRACE_LOG_D2D_PAINT_CALL(etw::endTransparencyLayer);
        if (auto deviceContext = getPimpl()->getDeviceContext())
        {
            currentState->popTopLayer();
        }
    }

    void Direct2DGraphicsContext::setFill(const FillType& fillType)
    {
        TRACE_LOG_D2D_PAINT_CALL(etw::setFill);
        if (auto deviceContext = getPimpl()->getDeviceContext())
        {
            currentState->fillType = fillType;
            currentState->updateCurrentBrush();
        }
    }

    void Direct2DGraphicsContext::setOpacity(float newOpacity)
    {
        TRACE_LOG_D2D_PAINT_CALL(etw::setOpacity);

        currentState->setOpacity(newOpacity);
        if (auto deviceContext = getPimpl()->getDeviceContext())
        {
            currentState->updateCurrentBrush();
        }
    }

    void Direct2DGraphicsContext::setInterpolationQuality(Graphics::ResamplingQuality quality)
    {
        TRACE_LOG_D2D_PAINT_CALL(etw::setInterpolationQuality);

        switch (quality)
        {
        case Graphics::ResamplingQuality::lowResamplingQuality:
            currentState->interpolationMode = D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR;
            break;

        case Graphics::ResamplingQuality::mediumResamplingQuality:
            currentState->interpolationMode = D2D1_INTERPOLATION_MODE_LINEAR;
            break;

        case Graphics::ResamplingQuality::highResamplingQuality:
            currentState->interpolationMode = D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC;
            break;
        }
    }

    void Direct2DGraphicsContext::fillRect(const Rectangle<int>& r, bool replaceExistingContents)
    {
        if (replaceExistingContents)
        {
            currentState->pushAxisAlignedClipLayer(r);
            getPimpl()->clearBackground();
            currentState->popTopLayer();
        }

        fillRect(r.toFloat());
    }

    void Direct2DGraphicsContext::fillRect(const Rectangle<float>& r)
    {
        TRACE_LOG_D2D_PAINT_CALL(etw::fillRect);

        if (auto deviceContext = getPimpl()->getDeviceContext())
        {
            if (! currentState->isCurrentBrushUsable())
            {
                return;
            }

            updateDeviceContextTransform();
            deviceContext->FillRectangle(direct2d::rectangleToRectF(r), currentState->currentBrush);
        }
    }

    void Direct2DGraphicsContext::fillRectList(const RectangleList<float>& list)
    {
        for (auto& r : list) fillRect(r);
    }

    bool Direct2DGraphicsContext::drawRect(const Rectangle<float>& r, float lineThickness)
    {
        TRACE_LOG_D2D_PAINT_CALL(etw::drawRect);

        if (auto deviceContext = getPimpl()->getDeviceContext())
        {
            if (! currentState->isCurrentBrushUsable())
            {
                return true;
            }

            updateDeviceContextTransform();
            deviceContext->DrawRectangle(direct2d::rectangleToRectF(r), currentState->currentBrush, lineThickness);
        }

        return true;
    }

    void Direct2DGraphicsContext::fillPath(const Path& p, const AffineTransform& transform)
    {
        auto factory = getPimpl()->getDirect2DFactory();

        TRACE_LOG_D2D_PAINT_CALL(etw::fillPath);

        if (auto deviceContext = getPimpl()->getDeviceContext())
        {
            //
            // Don't bother if the path would be invisible
            //
            if (! currentState->isCurrentBrushUsable() || p.isEmpty())
            {
                return;
            }

            //
            // Use a cached geometry realisation?
            //
            if (auto geometryRealisation = getPimpl()->getFilledGeometryCache().getGeometryRealisation(p,
                factory,
                deviceContext,
                getPhysicalPixelScaleFactor()))
            {
                updateDeviceContextTransform(transform);
                deviceContext->DrawGeometryRealization(geometryRealisation, currentState->currentBrush);
                return;
            }

            //
            // Create and fill the geometry
            //
            if (auto geometry = direct2d::pathToPathGeometry(factory, p, transform, D2D1_FIGURE_BEGIN_FILLED))
            {
                updateDeviceContextTransform();
                deviceContext->FillGeometry(geometry, currentState->currentBrush);
            }
        }
    }

    bool Direct2DGraphicsContext::drawPath(const Path& p, const PathStrokeType& strokeType, const AffineTransform& transform)
    {
        TRACE_LOG_D2D_PAINT_CALL(etw::drawPath);

        if (auto factory = getPimpl()->getDirect2DFactory())
        {
            if (auto deviceContext = getPimpl()->getDeviceContext())
            {
                //
                // Don't bother if the path would be invisible
                //
                if (! currentState->isCurrentBrushUsable() || p.isEmpty())
                {
                    return true;
                }

                //
                // Use a cached geometry realisation?
                //
                if (auto geometryRealisation = getPimpl()->getStrokedGeometryCache().getGeometryRealisation(p,
                    strokeType,
                    factory,
                    deviceContext,
                    std::sqrt(std::abs(transform.getDeterminant())),
                    getPhysicalPixelScaleFactor()))
                {
                    updateDeviceContextTransform(transform);
                    deviceContext->DrawGeometryRealization(geometryRealisation, currentState->currentBrush);
                    return true;
                }

                //
                // Create and draw a geometry
                //
                if (auto geometry = direct2d::pathToPathGeometry(factory, p, transform, D2D1_FIGURE_BEGIN_HOLLOW))
                {
                    if (auto strokeStyle = direct2d::pathStrokeTypeToStrokeStyle(factory, strokeType))
                    {
                        updateDeviceContextTransform();
                        deviceContext->DrawGeometry(geometry, currentState->currentBrush, strokeType.getStrokeThickness(), strokeStyle);
                    }
                }
            }
        }

        return true;
    }

    void Direct2DGraphicsContext::drawImage(const Image& image, const AffineTransform& transform)
    {
        TRACE_LOG_D2D_PAINT_CALL(etw::drawImage);

        if (image.isNull())
        {
            return;
        }

        if (auto deviceContext = getPimpl()->getDeviceContext())
        {
            updateDeviceContextTransform(transform);

            //
            // Is this a Direct2D image already?
            //
            if (auto direct2DPixelData = dynamic_cast<Direct2DPixelData*> (image.getPixelData()))
            {
                if (auto bitmap = direct2DPixelData->getAdapterD2D1Bitmap(getPimpl()->getAdapter()))
                {
                    D2D1_RECT_F sourceRectangle = direct2d::rectangleToRectF(direct2DPixelData->deviceIndependentClipArea);
                    deviceContext->DrawBitmap(bitmap,
                        nullptr,
                        currentState->fillType.getOpacity(),
                        currentState->interpolationMode,
                        &sourceRectangle,
                        {});
                    return;
                }
            }

            //
            // Convert to Direct2D image
            //
            auto direct2DBitmap = direct2d::Direct2DBitmap::fromImage(image, deviceContext, Image::ARGB);
            if (auto bitmap = direct2DBitmap.get())
            {
                deviceContext->DrawBitmap(bitmap, nullptr, currentState->fillType.getOpacity(), currentState->interpolationMode, nullptr, {});
            }
        }
    }

    void Direct2DGraphicsContext::drawLine(const Line<float>& line)
    {
        drawLineWithThickness(line, 1.0f);
    }

    bool Direct2DGraphicsContext::drawLineWithThickness(const Line<float>& line, float lineThickness)
    {
        TRACE_LOG_D2D_PAINT_CALL(etw::drawLine);

        if (auto deviceContext = getPimpl()->getDeviceContext())
        {
            if (! currentState->isCurrentBrushUsable())
            {
                return true;
            }

            updateDeviceContextTransform();
            deviceContext->DrawLine(D2D1::Point2F(line.getStartX(), line.getStartY()),
                D2D1::Point2F(line.getEndX(), line.getEndY()),
                currentState->currentBrush,
                lineThickness);
        }

        return true;
    }

    void Direct2DGraphicsContext::setFont(const Font& newFont)
    {
        TRACE_LOG_D2D_PAINT_CALL(etw::setFont);

        currentState->setFont(newFont);
    }

    const Font& Direct2DGraphicsContext::getFont()
    {
        return currentState->font;
    }

    void Direct2DGraphicsContext::drawGlyph(int glyphNumber, const AffineTransform& transform)
    {
        TRACE_LOG_D2D_PAINT_CALL(etw::drawGlyph);

        getPimpl()->glyphRun.glyphIndices[0] = (uint16)glyphNumber;
        getPimpl()->glyphRun.glyphOffsets[0] = {};

        drawGlyphCommon(1, currentState->font, transform, {});
    }

    bool Direct2DGraphicsContext::drawTextLayout(const AttributedString& text, const Rectangle<float>& area)
    {
        TRACE_LOG_D2D_PAINT_CALL(etw::drawTextLayout);

        if (! currentState->isCurrentBrushUsable())
        {
            return true;
        }

        auto deviceContext = getPimpl()->getDeviceContext();
        auto directWriteFactory = getPimpl()->getDirectWriteFactory();
        auto fontCollection = getPimpl()->getSystemFonts();

        if (deviceContext && directWriteFactory && fontCollection)
        {
            updateDeviceContextTransform();

            auto translatedArea = area;
            auto textLayout =
                DirectWriteTypeLayout::createDirectWriteTextLayout(text, translatedArea, *directWriteFactory, *fontCollection, *deviceContext);
            if (textLayout)
            {
                deviceContext->DrawTextLayout(D2D1::Point2F(translatedArea.getX(), translatedArea.getY()),
                    textLayout,
                    currentState->currentBrush,
                    D2D1_DRAW_TEXT_OPTIONS_NONE);
            }
        }

        return true;
    }

    float Direct2DGraphicsContext::getPhysicalPixelScaleFactor()
    {
        return getPimpl()->getScaleFactor();
    }

    void Direct2DGraphicsContext::setPhysicalPixelScaleFactor(float scale_)
    {
        getPimpl()->setScaleFactor(scale_);
    }

    bool Direct2DGraphicsContext::drawRoundedRectangle(Rectangle<float> area, float cornerSize, float lineThickness)
    {
        TRACE_LOG_D2D_PAINT_CALL(etw::drawRoundedRectangle);

        if (auto deviceContext = getPimpl()->getDeviceContext())
        {
            if (! currentState->isCurrentBrushUsable())
            {
                return true;
            }

            updateDeviceContextTransform();
            D2D1_ROUNDED_RECT roundedRect{ direct2d::rectangleToRectF(area), cornerSize, cornerSize };
            deviceContext->DrawRoundedRectangle(roundedRect, currentState->currentBrush, lineThickness);
        }

        return true;
    }

    bool Direct2DGraphicsContext::fillRoundedRectangle(Rectangle<float> area, float cornerSize)
    {
        TRACE_LOG_D2D_PAINT_CALL(etw::fillRoundedRectangle);

        if (auto deviceContext = getPimpl()->getDeviceContext())
        {
            if (! currentState->isCurrentBrushUsable())
            {
                return true;
            }

            updateDeviceContextTransform();
            D2D1_ROUNDED_RECT roundedRect{ direct2d::rectangleToRectF(area), cornerSize, cornerSize };
            deviceContext->FillRoundedRectangle(roundedRect, currentState->currentBrush);
        }

        return true;
    }

    bool Direct2DGraphicsContext::drawEllipse(Rectangle<float> area, float lineThickness)
    {
        TRACE_LOG_D2D_PAINT_CALL(etw::drawEllipse);

        if (auto deviceContext = getPimpl()->getDeviceContext())
        {
            if (! currentState->isCurrentBrushUsable())
            {
                return true;
            }

            updateDeviceContextTransform();

            auto         centre = area.getCentre();
            D2D1_ELLIPSE ellipse{ { centre.x, centre.y }, area.proportionOfWidth(0.5f), area.proportionOfHeight(0.5f) };
            deviceContext->DrawEllipse(ellipse, currentState->currentBrush, lineThickness, nullptr);
        }
        return true;
    }

    bool Direct2DGraphicsContext::fillEllipse(Rectangle<float> area)
    {
        TRACE_LOG_D2D_PAINT_CALL(etw::fillEllipse);

        if (auto deviceContext = getPimpl()->getDeviceContext())
        {
            if (! currentState->isCurrentBrushUsable())
            {
                return true;
            }

            updateDeviceContextTransform();

            auto         centre = area.getCentre();
            D2D1_ELLIPSE ellipse{ { centre.x, centre.y }, area.proportionOfWidth(0.5f), area.proportionOfHeight(0.5f) };
            deviceContext->FillEllipse(ellipse, currentState->currentBrush);
        }
        return true;
    }

    void Direct2DGraphicsContext::drawGlyphRun(Array<PositionedGlyph> const& glyphs,
        int                           startIndex,
        int                           numGlyphs,
        const AffineTransform& transform,
        Rectangle<float>              underlineArea)
    {
        TRACE_LOG_D2D_PAINT_CALL(etw::drawGlyphRun);

        if (numGlyphs > 0 && (startIndex + numGlyphs) <= glyphs.size())
        {
            if (! currentState->isCurrentBrushUsable())
            {
                return;
            }

            //
            // Fill the array of glyph indices and offsets
            //
            // All the fonts should be the same for the glyph run
            //
            getPimpl()->glyphRun.ensureStorageAllocated(numGlyphs);

            auto const& font = glyphs[startIndex].getFont();
            auto        fontHorizontalScale = font.getHorizontalScale();
            auto        inverseHScale = fontHorizontalScale > 0.0f ? 1.0f / fontHorizontalScale : 1.0f;

            auto indices = getPimpl()->glyphRun.glyphIndices.getData();
            auto offsets = getPimpl()->glyphRun.glyphOffsets.getData();

            int numGlyphsToDraw = 0;
            for (int sourceIndex = 0; sourceIndex < numGlyphs; ++sourceIndex)
            {
                auto const& glyph = glyphs[sourceIndex + startIndex];
                if (!glyph.isWhitespace())
                {
                    indices[numGlyphsToDraw] = (UINT16)glyph.getGlyphNumber();
                    offsets[numGlyphsToDraw] = {
                        glyph.getLeft() * inverseHScale,
                        -glyph.getBaselineY()
                    }; // note the essential minus sign before the baselineY value; negative offset goes down, positive goes up (opposite from JUCE)
                    jassert(getPimpl()->glyphRun.glyphAdvances[numGlyphsToDraw] == 0.0f);
                    jassert(glyph.getFont() == font);
                    ++numGlyphsToDraw;
                }
            }

            drawGlyphCommon(numGlyphsToDraw, font, transform, underlineArea);
        }
    }

    void Direct2DGraphicsContext::drawGlyphCommon(int                    numGlyphs,
        Font const& font,
        const AffineTransform& transform,
        Rectangle<float>       underlineArea)
    {
        auto deviceContext = getPimpl()->getDeviceContext();
        if (!deviceContext)
        {
            return;
        }

        auto dwriteFontFace = direct2d::DirectWriteFontFace::fromFont(font);
        if (dwriteFontFace.fontFace == nullptr)
        {
            return;
        }

        if (! currentState->isCurrentBrushUsable())
        {
            return;
        }

        //
        // Draw the glyph run
        //
        auto scaledTransform = AffineTransform::scale(dwriteFontFace.fontHorizontalScale, 1.0f).followedBy(transform);
        auto glyphRunTransform = scaledTransform.followedBy(currentState->currentTransform.getTransform());
        getPimpl()->setDeviceContextTransform(glyphRunTransform);

        DWRITE_GLYPH_RUN directWriteGlyphRun;
        directWriteGlyphRun.fontFace = dwriteFontFace.fontFace;
        directWriteGlyphRun.fontEmSize = dwriteFontFace.getEmSize();
        directWriteGlyphRun.glyphCount = (UINT32) numGlyphs;
        directWriteGlyphRun.glyphIndices = getPimpl()->glyphRun.glyphIndices.getData();
        directWriteGlyphRun.glyphAdvances = getPimpl()->glyphRun.glyphAdvances.getData();
        directWriteGlyphRun.glyphOffsets = getPimpl()->glyphRun.glyphOffsets.getData();
        directWriteGlyphRun.isSideways = FALSE;
        directWriteGlyphRun.bidiLevel = 0;

        //
        // The gradient brushes are position-dependent, so need to undo the device context transform
        //
        SavedState::ScopedBrushTransformInverter brushTransformInverter{ currentState, scaledTransform };

        deviceContext->DrawGlyphRun({}, &directWriteGlyphRun, currentState->currentBrush);

        //
        // Draw the underline
        //
        if (!underlineArea.isEmpty())
        {
            fillRect(underlineArea);
        }
    }

    void Direct2DGraphicsContext::updateDeviceContextTransform()
    {
        getPimpl()->setDeviceContextTransform(currentState->currentTransform.getTransform());
    }

    void Direct2DGraphicsContext::updateDeviceContextTransform(AffineTransform chainedTransform)
    {
        getPimpl()->setDeviceContextTransform(currentState->currentTransform.getTransformWith(chainedTransform));
    }

} // namespace juce
