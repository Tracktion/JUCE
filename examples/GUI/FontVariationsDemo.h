/*
  ==============================================================================

   This file is part of the JUCE framework examples.
   Copyright (c) Raw Material Software Limited

   The code included in this file is provided under the terms of the ISC license
   http://www.isc.org/downloads/software-support-policy/isc-license. Permission
   to use, copy, modify, and/or distribute this software for any purpose with or
   without fee is hereby granted provided that the above copyright notice and
   this permission notice appear in all copies.

   THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
   REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
   AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
   INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
   LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
   OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
   PERFORMANCE OF THIS SOFTWARE.

  ==============================================================================
*/

/*******************************************************************************
 The block below describes the properties of this PIP. A PIP is a short snippet
 of code that can be read by the Projucer and used to generate a JUCE project.

 BEGIN_JUCE_PIP_METADATA

 name:             FontVariationsDemo
 version:          1.0.0
 vendor:           JUCE
 website:          http://juce.com
 description:      Displays variable font variations.

 dependencies:     juce_core, juce_data_structures, juce_events, juce_graphics,
                   juce_gui_basics
 exporters:        xcode_mac, vs2022, vs2026, linux_make, androidstudio,
                   xcode_iphone

 moduleFlags:      JUCE_STRICT_REFCOUNTEDPOINTER=1

 type:             Component
 mainClass:        FontVariationsDemo

 useLocalCopy:     1

 END_JUCE_PIP_METADATA

*******************************************************************************/

#pragma once

#include "../Assets/DemoUtilities.h"

//==============================================================================
class VariableFontsListModel : public ListBoxModel
{
public:
    VariableFontsListModel()
    {
        Font::findFonts (fonts);

        // Filter to only variable fonts
        fonts.removeIf ([] (const Font& f)
        {
            return ! f.getTypefacePtr()->isVariableFont();
        });
    }

    std::function<void()> onFontSelected;

    int getNumRows() override
    {
        return fonts.size();
    }

    void paintListBoxItem (int rowNumber,
                           Graphics& g,
                           int width,
                           int height,
                           bool rowIsSelected) override
    {
        if (rowIsSelected)
            g.fillAll (Colours::lightblue);

        const Font options { FontOptions { getFaceForRow (rowNumber) } };

        AttributedString s;
        s.setWordWrap (AttributedString::none);
        s.setJustification (Justification::centredLeft);
        s.append (getNameForRow (rowNumber),
                  options.withPointHeight ((float) height * 0.7f),
                  Colours::black);

        s.append ("   " + getNameForRow (rowNumber),
                  FontOptions{}.withPointHeight ((float) height * 0.5f).withStyle ("Italic"),
                  Colours::grey);

        s.draw (g, Rectangle (width, height).expanded (-4, 50).toFloat());
    }

    void selectedRowsChanged (int) override
    {
        NullCheckedInvocation::invoke (onFontSelected);
    }

    Typeface::Ptr getFaceForRow (int rowNumber) const
    {
        if (! isPositiveAndBelow (rowNumber, fonts.size()))
            return nullptr;

        return fonts.getReference (rowNumber).getTypefacePtr();
    }

    String getNameForRow (int rowNumber) override
    {
        if (! isPositiveAndBelow (rowNumber, fonts.size()))
            return {};

        return fonts.getReference (rowNumber).getTypefaceName();
    }

    void addCustomFont (Typeface::Ptr typeface)
    {
        if (typeface != nullptr && typeface->isVariableFont())
        {
            fonts.add (Font { FontOptions { typeface } });
        }
    }

private:
    Array<Font> fonts;
};

//==============================================================================
class VariationAxisSlider : public Component
{
public:
    VariationAxisSlider (FontVariationSetting axisSetting)
        : setting (axisSetting)
    {
        slider.setRange (setting.minValue, setting.maxValue, 0.0);
        slider.setValue (setting.defaultValue, dontSendNotification);
        slider.setTextBoxStyle (Slider::TextBoxRight, false, 70, 20);
        slider.onValueChange = [this] { if (onValueChanged) onValueChanged (slider.getValue()); };
        addAndMakeVisible (slider);

        const auto tagStr = setting.tag.toString();
        const auto labelText = String::formatted ("%s  [%.1f - %.1f, default: %.1f]",
                                                   tagStr.toRawUTF8(),
                                                   setting.minValue,
                                                   setting.maxValue,
                                                   setting.defaultValue);
        label.setText (labelText, dontSendNotification);
        label.setFont (FontOptions{}.withPointHeight (14));
        addAndMakeVisible (label);

        resetButton.setButtonText ("Reset");
        resetButton.onClick = [this]
        {
            slider.setValue (setting.defaultValue, sendNotification);
        };
        addAndMakeVisible (resetButton);
    }

    void resized() override
    {
        auto bounds = getLocalBounds();
        label.setBounds (bounds.removeFromTop (getHeight() / 2));
        resetButton.setBounds (bounds.removeFromRight (60).reduced (2, 0));
        slider.setBounds (bounds);
    }

