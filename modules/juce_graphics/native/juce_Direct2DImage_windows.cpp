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

#include <d2d1_2.h>
#include <d3d11_1.h>
#include <dcomp.h>
#include <dwrite.h>
#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>
#include <windows.h>
#include "juce_ETW_windows.h"
#include "juce_DirectX_windows.h"
#include "juce_Direct2DImage_windows.h"

#endif

namespace juce
{

    //==============================================================================
    //
    // Direct2D pixel data
    //

    Direct2DPixelData::Direct2DPixelData(Image::PixelFormat formatToUse,
        direct2d::DPIScalableArea<int> area_,
        bool clearImage_,
        DirectX::DXGI::Adapter::Ptr adapter_)
        : ImagePixelData(formatToUse,
            area_.getDeviceIndependentWidth(),
            area_.getDeviceIndependentHeight()),
        deviceIndependentClipArea(area_.withZeroOrigin().getDeviceIndependentArea()),
        imageAdapter(adapter_),
        area(area_.withZeroOrigin()),
        pixelStride((formatToUse == Image::SingleChannel) ? 1 : 4),
        lineStride((pixelStride* jmax(1, width) + 3) & ~3),
        clearImage(clearImage_)
    {
        createAdapterBitmap();

        directX->dxgi.adapters.listeners.add(this);
    }

    Direct2DPixelData::Direct2DPixelData(ReferenceCountedObjectPtr<Direct2DPixelData> source_,
        Rectangle<int> clipArea_,
        DirectX::DXGI::Adapter::Ptr adapter_)
        : ImagePixelData(source_->pixelFormat, source_->width, source_->height),
        deviceIndependentClipArea(clipArea_ + source_->deviceIndependentClipArea.getPosition()),
        imageAdapter(adapter_),
        area(source_->area.withZeroOrigin()),
        pixelStride(source_->pixelStride),
        lineStride(source_->lineStride),
        clearImage(false),
        adapterBitmap(source_->adapterBitmap)
    {
        createAdapterBitmap();

        directX->dxgi.adapters.listeners.add(this);
    }

    Direct2DPixelData::Direct2DPixelData(Image::PixelFormat formatToUse,
        direct2d::DPIScalableArea<int> area_,
        bool clearImage_,
        ID2D1Bitmap1* d2d1Bitmap,
        DirectX::DXGI::Adapter::Ptr adapter_)
        : ImagePixelData(formatToUse,
            area_.getDeviceIndependentWidth(),
            area_.getDeviceIndependentHeight()),
        deviceIndependentClipArea(area_.getDeviceIndependentArea()),
        imageAdapter(adapter_),
        area(area_.withZeroOrigin()),
        pixelStride(4),
        lineStride((pixelStride* jmax(1, width) + 3) & ~3),
        clearImage(clearImage_)
    {
        if (!imageAdapter || !imageAdapter->direct2DDevice)
        {
            imageAdapter = directX->dxgi.adapters.getDefaultAdapter();
        }

        deviceResources.create(imageAdapter, area.getDPIScalingFactor());

        adapterBitmap.set(d2d1Bitmap);

        directX->dxgi.adapters.listeners.add(this);
    }

    Direct2DPixelData::~Direct2DPixelData()
    {
        directX->dxgi.adapters.listeners.remove(this);
    }

    bool Direct2DPixelData::isValid() const noexcept
    {
        return imageAdapter && imageAdapter->direct2DDevice && adapterBitmap.get() != nullptr;
    }

    void Direct2DPixelData::createAdapterBitmap()
    {
        if (!imageAdapter || !imageAdapter->direct2DDevice)
            imageAdapter = directX->dxgi.adapters.getDefaultAdapter();

        deviceResources.create(imageAdapter, area.getDPIScalingFactor());
        adapterBitmap.create(deviceResources.deviceContext.context, pixelFormat, area, lineStride, clearImage);
    }

    void Direct2DPixelData::release()
    {
        if (adapterBitmap.get())
        {
            listeners.call(&Listener::imageDataBeingDeleted, this);
        }

        imageAdapter = nullptr;
        deviceResources.release();
        adapterBitmap.release();

        for (auto bitmap : mappableBitmaps)
        {
            bitmap->release();
        }
    }

