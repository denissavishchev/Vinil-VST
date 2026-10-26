#pragma once
#include "PluginProcessor.h"

// palette of the reference: navy blueprint with gold line-art
namespace theme
{
    const juce::Colour navy    { 0xff262c3b };
    const juce::Colour navyHi  { 0xff2d3547 };
    const juce::Colour navyLo  { 0xff1f2433 };
    const juce::Colour gold    { 0xffbb8c52 };
    const juce::Colour goldHi  { 0xffd9aa6c };
}

class VinilLookAndFeel : public juce::LookAndFeel_V4
{
public:
    VinilLookAndFeel();

    int getSliderThumbRadius (juce::Slider&) override { return 14; }   // room around the ball: it is never cut off by the frame
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float sliderPos,
                           float minPos, float maxPos, juce::Slider::SliderStyle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
    void drawLabel (juce::Graphics&, juce::Label&) override;
    juce::Font getLabelFont (juce::Label&) override;
    juce::Font getPopupMenuFont() override;
};

// MPC style pad
class PadButton : public juce::Button
{
public:
    explicit PadButton (const juce::String& name) : juce::Button (name) {}
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
};

// waveform of the incoming audio + the speed curve: the top panel
class CurveView : public juce::Component, private juce::Timer
{
public:
    explicit CurveView (VinilProcessor&);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    void timerCallback() override { repaint(); }
    juce::Rectangle<float> area() const { return getLocalBounds().toFloat().reduced (6.0f); }
    float xToPix (float x) const { auto r = area(); return r.getX() + x * r.getWidth(); }
    float uToPix (float u) const { auto r = area().reduced (0.0f, 10.0f); return r.getCentreY() - u * r.getHeight() * 0.5f; }
    float pixToX (float px) const { auto r = area(); return juce::jlimit (0.0f, 1.0f, (px - r.getX()) / r.getWidth()); }
    float pixToU (float py) const { auto r = area().reduced (0.0f, 10.0f); return juce::jlimit (-1.0f, 1.0f, (r.getCentreY() - py) / (r.getHeight() * 0.5f)); }
    float snapX (float x) const;
    static float snapU (float u);
    int hitPoint (juce::Point<float>) const;
    void addPoint (juce::Point<float> pixel, bool snap);

    VinilProcessor& proc;
    int dragIndex = -1;
    juce::String hover;
};

// the record: grab it and scratch
class TurntableView : public juce::Component, private juce::Timer
{
public:
    explicit TurntableView (VinilProcessor&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    void timerCallback() override { repaint(); }
    float angleAt (juce::Point<float>) const;
    bool inside (juce::Point<float>) const;

    VinilProcessor& proc;
    bool held = false;
    float lastAngle = 0.0f;
};

// MPC style LCD: seven-segment speed readout and the tempo
class LcdView : public juce::Component, private juce::Timer
{
public:
    explicit LcdView (VinilProcessor&);
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override { repaint(); }
    VinilProcessor& proc;
};

class VinilEditor : public juce::AudioProcessorEditor
{
public:
    explicit VinilEditor (VinilProcessor&);
    ~VinilEditor() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

private:
    void applyShape (int idx);

    VinilProcessor& proc;
    VinilLookAndFeel laf;

    CurveView curveView;
    TurntableView turntable;
    LcdView lcd;

    juce::Slider loopSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> loopAtt;
    juce::TextButton snapButton { "SNAP" }, smoothButton { "SMOOTH" };
    std::unique_ptr<juce::ButtonParameterAttachment> snapAtt, smoothAtt;

    struct Knob
    {
        juce::String name;
        juce::Slider slider;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attach;
    };
    std::array<Knob, 4> knobs;
    juce::OwnedArray<PadButton> shapePads;

    juce::Rectangle<int> topPanel, turnPanel, mpcPanel, barArea;
    juce::Array<juce::Rectangle<int>> captions;
    juce::StringArray captionTexts;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VinilEditor)
};
