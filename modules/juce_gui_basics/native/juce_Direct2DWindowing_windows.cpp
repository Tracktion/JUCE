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
    #include <d2d1_2.h>
    #include <d3d11_1.h>
    #include <dcomp.h>
    #include <dwrite.h>
    #include <juce_core/juce_core.h>
    #include <juce_graphics/juce_graphics.h>
    #include <juce_gui_basics/juce_gui_basics.h>
    #include "juce_Windowing_windows.cpp"

#endif

class Direct2DComponentPeer : public HWNDComponentPeer
{
private:
    #if JUCE_DIRECT2D_METRICS
    int64 lastPaintStartTicks = 0;
    #endif

public:
    enum
    {
        direct2DRenderingEngine = softwareRenderingEngine + 1
    };

    //==============================================================================
    Direct2DComponentPeer (Component& comp, int windowStyleFlags, HWND parent, bool nonRepainting, int renderingEngine)
        : HWNDComponentPeer (comp, windowStyleFlags, parent, nonRepainting, renderingEngine)
    {
    }

    void createWindow() override
    {
        HWNDComponentPeer::createWindow();
        updateDirect2DContext();
    }

    void destroyWindow() noexcept override
    {
        direct2DContext = nullptr;
        HWNDComponentPeer::destroyWindow();
    }

    ~Direct2DComponentPeer() override
    {
        direct2DContext = nullptr;
    }

    DWORD adjustWindowStyleFlags (DWORD exStyleFlags) override
    {
        // don't need this anymore?
//         if (currentRenderingEngine == direct2DRenderingEngine)
//         {
//             //exStyleFlags |= WS_EX_LAYERED;
//         }

        return exStyleFlags;
    }

    void updateBorderSize() override
    {
        HWNDComponentPeer::updateBorderSize();

        updateDirect2DSize();
    }

    void setAlpha (float newAlpha) override
    {
        if (currentRenderingEngine == direct2DRenderingEngine)
        {
            const ScopedValueSetter<bool> scope(shouldIgnoreModalDismiss, true);

            if (direct2DContext)
            {
                direct2DContext->setWindowAlpha (newAlpha);
            }

            component.repaint();
            return;
        }

        HWNDComponentPeer::setAlpha (newAlpha);
    }

    void repaint (const Rectangle<int>& area) override
    {
        if (usingDirect2DRendering())
        {
            direct2DContext->addDeferredRepaint (area);
            return;
        }

        HWNDComponentPeer::repaint (area);
    }

    void dispatchDeferredRepaints()
    {
        if (usingDirect2DRendering())
        {
            return;
        }

        HWNDComponentPeer::dispatchDeferredRepaints();
    }

    void performAnyPendingRepaintsNow() override
    {
        if (usingDirect2DRendering())
        {
            // repaint will happen on the next vblank
            return;
        }

        HWNDComponentPeer::performAnyPendingRepaintsNow();
    }

    Image createWindowSnapshot()
    {
        if (usingDirect2DRendering())
        {
            return direct2DContext->createSnapshot();
        }

        return {};
    }

private:
    #if JUCE_ETW_TRACELOGGING
    SharedResourcePointer<ETWEventProvider> etwEventProvider;
    #endif
#if JUCE_DIRECT2D_METRICS
    direct2d::PaintStats::Ptr paintStats = new direct2d::PaintStats{};
#endif
    std::unique_ptr<Direct2DHwndContext> direct2DContext;

    void handlePaintMessage() override
    {
        if (usingDirect2DRendering())
        {
            direct2DContext->addInvalidWindowRegionToDeferredRepaints();
            return;
        }

    #if JUCE_DIRECT2D_METRICS
        auto paintStartTicks = Time::getHighResolutionTicks();
    #endif

        HWNDComponentPeer::handlePaintMessage();

    #if JUCE_DIRECT2D_METRICS
        if (lastPaintStartTicks > 0 && paintStats)
        {
            paintStats->addValueTicks (direct2d::PaintStats::frameInterval, paintStartTicks - lastPaintStartTicks);
            paintStats->addValueTicks (direct2d::PaintStats::messageThreadPaintDuration, Time::getHighResolutionTicks() - paintStartTicks);
        }
        lastPaintStartTicks = paintStartTicks;
    #endif
    }

    void onVBlank() override
    {
        HWNDComponentPeer::onVBlank();

        if (usingDirect2DRendering())
        {
            handleDirect2DPaint();
        }
    }