    ReferenceCountedObjectPtr<Direct2DPixelData> Direct2DPixelData::fromDirect2DBitmap(ID2D1Bitmap1* const bitmap,
        direct2d::DPIScalableArea<int> area)
    {
        Direct2DPixelData::Ptr pixelData = new Direct2DPixelData{ Image::ARGB, area, false };
        pixelData->adapterBitmap.set(bitmap);
        return pixelData;
    }

    std::unique_ptr<LowLevelGraphicsContext> Direct2DPixelData::createLowLevelContext()
    {
        sendDataChangeMessage();

        createAdapterBitmap();

        auto bitmap = getAdapterD2D1Bitmap(imageAdapter);
        jassert(bitmap);

        auto context = std::make_unique<Direct2DImageContext>(imageAdapter);
        context->startFrame(bitmap, getDPIScalingFactor());
        context->clipToRectangle(deviceIndependentClipArea);
        context->setOrigin(deviceIndependentClipArea.getPosition());
        return context;
    }

    ID2D1Bitmap1* Direct2DPixelData::getAdapterD2D1Bitmap(DirectX::DXGI::Adapter::Ptr adapter)
    {
        jassert(adapter && adapter->direct2DDevice);

        if (!imageAdapter || !imageAdapter->direct2DDevice || imageAdapter->direct2DDevice != adapter->direct2DDevice)
        {
            release();

            imageAdapter = adapter;

            createAdapterBitmap();
        }

        jassert(imageAdapter && imageAdapter->direct2DDevice);
        jassert(adapter->direct2DDevice == imageAdapter->direct2DDevice);

        return adapterBitmap.get();
    }

    void Direct2DPixelData::initialiseBitmapData(Image::BitmapData& bitmap, int x, int y, Image::BitmapData::ReadWriteMode mode)
    {
        x += deviceIndependentClipArea.getX();
        y += deviceIndependentClipArea.getY();

        //
        // Use a mappable Direct2D bitmap to read the contents of the bitmap from the CPU back to the CPU
        //
        // Mapping the bitmap to the CPU means this class can read the pixel data, but the mappable bitmap
        // cannot be a render target
        //
        // So - the Direct2D image low-level graphics context allocates two bitmaps - the adapter bitmap and the mappable bitmap.
        // initialiseBitmapData copies the contents of the adapter bitmap to the mappable bitmap, then maps that mappable bitmap to the
        // CPU.
        //
        // Ultimately the data releaser copies the bitmap data from the CPU back to the GPU
        //
        // Adapter bitmap -> mappable bitmap -> mapped bitmap data -> adapter bitmap
        //
        bitmap.size = 0;
        bitmap.pixelFormat = pixelFormat;
        bitmap.pixelStride = pixelStride;
        bitmap.data = nullptr;

        auto mappableBitmap = new MappableBitmap{};

        if (auto sourceBitmap = getAdapterD2D1Bitmap(imageAdapter))
        {
            mappableBitmap->createAndMap(sourceBitmap,
                pixelFormat,
                Rectangle<int>{ x, y, width, height },
                deviceResources.deviceContext.context,
                deviceIndependentClipArea,
                area.getDPIScalingFactor(),
                lineStride);
            mappableBitmaps.add(mappableBitmap);
        }

        bitmap.lineStride = (int)mappableBitmap->mappedRect.pitch;
        bitmap.data = mappableBitmap->mappedRect.bits;
        bitmap.size = (size_t)mappableBitmap->mappedRect.pitch * (size_t)height;

        if (pixelFormat == Image::RGB && bitmap.data)
        {
            //
            // Direct2D doesn't support RGB formats, but some legacy code assumes that the pixel stride is 3.
            // Convert the ARGB data to RGB
            // The alpha mode should be set to D2D1_ALPHA_MODE_IGNORE, so the alpha value can be skipped.
            //
            auto softwareImage = SoftwareImageType{}.convertFromBitmapData(bitmap);
            mappableBitmap->rgbProxyImage = softwareImage.convertedToFormat(Image::RGB);
            mappableBitmap->rgbProxyBitmapData = std::make_unique<Image::BitmapData>(mappableBitmap->rgbProxyImage, mode);

            //
            // Change the bitmap data to refer to the RGB proxy
            //
            bitmap.pixelStride = mappableBitmap->rgbProxyBitmapData->pixelStride;
            bitmap.lineStride = mappableBitmap->rgbProxyBitmapData->lineStride;
            bitmap.data = mappableBitmap->rgbProxyBitmapData->data;
            bitmap.size = mappableBitmap->rgbProxyBitmapData->size;
        }

        auto bitmapDataScaledArea = direct2d::DPIScalableArea<int>::fromDeviceIndependentArea({ bitmap.width, bitmap.height }, area.getDPIScalingFactor());
        bitmap.width = bitmapDataScaledArea.getPhysicalArea().getWidth();
        bitmap.height = bitmapDataScaledArea.getPhysicalArea().getHeight();

        bitmap.dataReleaser = std::make_unique<Direct2DBitmapReleaser>(*this, mappableBitmap, mode);

        if (mode != Image::BitmapData::readOnly) sendDataChangeMessage();
    }

