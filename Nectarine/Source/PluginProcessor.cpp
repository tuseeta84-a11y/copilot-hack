#include "PluginProcessor.h"
#include "PluginEditor.h"

using APF = juce::AudioParameterFloat;

juce::AudioProcessorValueTreeState::ParameterLayout NectarineProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    auto pct = juce::NormalisableRange<float> (0.f, 1.f, 0.001f);
    l.add (std::make_unique<APF> ("shape",     "Shape",     pct, 0.5f));
    l.add (std::make_unique<APF> ("intensity", "Intensity", pct, 0.5f));
    l.add (std::make_unique<APF> ("width",     "Width",     pct, 0.5f));
    l.add (std::make_unique<APF> ("mix",       "Mix",       pct, 1.0f));
    l.add (std::make_unique<juce::AudioParameterInt>  ("voices", "Voices", 0, 4, 2));
    l.add (std::make_unique<juce::AudioParameterChoice> ("mode", "Tone",
            juce::StringArray { "Dark", "Warm", "Balanced", "Bright", "Air" }, 0));
    l.add (std::make_unique<APF> ("output", "Output",
            juce::NormalisableRange<float> (-12.f, 12.f, 0.1f), 0.f));
    l.add (std::make_unique<juce::AudioParameterBool> ("bypass", "Bypass", false));
    return l;
}

NectarineProcessor::NectarineProcessor()
    : AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout()) {}

bool NectarineProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    return in == out && (out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono());
}

void NectarineProcessor::updateTone (int mode)
{
    // Dark, Warm, Balanced, Bright, Air : {lowShelfDb, highShelfDb, highShelfHz}
    static const float t[5][3] = { { 2.5f, -5.f, 4500.f }, { 3.f, -2.f, 6000.f }, { 0.f, 0.f, 6000.f },
                                   { -1.f, 3.f, 5000.f }, { -2.f, 5.5f, 9000.f } };
    for (auto& c : ch) {
        c.lowShelf.set  (*juce::dsp::IIR::Coefficients<float>::makeLowShelf  (sr, 200.0, 0.7, juce::Decibels::decibelsToGain (t[mode][0])));
        c.highShelf.set (*juce::dsp::IIR::Coefficients<float>::makeHighShelf (sr, (double) t[mode][2], 0.7, juce::Decibels::decibelsToGain (t[mode][1])));
    }
    lastMode = mode;
}

void NectarineProcessor::prepareToPlay (double rate, int block)
{
    sr = rate;
    for (auto& c : ch) {
        c = {};
        c.hpf.set  (*juce::dsp::IIR::Coefficients<float>::makeHighPass (sr, 80.0));
        c.exHp.set (*juce::dsp::IIR::Coefficients<float>::makeHighPass (sr, 3500.0));
    }
    updateTone (apvts.getRawParameterValue ("mode")->load());
    oversampler.initProcessing ((size_t) block);
    oversampler.reset();
    int n = 1; while (n < (int) (sr * 0.06)) n <<= 1;
    delayMask = n - 1;
    for (auto& d : delayBuf) d.assign ((size_t) n, 0.f);
    writePos = 0;
    for (int v = 0; v < maxVoices; ++v) lfoPhase[(size_t) v] = (float) v / maxVoices;
    for (auto* s : { &sShape, &sIntensity, &sWidth, &sMix, &sOut, &sVoiceLvl }) s->reset (sr, 0.03);
    satEnv = 0.f; fifoIndex = 0;
    setLatencySamples ((int) std::round (oversampler.getLatencyInSamples()));
}

// Shape morphs: 0 = smooth tube-ish (asym tanh), 0.5 = tape (symmetric tanh), 1 = harder/diode-ish fold
float NectarineProcessor::shaper (float x, float s)
{
    float tube = std::tanh (x + 0.25f * x * x) - std::tanh (0.f);
    float tape = std::tanh (x);
    float hard = juce::jlimit (-1.f, 1.f, 1.3f * std::sin (juce::jlimit (-1.2f, 1.2f, x) * 1.0f));
    if (s < 0.5f) { float k = s * 2.f; return tube + (tape - tube) * k; }
    float k = (s - 0.5f) * 2.f; return tape + (hard - tape) * k;
}