    bool usingDirect2DRendering() const noexcept
    {
        //jassert((currentRenderingEngine == direct2DRenderingEngine && direct2DContext) || (currentRenderingEngine == softwareRenderingEngine));
        return currentRenderingEngine == direct2DRenderingEngine && direct2DContext;
    }

    void handleDirect2DPaint()
    {
    #if JUCE_DIRECT2D_METRICS
        auto paintStartTicks = Time::getHighResolutionTicks();
    #endif

        jassert (direct2DContext);

        //
        // Use the ID2D1DeviceContext to paint a swap chain buffer, then tell the swap chain to present
        // the next buffer.
        //
        // Direct2DLowLevelGraphicsContext::startFrame checks if there are any areas to be painted and if the
        // renderer is ready to go; if so, startFrame allocates any needed Direct2D resources,
        // and calls ID2D1DeviceContext::BeginDraw
        //
        // handlePaint() makes various calls into the Direct2DLowLevelGraphicsContext which in turn calls
        // the appropriate ID2D1DeviceContext functions to draw rectangles, clip, set the fill color, etc.
        //
        // Direct2DLowLevelGraphicsContext::endFrame calls ID2D1DeviceContext::EndDraw to finish painting
        // and then tells the swap chain to present the next swap chain back buffer.
        //
        if (direct2DContext->startFrame())
        {
            handlePaint (*direct2DContext);
            direct2DContext->endFrame();

    #if JUCE_DIRECT2D_METRICS
            if (lastPaintStartTicks > 0 && direct2DContext->paintStats)
            {
                direct2DContext->paintStats->addValueTicks (direct2d::PaintStats::messageThreadPaintDuration,
                                           Time::getHighResolutionTicks() - paintStartTicks);
                direct2DContext->paintStats->addValueTicks (direct2d::PaintStats::frameInterval, paintStartTicks - lastPaintStartTicks);
            }
            lastPaintStartTicks = paintStartTicks;
    #endif
            return;
        }
    }

    void handleDirect2DResize (int width, int height)
    {
        if (direct2DContext)
        {
            direct2DContext->setSize (width, height);
        }
    }

    void updateDirect2DSize()
    {
        if (direct2DContext && component.isVisible())
        {
            direct2DContext->updateSize();
        }
    }

    StringArray getAvailableRenderingEngines() override
    {
        auto engines = HWNDComponentPeer::getAvailableRenderingEngines();

        if (SystemStats::getOperatingSystemType() >= SystemStats::Windows8_1) engines.add ("Direct2D");

        return engines;
    }

    void updateDirect2DContext()
    {
        switch (currentRenderingEngine)
        {
        case HWNDComponentPeer::softwareRenderingEngine:
        {
            setLayeredWindowStyle(false);

            direct2DContext = nullptr;

            setAlpha(layeredWindowAlpha / 255.0f);
            RedrawWindow(hwnd,
                nullptr,
                nullptr,
                RDW_ERASE | RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN);
            break;
        }

        case Direct2DComponentPeer::direct2DRenderingEngine:
        {
            if (direct2DContext && direct2DContext->getHwnd() != hwnd)
            {
                direct2DContext = nullptr;
            }

            if (!direct2DContext)
            {
                direct2DContext = std::make_unique<Direct2DHwndContext>(hwnd, (float)scaleFactor, component.isOpaque());

                //
                // Layered windows use the contents of the window back buffer to automatically determine mouse hit testing
                // But - Direct2D doesn't fill the window back buffer so the hit tests pass through for transparent windows
                //
                // Layered windows can use a single RGB colour as a transparency key (like a green screen). So - choose an
                // RGB color as the key and call SetLayeredWindowAttributes with LWA_COLORKEY and the key colour.
                //
                // Then, use an ID2D1HwndRenderTarget when resizing the window to flush the redirection bitmap to that same
                // RGB color.
                //
                // Also tried using the window class background brush and handling WM_ERASEBKGND; this seems to work best.
                //
                // Only certain colour values seem to work; RGB(0, 0, 1) seems OK
                //
                if (isNotOpaque())
                {
                    setLayeredWindowStyle(true);

                    auto backgroundKeyColour = Direct2DHwndContext::getBackgroundTransparencyKeyColour();
                    [[maybe_unused]] auto ok = SetLayeredWindowAttributes(hwnd, RGB(backgroundKeyColour.getRed(), backgroundKeyColour.getGreen(), backgroundKeyColour.getBlue()), 255, LWA_COLORKEY);
                    jassert(ok);
                }

#if JUCE_DIRECT2D_METRICS
                {
                    direct2DContext->paintStats = paintStats;

                    var direct2DVar{ new DynamicObject };
                    direct2DVar.getDynamicObject()->setProperty("Metrics", direct2DContext->paintStats.get());
                    component.getProperties().set("Direct2D", direct2DVar);
                }
#endif
            }
            break;
        }
        }
    }

