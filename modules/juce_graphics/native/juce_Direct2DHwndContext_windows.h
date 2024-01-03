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

namespace juce
{

class Direct2DHwndContext : public Direct2DGraphicsContext
{
public:
    Direct2DHwndContext (void* windowHandle, float dpiScalingFactor, bool opaque);
    ~Direct2DHwndContext() override;

    void* getHwnd() const noexcept;
    void handleShowWindow();
    void setWindowAlpha (float alpha);

    void setSize (int width, int height);
    void updateSize();

    void addDeferredRepaint (Rectangle<int> deferredRepaint);
    void addInvalidWindowRegionToDeferredRepaints();

    Image createSnapshot(Rectangle<int> deviceIndependentArea) override;
    Image createSnapshot() override;

    static Colour getBackgroundTransparencyKeyColour() noexcept
    {
        return Colour { 0xff000001 };
    }

private:
    struct HwndPimpl;
    std::unique_ptr<HwndPimpl> pimpl;

    Pimpl* getPimpl() const noexcept override;
    void clearTargetBuffer() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Direct2DHwndContext)
};

} // namespace juce
