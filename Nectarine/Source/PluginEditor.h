#pragma once
#include "PluginProcessor.h"

class NectarineLnF : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float, float, juce::Slider::SliderStyle, juce::Slider&) override;
};

class NectarineEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit NectarineEditor (NectarineProcessor&);
    ~NectarineEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    NectarineProcessor& proc;
    NectarineLnF lnf;
    juce::Slider shape, intensity, voices, width, mix, output;
    juce::ComboBox mode;
    juce::ToggleButton bypass { "Bypass" };
    juce::Label lShape, lInt, lVoices, lWidth, lMix, lOut;
    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<SA> aShape, aInt, aVoices, aWidth, aMix, aOut;
    std::unique_ptr<CA> aMode;
    std::unique_ptr<BA> aBypass;
    juce::dsp::FFT fft { NectarineProcessor::fftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) NectarineProcessor::fftSize, juce::dsp::WindowingFunction<float>::hann };
    std::array<float, NectarineProcessor::fftSize * 2> fftData {};
    static constexpr int bins = 96;
    std::array<float, bins> curve {};
    juce::Rectangle<int> hexArea;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NectarineEditor)
};
