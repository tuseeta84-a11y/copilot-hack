#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

class NectarineProcessor : public juce::AudioProcessor
{
public:
    NectarineProcessor();
    ~NectarineProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Nectarine"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.1; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;

    // spectrum for the UI (post-processing, mono)
    static constexpr int fftOrder = 11, fftSize = 1 << fftOrder;
    std::atomic<bool> spectrumReady { false };
    std::array<float, fftSize> spectrumFifo {};
    int fifoIndex = 0;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    struct Biquad {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        float process (float x) { float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
        void set (const juce::dsp::IIR::Coefficients<float>& c) {
            auto* p = c.getRawCoefficients();
            b0 = p[0]; b1 = p[1]; b2 = p[2]; a1 = p[3]; a2 = p[4];
        }
        void reset() { z1 = z2 = 0; }
    };

    struct Channel {
        Biquad lowShelf, highShelf, hpf, exHp, sideLp;
        float env = 0.f;
        float lpState = 0.f;
    };

    static constexpr int maxVoices = 4;
    std::array<Channel, 2> ch;
    juce::dsp::Oversampling<float> oversampler { 2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR };

    // unison voices: modulated delay lines
    std::array<std::vector<float>, 2> delayBuf;
    int writePos = 0, delayMask = 0;
    std::array<float, maxVoices> lfoPhase {};
    juce::SmoothedValue<float> sShape, sIntensity, sWidth, sMix, sOut, sVoiceLvl;
    double sr = 44100.0;
    int lastMode = -1;
    float satEnv = 0.f;

    void updateTone (int mode);
    static float shaper (float x, float shape);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NectarineProcessor)
};
