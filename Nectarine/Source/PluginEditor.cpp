#include "PluginEditor.h"

static const juce::Colour kBg (0xff15161a), kPanel (0xff23252b), kOrange (0xffe9a23b), kRed (0xffe0575b), kIce (0xffa8dff0);

static void accentFor (juce::Slider& s, juce::Colour c) { s.setColour (juce::Slider::rotarySliderFillColourId, c); s.setColour (juce::Slider::thumbColourId, c); }

void NectarineLnF::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s)
{
    auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (6.f);
    float d = juce::jmin (b.getWidth(), b.getHeight()); b = b.withSizeKeepingCentre (d, d);
    auto col = s.findColour (juce::Slider::rotarySliderFillColourId);
    float ang = a0 + pos * (a1 - a0);
    juce::Path track, val;
    track.addCentredArc (b.getCentreX(), b.getCentreY(), d * 0.5f, d * 0.5f, 0, a0, a1, true);
    val.addCentredArc (b.getCentreX(), b.getCentreY(), d * 0.5f, d * 0.5f, 0, a0, ang, true);
    g.setColour (col.withAlpha (0.18f)); g.strokePath (track, juce::PathStrokeType (5.f));
    g.setColour (col); g.strokePath (val, juce::PathStrokeType (5.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    auto knob = b.reduced (d * 0.14f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a3d45), knob.getX(), knob.getY(), juce::Colour (0xff1e2025), knob.getRight(), knob.getBottom(), false));
    g.fillEllipse (knob);
    g.setColour (juce::Colours::white.withAlpha (0.06f)); g.drawEllipse (knob, 1.f);
    auto c = knob.getCentre(); float r = knob.getWidth() * 0.5f;
    g.setColour (col); g.drawLine (c.x + std::sin (ang) * r * 0.55f, c.y - std::cos (ang) * r * 0.55f,
                                   c.x + std::sin (ang) * r * 0.9f,  c.y - std::cos (ang) * r * 0.9f, 3.f);
    g.setColour (juce::Colours::white.withAlpha (0.85f)); g.setFont (juce::FontOptions (13.f, juce::Font::bold));
    g.drawText (s.getTextFromValue (s.getValue()), knob.toNearestInt(), juce::Justification::centred);
}

void NectarineLnF::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider::SliderStyle, juce::Slider& s)
{
    auto r = juce::Rectangle<float> ((float) x, (float) y + h * 0.5f - 6.f, (float) w, 12.f);
    g.setColour (juce::Colour (0xff0d0e10)); g.fillRoundedRectangle (r, 6.f);
    g.setColour (kIce.withAlpha (0.35f)); g.fillRoundedRectangle (r.withRight (pos), 6.f);
    g.setColour (juce::Colour (0xffd8dde2)); g.fillEllipse (pos - 11.f, r.getCentreY() - 11.f, 22.f, 22.f);
    juce::ignoreUnused (s);
}

NectarineEditor::NectarineEditor (NectarineProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    auto setup = [&] (juce::Slider& s, juce::Label& l, const juce::String& name, juce::Colour c, juce::Slider::SliderStyle st = juce::Slider::RotaryHorizontalVerticalDrag)
    {
        s.setSliderStyle (st); s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        s.setRotaryParameters (juce::degreesToRadians (225.f), juce::degreesToRadians (495.f), true);
        accentFor (s, c); addAndMakeVisible (s);
        l.setText (name, juce::dontSendNotification); l.setJustificationType (juce::Justification::centred);
        l.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.8f)); l.setFont (juce::FontOptions (12.f, juce::Font::bold));
        addAndMakeVisible (l);
    };
    setup (shape, lShape, "SHAPE", kOrange);  setup (intensity, lInt, "INTENSITY", kRed);
    setup (voices, lVoices, "VOICES", kIce);  setup (mix, lMix, "MIX", juce::Colour (0xff6fcfbf));
    setup (output, lOut, "OUTPUT", juce::Colour (0xff9aa0aa));
    setup (width, lWidth, "WIDTH", kIce, juce::Slider::LinearHorizontal);
    for (auto* s : { &shape, &intensity, &mix }) s->textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v * 100)); };
    voices.textFromValueFunction = [] (double v) { static const char* n[] = { "Off", "Single", "Duo", "Trio", "Unison" }; return juce::String (n[juce::jlimit (0, 4, (int) v)]); };
    output.textFromValueFunction = [] (double v) { return juce::String (v, 1); };

    mode.addItemList ({ "Dark", "Warm", "Balanced", "Bright", "Air" }, 1);
    addAndMakeVisible (mode); addAndMakeVisible (bypass);
    aShape = std::make_unique<SA> (proc.apvts, "shape", shape);  aInt = std::make_unique<SA> (proc.apvts, "intensity", intensity);
    aVoices = std::make_unique<SA> (proc.apvts, "voices", voices); aWidth = std::make_unique<SA> (proc.apvts, "width", width);
    aMix = std::make_unique<SA> (proc.apvts, "mix", mix);         aOut = std::make_unique<SA> (proc.apvts, "output", output);
    aMode = std::make_unique<CA> (proc.apvts, "mode", mode);      aBypass = std::make_unique<BA> (proc.apvts, "bypass", bypass);
    setSize (860, 480);
    startTimerHz (30);
}