    void setCurrentRenderingEngine ([[maybe_unused]] int index) override
    {
        if (index != currentRenderingEngine)
        {
            currentRenderingEngine = jlimit(0, getAvailableRenderingEngines().size() - 1, index);
        }

        updateDirect2DContext();
    }

    LRESULT handleSizeConstraining (RECT& r, const WPARAM wParam) override
    {
        auto result = HWNDComponentPeer::handleSizeConstraining (r, wParam);

        updateDirect2DSize();

        return result;
    }

    //==============================================================================
    LRESULT handleDPIChanging (int newDPI, RECT newRect) override
    {
        auto result = HWNDComponentPeer::handleDPIChanging (newDPI, newRect);

        if (direct2DContext)
        {
            direct2DContext->setPhysicalPixelScaleFactor ((float) scaleFactor);
        }

        return result;
    }

    LRESULT peerWindowProc (HWND messageHwnd, UINT message, WPARAM wParam, LPARAM lParam) override
    {
        //Logger::outputDebugString ("peerWindowProc d2d " + String::toHexString ((int) message));

        if (usingDirect2DRendering())
        {
            switch (message)
            {
            case WM_PAINT:
            {
                if (usingDirect2DRendering())
                {
                    direct2DContext->addInvalidWindowRegionToDeferredRepaints();
                    return 0;
                }
                break;
            }

            case WM_NCHITTEST:
            {
                //
                // Need to avoid setting WS_EX_TRANSPARENT since that hides OpenGL child windows;
                // instead, return HTTRANSPARENT to pass the mouse click through to next window below
                //
                if ((styleFlags & windowIgnoresMouseClicks) != 0)
                    return HTTRANSPARENT;

                return DefWindowProc(messageHwnd, message, wParam, lParam);
            }

            case WM_NCCALCSIZE:
            {
                TRACE_LOG_D2D_RESIZE(WM_NCCALCSIZE);

                if (direct2DContext && component.isVisible())
                {
                    RECT* rect = (RECT*)lParam;
                    direct2DContext->setSize(rect->right - rect->left, rect->bottom - rect->top);
                }
                break;
            }

            case WM_SYSCOMMAND:
            {
                switch (wParam & 0xfff0)
                {
                case SC_MAXIMIZE:
                case SC_RESTORE:
                {
                    if (messageHwnd == hwnd)
                    {
                        auto status = HWNDComponentPeer::peerWindowProc(messageHwnd, message, wParam, lParam);

                        updateDirect2DSize();

                        return status;
                    }

                    break;
                }

                case SC_MINIMIZE: break;
                }

                break;
            }

            case WM_SHOWWINDOW:
            {
                //
                // If this window is now shown (wParam != 0), tell the Direct2D LLGC to create resources
                // and paint the whole window immediately
                //
                if (direct2DContext && wParam)
                {
                    direct2DContext->handleShowWindow();
                    handleDirect2DPaint();
                }
                break;
            }

            default: break;
            }
        }

        return HWNDComponentPeer::peerWindowProc (messageHwnd, message, wParam, lParam);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Direct2DComponentPeer)
};

ComponentPeer* Component::createNewPeer (int styleFlags, void* parentHWND)
{
    auto peer = new Direct2DComponentPeer { *this, styleFlags, (HWND)parentHWND, false, Direct2DComponentPeer::direct2DRenderingEngine };
    peer->initialise();
    return peer;
}


Image createSnapshotOfNativeWindow(void* nativeWindowHandle)
{
    auto findPeerForHwnd = [&]() -> ComponentPeer*
        {
            int numDesktopComponents = Desktop::getInstance().getNumComponents();
            for (int index = 0; index < numDesktopComponents; ++index)
            {
                auto component = Desktop::getInstance().getComponent(index);
                if (auto peer = component->getPeer(); peer && peer->getNativeHandle() == nativeWindowHandle)
                {
                    return peer;
                }
            }

            return nullptr;
        };

    if (auto peer = findPeerForHwnd(); peer && peer->getCurrentRenderingEngine() == Direct2DComponentPeer::direct2DRenderingEngine)
    {
        if (auto direct2DPeer = dynamic_cast<Direct2DComponentPeer*>(peer))
        {
            return direct2DPeer->createWindowSnapshot();
        }
    }

    return createGDISnapshotOfNativeWindow(nativeWindowHandle);
}