    FontVariationTag getTag() const { return setting.tag; }
    float getValue() const { return (float) slider.getValue(); }

    std::function<void(float)> onValueChanged;

private:
    FontVariationSetting setting;
    Label label;
    Slider slider;
    TextButton resetButton;
};

//==============================================================================
class VariationControlsComponent : public Component
{
public:
    VariationControlsComponent()
    {
        addAndMakeVisible (namedInstanceLabel);
        namedInstanceLabel.setText ("Named Instance:", dontSendNotification);
    }

    void setFont (Typeface::Ptr face)
    {
        currentFace = face;
        sliders.clear();

        if (currentFace == nullptr)
        {
            resized();
            return;
        }

        auto axes = currentFace->getVariationAxes();

        for (const auto& axis : axes)
        {
            auto* slider = sliders.add (new VariationAxisSlider (axis));
            slider->onValueChanged = [this] (float) { updatePreview(); };
            addAndMakeVisible (slider);
        }

        // Check for named instances
        namedInstances = currentFace->getVariationNamedInstances();

        if (! namedInstances.empty())
        {
            instanceCombo.clear();
            instanceCombo.addItem ("Custom", 1);

            for (int i = 0; i < namedInstances.size(); ++i)
            {
                if (namedInstances[i].name.isEmpty())
                    instanceCombo.addItem (juce::String (i), i + 2);
                else
                    instanceCombo.addItem (namedInstances[i].name, i + 2);
            }

            instanceCombo.setSelectedId (1, dontSendNotification);
            instanceCombo.onChange = [this] { applyNamedInstance(); };
            addAndMakeVisible (instanceCombo);
        }
        else
        {
            instanceCombo.setVisible (false);
        }

        resized();
        updatePreview();
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced (5);

        if (! namedInstances.empty())
        {
            instanceCombo.setVisible (true);
            auto top = bounds.removeFromTop (30);
            namedInstanceLabel.setBounds (top.removeFromLeft (150).reduced (2, 0));
            instanceCombo.setBounds (top.removeFromTop (25));
            bounds.removeFromTop (10);
        }
        else
        {
            instanceCombo.setVisible (false);
        }

        for (auto slider : sliders)
            slider->setBounds (bounds.removeFromTop (40));
    }

    Font getCurrentFont() const
    {
        if (currentFace == nullptr)
            return {};

        auto options = FontOptions { currentFace }.withPointHeight (30.0f);

        for (auto* slider : sliders)
        {
            options = options.withVariationSetting (FontVariationSetting {
                slider->getTag(),
                slider->getValue()
            });
        }

        return Font { options };
    }

    std::function<void()> onVariationChanged;

private:
    void updatePreview()
    {
        instanceCombo.setSelectedId (1, dontSendNotification);
        NullCheckedInvocation::invoke (onVariationChanged);
    }

    void applyNamedInstance()
    {
        const int selected = instanceCombo.getSelectedId();

        if (selected <= 1)
            return;

        const int index = selected - 2;

        if (! isPositiveAndBelow (index, namedInstances.size()))
            return;

        const auto& instance = namedInstances[index];

        for (const auto& setting : instance.settings)
        {
            for (auto* slider : sliders)
            {
                if (slider->getTag() == setting.tag)
                {
                    auto* sliderPtr = dynamic_cast<VariationAxisSlider*> (slider);
                    if (sliderPtr != nullptr)
                    {
                        // Access the slider member to set value
                        for (auto* child : sliderPtr->getChildren())
                        {
                            if (auto* s = dynamic_cast<Slider*> (child))
                            {
                                s->setValue (setting.value, sendNotification);
                                break;
                            }
                        }
                    }
                }
            }
        }
    }

    Typeface::Ptr currentFace;
    OwnedArray<VariationAxisSlider> sliders;
    std::vector<FontVariationNamedInstance> namedInstances;
    Label namedInstanceLabel;
    ComboBox instanceCombo;
};

//==============================================================================
class PreviewComponent : public Component
{
public:
    PreviewComponent()
    {
        previewText.setMultiLine (true);
        previewText.setReturnKeyStartsNewLine (true);
        previewText.setText ("The Quick Brown Fox Jumps Over The Lazy Dog\n"
                             "0123456789\n"
                             "Sphinx of black quartz, judge my vow.");
        addAndMakeVisible (previewText);
    }

    void setFont (const Font& font)
    {
        currentFont = font;
        previewText.applyFontToAllText (font);
    }

    void resized() override
    {
        previewText.setBounds (getLocalBounds());
    }

private:
    Font currentFont;
    TextEditor previewText;
};

//==============================================================================
class GlyphPathComponent : public Component
{
public:
    GlyphPathComponent()
    {
        textEditor.setMultiLine (false);
        textEditor.setText ("Ag");
        textEditor.onTextChange = [this] { updatePath(); };
        addAndMakeVisible (textEditor);
    }

    void setFont (const Font& font)
    {
        currentFont = font;
        textEditor.applyFontToAllText (currentFont.withHeight (textEditor.getHeight() * 0.9f), true);

        updatePath();
    }