    ImagePixelData::Ptr Direct2DPixelData::clone()
    {
        auto clone = new Direct2DPixelData{ pixelFormat,
            area,
            false,
            imageAdapter };

        D2D1_POINT_2U destinationPoint{ 0, 0 };
        auto sourceRectU = area.getPhysicalAreaD2DRectU();
        auto sourceD2D1Bitmap = getAdapterD2D1Bitmap(imageAdapter);
        auto destinationD2D1Bitmap = clone->getAdapterD2D1Bitmap(imageAdapter);
        if (sourceD2D1Bitmap && destinationD2D1Bitmap)
        {
            auto hr = destinationD2D1Bitmap->CopyFromBitmap(&destinationPoint, sourceD2D1Bitmap, &sourceRectU);
            jassertquiet(SUCCEEDED(hr));
            if (SUCCEEDED(hr))
            {
                return clone;
            }
        }

        return nullptr;
    }

    ImagePixelData::Ptr Direct2DPixelData::clip(Rectangle<int> sourceArea)
    {
        sourceArea = sourceArea.getIntersection({ width, height });

        return new Direct2DPixelData{ pixelFormat,
            direct2d::DPIScalableArea<int>::fromDeviceIndependentArea(sourceArea, area.getDPIScalingFactor()),
            false,
            getAdapterD2D1Bitmap(imageAdapter),
            imageAdapter };
    }

    float Direct2DPixelData::getDPIScalingFactor() const noexcept
    {
        return area.getDPIScalingFactor();
    }

    std::unique_ptr<ImageType> Direct2DPixelData::createType() const
    {
        return std::make_unique<NativeImageType>(area.getDPIScalingFactor());
    }

    void Direct2DPixelData::adapterCreated(DirectX::DXGI::Adapter::Ptr adapter)
    {
        if (!imageAdapter || imageAdapter->uniqueIDMatches(adapter))
        {
            release();

            imageAdapter = adapter;
        }
    }

    void Direct2DPixelData::adapterRemoved(DirectX::DXGI::Adapter::Ptr adapter)
    {
        if (imageAdapter && imageAdapter->uniqueIDMatches(adapter))
        {
            release();
        }
    }

    Direct2DPixelData::Direct2DBitmapReleaser::Direct2DBitmapReleaser(Direct2DPixelData& pixelData_, ReferenceCountedObjectPtr<MappableBitmap> mappableBitmap_, Image::BitmapData::ReadWriteMode mode_)
        : pixelData(pixelData_),
        mappableBitmap(mappableBitmap_),
        mode(mode_)
    {
    }

    Direct2DPixelData::Direct2DBitmapReleaser::~Direct2DBitmapReleaser()
    {
        mappableBitmap->unmap(pixelData.adapterBitmap.get(), mode);
        pixelData.mappableBitmaps.removeObject(mappableBitmap);
    }

    //==============================================================================
    //
    // Direct2D native image type
    //

    ImagePixelData::Ptr NativeImageType::create(Image::PixelFormat format, int width, int height, bool clearImage) const
    {
        auto area = direct2d::DPIScalableArea<int>::fromDeviceIndependentArea({ width, height }, scaleFactor);
        return new Direct2DPixelData{ format, area, clearImage };
    }

    //==============================================================================
    //
    // Unit test
    //

#if JUCE_UNIT_TESTS

    class Direct2DImageUnitTest final : public UnitTest
    {
    public:
        Direct2DImageUnitTest()
            : UnitTest("Direct2DImageUnitTest", UnitTestCategories::graphics)
        {}