void NectarineProcessor::processBlock (juce::AudioBuffer<float>& buf, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals nd;
    const int nCh = juce::jmin (2, buf.getNumChannels()), n = buf.getNumSamples();
    for (int c = nCh; c < buf.getNumChannels(); ++c) buf.clear (c, 0, n);

    auto get = [&] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    if (get ("bypass") > 0.5f) return;

    const int mode = (int) get ("mode");
    if (mode != lastMode) updateTone (mode);
    const int voices = (int) get ("voices");
    sShape.setTargetValue (get ("shape"));  sIntensity.setTargetValue (get ("intensity"));
    sWidth.setTargetValue (get ("width"));  sMix.setTargetValue (get ("mix"));
    sOut.setTargetValue (juce::Decibels::decibelsToGain (get ("output")));

    juce::AudioBuffer<float> dry; dry.makeCopyOf (buf, true);

    // --- 1. tone shaping + dynamics (per sample) ---
    const float atk = std::exp (-1.f / (0.005f * (float) sr)), rel = std::exp (-1.f / (0.12f * (float) sr));
    std::vector<float> intensityTrack ((size_t) n), shapeTrack ((size_t) n);
    for (int i = 0; i < n; ++i) { intensityTrack[(size_t) i] = sIntensity.getNextValue(); shapeTrack[(size_t) i] = sShape.getNextValue(); }

    for (int c = 0; c < nCh; ++c) {
        auto* d = buf.getWritePointer (c);
        auto& s = ch[(size_t) c];
        for (int i = 0; i < n; ++i) {
            float x = s.hpf.process (d[i]);
            x = s.highShelf.process (s.lowShelf.process (x));
            float I = intensityTrack[(size_t) i];
            // compressor: threshold falls with intensity, ratio ~ 2.5-4
            float a = std::abs (x);
            s.env = a > s.env ? atk * s.env + (1 - atk) * a : rel * s.env + (1 - rel) * a;
            float thrDb = -8.f - 22.f * I, lvlDb = juce::Decibels::gainToDecibels (s.env + 1e-6f);
            float grDb = lvlDb > thrDb ? (thrDb - lvlDb) * (1.f - 1.f / (2.f + 2.f * I)) : 0.f;
            float makeup = 0.5f * (-thrDb - 8.f) * 0.35f * (1.f - 1.f / (2.f + 2.f * I));
            x *= juce::Decibels::decibelsToGain (grDb + makeup);
            d[i] = x;
        }
    }

    // --- 2. saturation / harmonic exciter, 2x oversampled ---
    {
        juce::dsp::AudioBlock<float> block (buf);
        auto sub = block.getSubsetChannelBlock (0, (size_t) nCh);
        auto up = oversampler.processSamplesUp (sub);
        const size_t un = up.getNumSamples();
        for (size_t c = 0; c < (size_t) nCh; ++c) {
            auto* u = up.getChannelPointer (c);
            for (size_t i = 0; i < un; ++i) {
                float I = intensityTrack[juce::jmin ((size_t) n - 1, i / 2)], S = shapeTrack[juce::jmin ((size_t) n - 1, i / 2)];
                float drive = 1.f + 5.f * I * I + 0.8f * I;
                float x = u[i];
                float sat = shaper (x * drive, S) / (0.8f + 0.2f * drive);
                // exciter: saturate the high band and add it back
                float hi = ch[c].exHp.process (x);
                float ex = shaper (hi * (2.f + 6.f * I), S) * 0.25f * I;
                u[i] = sat + ex;
            }
        }
        oversampler.processSamplesDown (sub);
    }

    // --- 3. unison voices (modulated delay lines, panned), + width ---
    sVoiceLvl.setTargetValue (voices > 0 ? 1.f : 0.f);
    auto* L = buf.getWritePointer (0);
    auto* R = nCh > 1 ? buf.getWritePointer (1) : nullptr;
    static const float rates[maxVoices] = { 0.31f, 0.43f, 0.57f, 0.71f };
    static const float basesMs[maxVoices] = { 11.f, 15.f, 19.f, 23.f };
    static const float pans[maxVoices] = { -1.f, 1.f, -0.5f, 0.5f };
    for (int i = 0; i < n; ++i) {
        float w = sWidth.getNextValue(), vl = sVoiceLvl.getNextValue();
        float inL = L[i], inR = R ? R[i] : L[i];
        delayBuf[0][(size_t) writePos] = inL; delayBuf[1][(size_t) writePos] = inR;
        float wetL = 0.f, wetR = 0.f;
        for (int v = 0; v < voices; ++v) {
            auto& ph = lfoPhase[(size_t) v];
            ph += rates[v] / (float) sr; if (ph >= 1.f) ph -= 1.f;
            float mod = std::sin (juce::MathConstants<float>::twoPi * ph);
            float depthMs = 0.8f + 2.2f * w;                 // detune depth rises with width
            float dMs = basesMs[v] + depthMs * mod;
            float dSmp = dMs * 0.001f * (float) sr;
            float rp = (float) writePos - dSmp;
            while (rp < 0) rp += (float) (delayMask + 1);
            int i0 = (int) rp & delayMask, i1 = (i0 + 1) & delayMask; float fr = rp - std::floor (rp);
            auto& line = delayBuf[(size_t) (v & 1)];
            float y = line[(size_t) i0] * (1 - fr) + line[(size_t) i1] * fr;
            float p = pans[v] * (0.35f + 0.65f * w);
            wetL += y * 0.5f * (1.f - p); wetR += y * 0.5f * (1.f + p);
        }
        float g = vl * 0.55f / std::sqrt ((float) juce::jmax (1, voices));
        float oL = inL + wetL * g * 2.f, oR = inR + wetR * g * 2.f;
        // M/S width: 0..1 -> 0.4..1.6
        float m = 0.5f * (oL + oR), sd = 0.5f * (oL - oR) * (0.4f + 1.2f * w);
        oL = m + sd; oR = m - sd;
        L[i] = oL; if (R) R[i] = oR;
        writePos = (writePos + 1) & delayMask;
    }

    // --- 4. mix + output ---
    for (int c = 0; c < nCh; ++c) {
        auto* d = buf.getWritePointer (c); auto* dr = dry.getReadPointer (c);
        // temporary smoothers are shared across channels, so use a copy per channel
        auto smMix = sMix; auto smOut = sOut;
        for (int i = 0; i < n; ++i) {
            float m = smMix.getNextValue(), o = smOut.getNextValue();
            d[i] = (dr[i] * (1.f - m) + d[i] * m) * o;
        }
    }
    sMix.skip (n); sOut.skip (n);

    // spectrum feed (UI resets fifoIndex/spectrumReady after consuming)
    if (! spectrumReady.load())
        for (int i = 0; i < n && fifoIndex < fftSize; ++i)
            spectrumFifo[(size_t) fifoIndex++] = buf.getSample (0, i);
    if (fifoIndex >= fftSize) spectrumReady = true;
}

juce::AudioProcessorEditor* NectarineProcessor::createEditor() { return new NectarineEditor (*this); }

void NectarineProcessor::getStateInformation (juce::MemoryBlock& d)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, d);
}
void NectarineProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType())) apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new NectarineProcessor(); }
