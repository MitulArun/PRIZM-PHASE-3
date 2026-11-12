/*
  ==============================================================================

    PRIZM V2 - mastering chain strip and module control pages.

    Every knob/switch here is attached to a real parameter that the DSP
    chain reads, so beginners can watch what the assistant changed and
    advanced users can fine-tune anything.

  ==============================================================================
*/

#pragma once

#include "../Engine/MeterData.h"
#include "Controls.h"

class Prizmv1AudioProcessor;

namespace prizm::ui
{

class ModuleRack : public juce::Component
{
public:
    enum class Page { Gain, EQ, Dynamics, Saturation, Imager, Limiter };

    explicit ModuleRack (Prizmv1AudioProcessor& processor);
    ~ModuleRack() override;

    void update (const MeterSnapshot& meters);
    void showPage (Page page);

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Height of the chain strip at the top of this component. */
    static constexpr int stripHeight = 54;

    class Pill;
    class PageBase;

private:
    Prizmv1AudioProcessor& processor;
    juce::OwnedArray<Pill> pills;
    juce::OwnedArray<PageBase> pages;
    Page current = Page::EQ;
};

} // namespace prizm::ui
