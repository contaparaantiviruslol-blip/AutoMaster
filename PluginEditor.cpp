#include "PluginEditor.h"

namespace
{
const juce::Colour kBg     = juce::Colour (0xff14161a);
const juce::Colour kPanel  = juce::Colour (0xff1e2127);
const juce::Colour kAccent = juce::Colour (0xff3fd0b8);
const juce::Colour kWarn   = juce::Colour (0xffe8a33d);
}

AutoMasterEditor::AutoMasterEditor (AutoMasterProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setSize (780, 560);

    getLookAndFeel().setColour (juce::Slider::rotarySliderFillColourId, kAccent);
    getLookAndFeel().setColour (juce::Slider::thumbColourId, juce::Colours::white);
    getLookAndFeel().setColour (juce::TextButton::buttonColourId, kPanel);

    for (int i = 0; i < am::kNumPresets; ++i)
        presetBox.addItem (am::kPresets[i].name, i + 1);
    presetBox.setSelectedItemIndex ((int) *proc.apvts.getRawParameterValue (ids::preset), juce::dontSendNotification);
    presetBox.onChange = [this] { proc.loadPreset (presetBox.getSelectedItemIndex()); };
    addAndMakeVisible (presetBox);

    analyseBtn.onClick = [this]
    {
        if (proc.isAnalysing())
        {
            proc.stopAnalysis();
            waiting = true; waitTicks = 0;
            reportLabel.setText ("Working out the genre and settings...", juce::dontSendNotification);
        }
        else
        {
            proc.startAnalysis();
            waiting = false;
            reportLabel.setText ("Analysing... press Play in FL Studio and let 10-60 s of the loudest part of the track run, "
                                 "then press 'Stop + apply'.", juce::dontSendNotification);
        }
    };
    matchBtn.onClick = [this] { reportLabel.setText (proc.matchTarget(), juce::dontSendNotification); };
    resetBtn.onClick = [this] { proc.resetMeters(); };
    addAndMakeVisible (analyseBtn);
    addAndMakeVisible (matchBtn);
    addAndMakeVisible (resetBtn);

    reportLabel.setJustificationType (juce::Justification::topLeft);
    reportLabel.setMinimumHorizontalScale (1.0f);
    reportLabel.setText ("1) Put AutoMaster on the Master channel   2) Press Analyse and play the track   3) Press Stop + apply   "
                         "4) Play it all once, press Match target   5) Fine-tune by ear (compare with Streaming Safe).",
                         juce::dontSendNotification);
    addAndMakeVisible (reportLabel);

    addKnob (kTarget, ids::target,   "Target LUFS");
    addKnob (kDrive,  ids::drive,    "Drive");
    addKnob (kLow,    ids::low,      "Low");
    addKnob (kPres,   ids::presence, "Presence");
    addKnob (kAir,    ids::air,      "Air");
    addKnob (kCrisp,  ids::crisp,    "Crisp");
    addKnob (kGlue,   ids::glue,     "Glue");
    addKnob (kWidth,  ids::width,    "Width");
    addKnob (kClip,   ids::clip,     "Clipper");
    addKnob (kCeil,   ids::ceiling,  "Ceiling");
    addKnob (kRel,    ids::release,  "Release");

    startTimerHz (30);
}

AutoMasterEditor::~AutoMasterEditor() { stopTimer(); }

void AutoMasterEditor::addKnob (Knob& k, const char* id, const juce::String& name)
{
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 20);
    k.slider.setDoubleClickReturnValue (true, proc.apvts.getParameter (id)->convertFrom0to1 (proc.apvts.getParameter (id)->getDefaultValue()));
    k.label.setText (name, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (k.slider);
    addAndMakeVisible (k.label);
    k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, k.slider);
}

