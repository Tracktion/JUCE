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

//==============================================================================
//
// DirectWrite
//

DirectWrite::DirectWrite()
{
    JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE("-Wlanguage-extension-token")

    [[maybe_unused]] auto hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof (IDWriteFactory),
        (IUnknown**)directWriteFactory.resetAndGetPointerAddress());
    jassert(SUCCEEDED(hr));

    JUCE_END_IGNORE_WARNINGS_GCC_LIKE

    if (directWriteFactory != nullptr)
    {
        directWriteFactory->GetSystemFontCollection(systemFonts.resetAndGetPointerAddress());
    }
}

DirectWrite::~DirectWrite()
{
    if (directWriteFactory != nullptr)
    {
        //
        // Unregister all the custom font stuff and then clear the array before releasing the factories
        //
        for (auto customFontCollectionLoader : customFontCollectionLoaders)
        {
            directWriteFactory->UnregisterFontCollectionLoader(customFontCollectionLoader);
            directWriteFactory->UnregisterFontFileLoader(customFontCollectionLoader->getFontFileLoader());
        }

        customFontCollectionLoaders.clear();
    }

    directWriteFactory = nullptr;
    systemFonts = nullptr;
}

IDWriteFontFamily* DirectWrite::getFontFamilyForRawData(const void* data, size_t dataSize)
{
    //
    // Hopefully the raw data here is pointing to a TrueType font file in memory.
    // This creates a custom font collection loader (one custom font per font collection)
    //
    if (directWriteFactory != nullptr)
    {
        DirectWriteCustomFontCollectionLoader* customFontCollectionLoader = nullptr;
        for (auto loader : customFontCollectionLoaders)
        {
            if (loader->hasRawData(data, dataSize))
            {
                customFontCollectionLoader = loader;
                break;
            }
        }

        if (customFontCollectionLoader == nullptr)
        {
            customFontCollectionLoader = customFontCollectionLoaders.add(new DirectWriteCustomFontCollectionLoader{ data, dataSize });

            directWriteFactory->RegisterFontFileLoader(customFontCollectionLoader->getFontFileLoader());
            directWriteFactory->RegisterFontCollectionLoader(customFontCollectionLoader);

            directWriteFactory->CreateCustomFontCollection(customFontCollectionLoader,
                &customFontCollectionLoader->key,
                sizeof(customFontCollectionLoader->key),
                customFontCollectionLoader->customFontCollection.resetAndGetPointerAddress());
        }

        if (customFontCollectionLoader != nullptr && customFontCollectionLoader->customFontCollection != nullptr)
        {
            IDWriteFontFamily* directWriteFontFamily = nullptr;
            auto hr = customFontCollectionLoader->customFontCollection->GetFontFamily(0, &directWriteFontFamily);
            if (SUCCEEDED(hr))
            {
                return directWriteFontFamily;
            }
        }
    }

    return nullptr;
}


//==============================================================================
//
// Direct2D
//

DirectX::Direct2D::Direct2D()
{
    {
        D2D1_FACTORY_OPTIONS options;
#if JUCE_DIRECTX_DEBUG && JUCE_DEBUG
        options.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
#else
        options.debugLevel = D2D1_DEBUG_LEVEL_NONE;
#endif
        JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE("-Wlanguage-extension-token")
        [[maybe_unused]] auto hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED, __uuidof (ID2D1Factory1), &options,
            (void**)d2dSharedFactory.resetAndGetPointerAddress());
        jassert(SUCCEEDED(hr));
        JUCE_END_IGNORE_WARNINGS_GCC_LIKE
    }

    if (d2dSharedFactory != nullptr)
    {
        auto d2dRTProp = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                D2D1_ALPHA_MODE_IGNORE),
            0, 0,
            D2D1_RENDER_TARGET_USAGE_GDI_COMPATIBLE,
            D2D1_FEATURE_LEVEL_DEFAULT);

        d2dSharedFactory->CreateDCRenderTarget(&d2dRTProp, directWriteRenderTarget.resetAndGetPointerAddress());
    }

    JUCE_END_IGNORE_WARNINGS_GCC_LIKE
}

DirectX::Direct2D::~Direct2D()
{
    d2dSharedFactory = nullptr;  // (need to make sure these are released before deleting the DynamicLibrary objects)
    directWriteRenderTarget = nullptr;
}

} // namespace juce