NectarineEditor::~NectarineEditor() { setLookAndFeel (nullptr); }

void NectarineEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBg);
    g.setColour (juce::Colours::white); g.setFont (juce::FontOptions (26.f, juce::Font::bold));
    g.drawText ("NECTARINE", 20, 10, 220, 36, juce::Justification::centredLeft);
    g.setColour (juce::Colours::white.withAlpha (0.45f)); g.setFont (12.f);
    g.drawText ("VOCAL CHARACTER", 22, 38, 220, 16, juce::Justification::centredLeft);
    // hexagon display
    juce::Path hex; auto c = hexArea.toFloat().getCentre(); float r = hexArea.getHeight() * 0.5f;
    for (int i = 0; i < 6; ++i) { float a = juce::degreesToRadians (60.f * i - 90.f); juce::Point<float> p (c.x + r * std::cos (a) * 0.95f, c.y + r * std::sin (a)); i ? hex.lineTo (p) : hex.startNewSubPath (p); }
    hex.closeSubPath();
    g.setColour (juce::Colour (0xff0a0b0d)); g.fillPath (hex);
    g.setColour (juce::Colours::white.withAlpha (0.08f)); g.strokePath (hex, juce::PathStrokeType (1.5f));
    juce::Graphics::ScopedSaveState ss (g); g.reduceClipRegion (hex);
    juce::Path line; auto b = hexArea.toFloat();
    for (int i = 0; i < bins; ++i) {
        float x = b.getX() + b.getWidth() * i / (bins - 1), y = b.getBottom() - curve[(size_t) i] * b.getHeight() * 0.85f - 10.f;
        i ? line.lineTo (x, y) : line.startNewSubPath (x, y);
    }
    g.setColour (kOrange.withAlpha (0.35f)); g.strokePath (line, juce::PathStrokeType (9.f, juce::PathStrokeType::curved));
    g.setColour (juce::Colour (0xfff2c27a)); g.strokePath (line, juce::PathStrokeType (2.f, juce::PathStrokeType::curved));
}

void NectarineEditor::resized()
{
    auto r = getLocalBounds().withTrimmedTop (60).reduced (20);
    auto left = r.removeFromLeft (230), right = r.removeFromRight (230);
    shape.setBounds (left.removeFromTop (210)); lShape.setBounds (left.removeFromTop (20));
    mix.setBounds (left.removeFromTop (100).withSizeKeepingCentre (90, 90)); lMix.setBounds (left.removeFromTop (20));
    intensity.setBounds (right.removeFromTop (210)); lInt.setBounds (right.removeFromTop (20));
    voices.setBounds (right.removeFromTop (100).withSizeKeepingCentre (130, 130).translated (0, 15)); lVoices.setBounds (right.removeFromTop (20).translated (0, 20));
    auto mid = r.reduced (10, 0);
    auto top = mid.removeFromTop (34);
    mode.setBounds (top.withSizeKeepingCentre (180, 30));
    bypass.setBounds (getWidth() - 150, 16, 120, 28);
    lOut.setBounds (getWidth() - 270, 38, 60, 14); output.setBounds (getWidth() - 275, 8, 60, 30);
    auto bottom = mid.removeFromBottom (60);
    width.setBounds (bottom.removeFromTop (34).reduced (30, 0)); lWidth.setBounds (bottom.removeFromTop (20));
    hexArea = mid.reduced (0, 10).withSizeKeepingCentre (juce::jmin (mid.getWidth(), mid.getHeight() - 20), mid.getHeight() - 20);
}

void NectarineEditor::timerCallback()
{
    if (proc.spectrumReady.load()) {
        std::copy (proc.spectrumFifo.begin(), proc.spectrumFifo.end(), fftData.begin());
        std::fill (fftData.begin() + NectarineProcessor::fftSize, fftData.end(), 0.f);
        proc.fifoIndex = 0; proc.spectrumReady = false;
        window.multiplyWithWindowingTable (fftData.data(), (size_t) NectarineProcessor::fftSize);
        fft.performFrequencyOnlyForwardTransform (fftData.data());
        const double sr = juce::jmax (8000.0, proc.getSampleRate());
        for (int i = 0; i < bins; ++i) {
            float f0 = 40.f * std::pow (20000.f / 40.f, (float) i / bins), f1 = 40.f * std::pow (20000.f / 40.f, (float) (i + 1) / bins);
            int k0 = juce::jlimit (1, NectarineProcessor::fftSize / 2 - 1, (int) (f0 * NectarineProcessor::fftSize / sr));
            int k1 = juce::jlimit (k0 + 1, NectarineProcessor::fftSize / 2, (int) (f1 * NectarineProcessor::fftSize / sr) + 1);
            float m = 0; for (int k = k0; k < k1; ++k) m = juce::jmax (m, fftData[(size_t) k]);
            float db = juce::Decibels::gainToDecibels (m / (NectarineProcessor::fftSize * 0.25f), -100.f);
            float tgt = juce::jmap (db + (float) (6.0 * std::log2 (f0 / 200.f + 1.0)), -80.f, -5.f, 0.f, 1.f);
            curve[(size_t) i] = 0.6f * curve[(size_t) i] + 0.4f * juce::jlimit (0.f, 1.f, tgt);
        }
        repaint (hexArea);
    }
}