void AutoMasterEditor::resized()
{
    auto r = getLocalBounds().reduced (16);
    auto top = r.removeFromTop (34);
    top.removeFromLeft (150);
    presetBox.setBounds (top.removeFromLeft (230));
    top.removeFromLeft (12);
    analyseBtn.setBounds (top.removeFromLeft (120));
    top.removeFromLeft (8);
    matchBtn.setBounds (top.removeFromLeft (110));
    top.removeFromLeft (8);
    resetBtn.setBounds (top.removeFromLeft (110));

    r.removeFromTop (8);
    reportLabel.setBounds (r.removeFromTop (74));
    r.removeFromBottom (76);                                 // space for meters (painted)

    Knob* row1[] = { &kTarget, &kDrive, &kLow, &kPres, &kAir, &kCrisp };
    Knob* row2[] = { &kGlue, &kWidth, &kClip, &kCeil, &kRel };
    const int rowH = r.getHeight() / 2;

    auto layoutRow = [] (juce::Rectangle<int> area, Knob* const* ks, int n)
    {
        const int w = area.getWidth() / 6;
        auto left = area.getX() + (area.getWidth() - w * n) / 2;
        for (int i = 0; i < n; ++i)
        {
            auto cell = juce::Rectangle<int> (left + i * w, area.getY(), w, area.getHeight());
            ks[i]->label.setBounds (cell.removeFromTop (20));
            ks[i]->slider.setBounds (cell.reduced (4));
        }
    };
    layoutRow (r.removeFromTop (rowH), row1, 6);
    layoutRow (r, row2, 5);
}

void AutoMasterEditor::drawBar (juce::Graphics& g, juce::Rectangle<float> area, float v01, juce::Colour c)
{
    g.setColour (juce::Colour (0xff2a2e36));
    g.fillRoundedRectangle (area, 3.f);
    g.setColour (c);
    g.fillRoundedRectangle (area.withWidth (area.getWidth() * juce::jlimit (0.f, 1.f, v01)), 3.f);
}

void AutoMasterEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBg);
    g.setColour (kAccent);
    g.setFont (juce::FontOptions (24.f, juce::Font::bold));
    g.drawText ("AutoMaster", 16, 14, 150, 30, juce::Justification::centredLeft);

    auto m = getLocalBounds().reduced (16).removeFromBottom (68).toFloat();
    g.setColour (kPanel);
    g.fillRoundedRectangle (m, 6.f);
    m = m.reduced (12, 8);

    auto fmt = [] (float v) { return v < -99.f ? juce::String ("--") : juce::String (v, 1); };
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (14.f));
    g.drawText ("Output loudness   Momentary " + fmt (proc.mMomentary.load()) + "   Short-term " + fmt (proc.mShort.load())
                + "   Integrated " + fmt (proc.mIntegrated.load()) + " LUFS   (target "
                + juce::String (proc.apvts.getRawParameterValue (ids::target)->load(), 1) + ")",
                m.removeFromTop (20), juce::Justification::centredLeft);

    const float pk = proc.mPeakDb.load(), gr = proc.mGrDb.load();
    auto row = m.removeFromTop (16);
    auto left = row.removeFromLeft (row.getWidth() / 2 - 8);
    row.removeFromLeft (16);
    g.setFont (juce::FontOptions (12.f));
    g.setColour (juce::Colours::lightgrey);
    g.drawText ("Peak " + fmt (pk) + " dBFS", left.removeFromLeft (110), juce::Justification::centredLeft);
    drawBar (g, left.reduced (0, 4), (pk + 30.f) / 30.f, pk > -1.0f ? kWarn : kAccent);
    g.drawText ("Limiter " + juce::String (gr, 1) + " dB", row.removeFromLeft (110), juce::Justification::centredLeft);
    drawBar (g, row.reduced (0, 4), gr / 12.f, kWarn);
}

void AutoMasterEditor::timerCallback()
{
    const int idx = (int) *proc.apvts.getRawParameterValue (ids::preset);
    if (presetBox.getSelectedItemIndex() != idx)
        presetBox.setSelectedItemIndex (idx, juce::dontSendNotification);

    analyseBtn.setButtonText (proc.isAnalysing() ? "Stop + apply" : "Analyse");

    am::Features f;
    if (proc.takeAnalysisResult (f))
    {
        waiting = false;
        reportLabel.setText (proc.applyAnalysis (f), juce::dontSendNotification);
    }
    else if (waiting && ++waitTicks > 90)
    {
        waiting = false;
        reportLabel.setText ("No audio reached the plugin. Press Play in FL Studio while analysing, then try again.",
                             juce::dontSendNotification);
    }
    else if (proc.isAnalysing())
    {
        reportLabel.setText ("Analysing... " + juce::String (proc.mAnalysisSeconds.load(), 0)
                             + " s collected. Let the loudest part of the track play, then press 'Stop + apply'.",
                             juce::dontSendNotification);
    }
    repaint();
}