    void paint (Graphics& g) override
    {
        g.fillAll (Colours::white);
        auto pathBounds = glyphPath.getBounds();

        if (glyphPath.getBounds().isEmpty())
            return;

        // Center and scale the path to fit
        auto bounds = getLocalBounds().reduced (20).withTrimmedTop (35).toFloat();
        auto scale = jmin (bounds.getWidth() / pathBounds.getWidth(),
                           bounds.getHeight() / pathBounds.getHeight()) * 0.9f;
        auto transform = glyphPath.getTransformToScaleToFit (bounds, true);

        g.setColour (Colours::black);
        g.strokePath (glyphPath, PathStrokeType (1.0f / scale), transform);

        g.setColour (Colours::black.withAlpha (0.1f));
        g.fillPath (glyphPath, transform);
    }

    void resized() override
    {
        textEditor.setBounds (getLocalBounds().removeFromTop (30).reduced (5));
    }

private:
    void updatePath()
    {
        glyphPath.clear();

        if (currentFont.getTypefacePtr() == nullptr)
            return;

        auto text = textEditor.getText();

        if (text.isEmpty())
            return;

        GlyphArrangement ga;
        ga.addLineOfText (currentFont, text, 0.0f, 0.0f);
        ga.createPath (glyphPath);

        repaint();
    }

    Font currentFont { FontOptions() };
    TextEditor textEditor;
    Path glyphPath;
};

//==============================================================================
class FontVariationsDemo : public Component
{
public:
    FontVariationsDemo()
    {
        fontsListBox.setTitle ("Variable Fonts");
        fontsListBox.setRowHeight (20);
        fontsListBox.setColour (ListBox::textColourId, Colours::black);
        fontsListBox.setColour (ListBox::backgroundColourId, Colours::white);

        fontsListModel.onFontSelected = [this]
        {
            auto face = fontsListModel.getFaceForRow (fontsListBox.getSelectedRow());
            variationControls.setFont (face);
            updatePreview();
        };

        variationControls.onVariationChanged = [this]
        {
            updatePreview();
        };

        loadFontButton.setButtonText ("Load Font File...");
        loadFontButton.onClick = [this] { loadFontFile(); };

        infoLabel.setFont (FontOptions{}.withPointHeight (16));
        infoLabel.setText ("Variable Font Variations - Adjust sliders to change axis values",
                           dontSendNotification);

        addAndMakeVisible (fontsListBox);
        addAndMakeVisible (loadFontButton);
        addAndMakeVisible (infoLabel);
        addAndMakeVisible (variationControls);
        addAndMakeVisible (previewComponent);
        addAndMakeVisible (glyphPathComponent);

        fontsListBox.selectRow (0);

        setSize (1200, 750);
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced (5);

        auto leftPanel = bounds.removeFromLeft (bounds.proportionOfWidth (0.25f));
        fontsListBox.setBounds (leftPanel.removeFromTop (leftPanel.getHeight() - 30));
        loadFontButton.setBounds (leftPanel.reduced (2));

        infoLabel.setBounds (bounds.removeFromTop (30).reduced (5));

        variationControls.setBounds (bounds.removeFromTop (bounds.proportionOfHeight (0.5f)));

        previewComponent.setBounds (bounds.removeFromLeft (bounds.proportionOfWidth (0.5f)));
        glyphPathComponent.setBounds (bounds);
    }

private:
    void updatePreview()
    {
        auto font = variationControls.getCurrentFont();
        previewComponent.setFont (font);
        glyphPathComponent.setFont (font);
    }

    void loadFontFile()
    {
        fileChooser = std::make_unique<FileChooser> ("Select a font file...",
                                                      File{},
                                                      "*.ttf;*.otf;*.ttc");

        fileChooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
                                  [this] (const FileChooser& chooser)
        {
            auto file = chooser.getResult();

            if (file == File{})
                return;

            MemoryBlock fontData;

            if (! file.loadFileAsData (fontData))
            {
                AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon,
                                                   "Error",
                                                   "Failed to load font file");
                return;
            }

            auto typeface = Typeface::createSystemTypefaceFor (fontData.getData(),
                                                                fontData.getSize());

            if (typeface == nullptr)
            {
                AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon,
                                                   "Error",
                                                   "Failed to create typeface from file");
                return;
            }

            if (! typeface->isVariableFont())
            {
                AlertWindow::showMessageBoxAsync (MessageBoxIconType::InfoIcon,
                                                   "Not a Variable Font",
                                                   "The selected font is not a variable font.");
                return;
            }

            fontsListModel.addCustomFont (typeface);
            fontsListBox.updateContent();
            fontsListBox.selectRow (fontsListModel.getNumRows() - 1);
        });
    }

    VariableFontsListModel fontsListModel;
    ListBox fontsListBox { {}, &fontsListModel };
    TextButton loadFontButton;
    Label infoLabel;
    VariationControlsComponent variationControls;
    PreviewComponent previewComponent;
    GlyphPathComponent glyphPathComponent;
    std::unique_ptr<FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE (FontVariationsDemo)
};
