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

GlowEffect::GlowEffect() {}
GlowEffect::~GlowEffect() {}

void GlowEffect::setGlowProperties (float newRadius, Colour newColour, Point<int> pos)
{
    radius = newRadius;
    colour = newColour;
    offset = pos;
}

void GlowEffect::applyEffect (Image& image, Graphics& g, float scaleFactor, float alpha)
{
    Image temp;

    if (auto blurredImage = image.getPixelData()->applyNativeDropShadowEffect(radius * 4.0f, colour, 4.0f, g.getInternalContext().llgcFrameNumber); blurredImage.has_value())
    {
        temp = *blurredImage;

        g.drawImageAt(*blurredImage, offset.x, offset.y, false);
    }
    else
    {
        temp = Image{ image.getFormat(), image.getWidth(), image.getHeight(), true };

        ImageConvolutionKernel blurKernel(roundToInt(radius * scaleFactor * 2.0f));

        blurKernel.createGaussianBlur(radius);
        blurKernel.rescaleAllValues(radius);

        blurKernel.applyToImage (temp, image, image.getBounds());

        g.setColour(colour.withMultipliedAlpha(alpha));
        g.drawImageAt(temp, offset.x, offset.y, true);
    }

    g.setOpacity (alpha);
    g.drawImageAt (image, offset.x, offset.y, false);
}

} // namespace juce
