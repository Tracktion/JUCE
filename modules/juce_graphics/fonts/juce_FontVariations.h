/*
  ==============================================================================

   This file is part of the JUCE framework.
   Copyright (c) Raw Material Software Limited

   JUCE is an open source framework subject to commercial or open source
   licensing.

   By downloading, installing, or using the JUCE framework, or combining the
   JUCE framework with any other source code, object code, content or any other
   copyrightable work, you agree to the terms of the JUCE End User Licence
   Agreement, and all incorporated terms including the JUCE Privacy Policy and
   the JUCE Website Terms of Service, as applicable, which will bind you. If you
   do not agree to the terms of these agreements, we will not license the JUCE
   framework to you, and you must discontinue the installation or download
   process and cease use of the JUCE framework.

   JUCE End User Licence Agreement: https://juce.com/legal/juce-8-licence/
   JUCE Privacy Policy: https://juce.com/juce-privacy-policy
   JUCE Website Terms of Service: https://juce.com/juce-website-terms-of-service/

   Or:

   You may also use this code under the terms of the AGPLv3:
   https://www.gnu.org/licenses/agpl-3.0.en.html

   THE JUCE FRAMEWORK IS PROVIDED "AS IS" WITHOUT ANY WARRANTY, AND ALL
   WARRANTIES, WHETHER EXPRESSED OR IMPLIED, INCLUDING WARRANTY OF
   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, ARE DISCLAIMED.

  ==============================================================================
*/

namespace juce
{

/** Represents an OpenType font variation axis tag.

    OpenType variable fonts support design-variation axes that allow continuous
    interpolation along dimensions such as weight, width, optical size, and others.
    Each axis is identified by a four-character tag. Registered axis tags use
    lowercase letters (like 'wght' for weight, 'wdth' for width), while custom
    axes use uppercase tags (like 'FILL' for fill).

    The tag must be exactly 4 characters long.

    @see FontVariationSetting, FontOptions, Typeface

    @tags{Graphics}
*/
class JUCE_API FontVariationTag final
{
public:
    /** Constructs a variation axis tag from the specified tag string. */
    constexpr FontVariationTag (const char (&string)[5])
        : tag { (uint32) string[0] << 24
              | (uint32) string[1] << 16
              | (uint32) string[2] << 8
              | (uint32) string[3] }
    {
    }

    /** Constructs a variation axis tag from the specified tag value. */
    constexpr explicit FontVariationTag (uint32 tagValue)
        : tag (tagValue)
    {
    }

    /** Creates a new FontVariationTag from the specified string. */
    [[nodiscard]] static FontVariationTag fromString (const String& tagString);

    /** Returns a string representation of this tag. */
    String toString() const;

    /** Returns the HarfBuzz compatible OpenType tag as an unsigned 32-bit integer. */
    constexpr uint32 getTag() const { return tag; }

    /** Comparison based on tag value. */
    [[nodiscard]] constexpr bool operator<  (FontVariationTag other) const { return tag <  other.tag; }
    /** Comparison based on tag value. */
    [[nodiscard]] constexpr bool operator<= (FontVariationTag other) const { return tag <= other.tag; }
    /** Comparison based on tag value. */
    [[nodiscard]] constexpr bool operator>  (FontVariationTag other) const { return tag >  other.tag; }
    /** Comparison based on tag value. */
    [[nodiscard]] constexpr bool operator>= (FontVariationTag other) const { return tag >= other.tag; }
    /** Comparison based on tag value. */
    [[nodiscard]] constexpr bool operator== (FontVariationTag other) const { return tag == other.tag; }
    /** Comparison based on tag value. */
    [[nodiscard]] constexpr bool operator!= (FontVariationTag other) const { return tag != other.tag; }

private:
    FontVariationTag() = default;
    uint32 tag;
};

/** Represents a font variation axis setting with its value and valid range.

    Variable fonts support design axes like weight, width, and optical size.
    Each axis has a minimum, maximum, and default value. The value member
    holds the current setting for this axis.

    When queried from a font via Typeface::getVariationAxes(), the minValue,
    maxValue, and defaultValue fields will be populated from the font's metadata.
    When constructed manually for setting a variation, these range fields may
    be left at their default values of 0.

    Common registered axes include:
    - 'wght' (weight): typically 100-900, default 400
    - 'wdth' (width): typically 50-200, default 100
    - 'ital' (italic): 0-1
    - 'slnt' (slant): typically -12 to 12 degrees
    - 'opsz' (optical size): typically 6-144 points

    Custom axes (uppercase tags) are font-specific, such as:
    - 'FILL' (fill): 0-1 (used by Material Symbols)
    - 'GRAD' (grade): typically -200 to 200 (used by Material Symbols)

    @see FontVariationTag, FontOptions, Typeface

    @tags{Graphics}
*/
class JUCE_API FontVariationSetting final
{
    constexpr auto tie() const;

public:
    /** Constructs a variation setting with just a tag and value.
        The min, max, and default values will be set to 0.
    */
    constexpr FontVariationSetting (FontVariationTag axisTag, float axisValue) noexcept
        : tag (axisTag),
          value (axisValue),
          minValue (0.0f),
          maxValue (0.0f),
          defaultValue (0.0f)
    {
    }

    /** Constructs a variation setting with full axis metadata. */
    constexpr FontVariationSetting (FontVariationTag axisTag,
                                    float axisValue,
                                    float axisMinValue,
                                    float axisMaxValue,
                                    float axisDefaultValue) noexcept
        : tag (axisTag),
          value (axisValue),
          minValue (axisMinValue),
          maxValue (axisMaxValue),
          defaultValue (axisDefaultValue)
    {
    }

    [[nodiscard]] constexpr bool operator<  (const FontVariationSetting& other) const;
    [[nodiscard]] constexpr bool operator<= (const FontVariationSetting& other) const;
    [[nodiscard]] constexpr bool operator>  (const FontVariationSetting& other) const;
    [[nodiscard]] constexpr bool operator>= (const FontVariationSetting& other) const;
    [[nodiscard]] constexpr bool operator== (const FontVariationSetting& other) const;
    [[nodiscard]] constexpr bool operator!= (const FontVariationSetting& other) const;

    /** The OpenType variation axis tag. */
    FontVariationTag tag;

    /** The value for this variation axis in design units. */
    float value;

    /** The minimum value supported by this axis.
        When queried from a font, this will be set to the font's reported minimum.
        When constructed manually, this may be 0.
    */
    float minValue;

    /** The maximum value supported by this axis.
        When queried from a font, this will be set to the font's reported maximum.
        When constructed manually, this may be 0.
    */
    float maxValue;

    /** The default value for this axis.
        When queried from a font, this will be set to the font's default value.
        When constructed manually, this may be 0.
    */
    float defaultValue;
};

/** Information about a named instance in a variable font.

    Variable fonts can define preset combinations of axis values,
    such as "Bold", "Light", "Condensed", etc. These are called named instances.

    Each named instance has a name (like "Bold") and a set of variation settings
    that define the axis values for that instance.

    @see FontVariationSetting, Typeface::getVariationNamedInstances()

    @tags{Graphics}
*/
struct JUCE_API FontVariationNamedInstance
{
    /** The display name of this instance (e.g., "Bold", "Light Condensed"). */
    String name;

    /** The variation axis settings that define this instance.
        Each setting includes the axis tag, value, and range information.
    */
    std::vector<FontVariationSetting> settings;
};

} // namespace juce