        void runTest() override
        {
            beginTest("Direct2DImageUnitTest");

            auto softwareImage = Image{ SoftwareImageType{}.create(Image::ARGB, 100, 100, true) };
            {
                Graphics g{ softwareImage };
                g.fillCheckerBoard(softwareImage.getBounds().toFloat(), 21.0f, 21.0f, juce::Colours::hotpink, juce::Colours::turquoise);
            }

            auto direct2DImage = NativeImageType{}.convert(softwareImage);

            {
                //
                // Bitmap data should match after conversion
                //
                Image::BitmapData softwareBitmapData{ softwareImage, Image::BitmapData::ReadWriteMode::readOnly };
                Image::BitmapData direct2DBitmapData{ direct2DImage, Image::BitmapData::ReadWriteMode::readOnly };

                expect(softwareBitmapData.width == direct2DBitmapData.width);
                expect(softwareBitmapData.height == direct2DBitmapData.height);

                for (int x = 0; x < softwareBitmapData.width; ++x)
                {
                    for (int y = 0; y < softwareBitmapData.height; ++y)
                    {
                        expect(softwareBitmapData.getPixelColour(x, y) == direct2DBitmapData.getPixelColour(x, y));
                    }
                }
            }

            {
                //
                // Subsection data should match
                //
                // Should be able to have two different BitmapData objects simultaneously for the same source image
                //
                Rectangle<int> area1 = randomRectangleWithin(softwareImage.getBounds());
                Rectangle<int> area2 = randomRectangleWithin(softwareImage.getBounds());
                Image::BitmapData softwareBitmapData{ softwareImage, Image::BitmapData::ReadWriteMode::readOnly };
                Image::BitmapData direct2DBitmapData1{ direct2DImage, area1.getX(), area1.getY(), area1.getWidth(), area1.getHeight(), Image::BitmapData::ReadWriteMode::readOnly };
                Image::BitmapData direct2DBitmapData2{ direct2DImage, area2.getX(), area2.getY(), area2.getWidth(), area2.getHeight(), Image::BitmapData::ReadWriteMode::readOnly };

                for (int x = 0; x < softwareBitmapData.width; ++x)
                {
                    for (int y = 0; y < softwareBitmapData.height; ++y)
                    {
                        if (area1.contains(x, y))
                        {
                            expect(softwareBitmapData.getPixelColour(x, y) == direct2DBitmapData1.getPixelColour(x - area1.getX(), y - area1.getY()));
                        }

                        if (area2.contains(x, y))
                        {
                            expect(softwareBitmapData.getPixelColour(x, y) == direct2DBitmapData2.getPixelColour(x - area2.getX(), y - area2.getY()));
                        }
                    }
                }
            }

            {
                //
                // BitmapData width & height should match
                //
                Rectangle<int> area = randomRectangleWithin(softwareImage.getBounds());
                Image::BitmapData softwareBitmapData{ softwareImage, area.getX(), area.getY(), area.getWidth(), area.getHeight(), Image::BitmapData::ReadWriteMode::readOnly };
                Image::BitmapData direct2DBitmapData{ direct2DImage, area.getX(), area.getY(), area.getWidth(), area.getHeight(), Image::BitmapData::ReadWriteMode::readOnly };

                expect(softwareBitmapData.width == direct2DBitmapData.width);
                expect(softwareBitmapData.height == direct2DBitmapData.height);
            }

            {
                //
                // Check read and write modes
                //
                int x = getRandom().nextInt(direct2DImage.getWidth());

                {
                    Image::BitmapData direct2DBitmapData{ direct2DImage, Image::BitmapData::ReadWriteMode::writeOnly };

                    for (int y = 0; y < direct2DBitmapData.height; ++y)
                    {
                        direct2DBitmapData.setPixelColour(x, y, juce::Colours::orange);
                    }
                }

                {
                    Image::BitmapData direct2DBitmapData{ direct2DImage, Image::BitmapData::ReadWriteMode::readOnly };

                    for (int y = 0; y < direct2DBitmapData.height; ++y)
                    {
                        expect(direct2DBitmapData.getPixelColour(x, y) == juce::Colours::orange);
                    }
                }
            }
        }

        Rectangle<int> randomRectangleWithin(Rectangle<int> container) const noexcept
        {
            auto x = getRandom().nextInt(container.getWidth() - 1);
            auto y = getRandom().nextInt(container.getHeight() - 1);
            auto h = getRandom().nextInt(container.getHeight() - x);
            auto w = getRandom().nextInt(container.getWidth() - y);
            return Rectangle<int>{ x, y, w, h };
        }
    };

    static Direct2DImageUnitTest direct2DImageUnitTest;

#endif



} // namespace juce
