#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <vector>
#include "Curve.h"

class VinilProcessor : public juce::AudioProcessor,
                       private juce::AudioProcessorValueTreeState::Listener,
                       private juce::AsyncUpdater
{
public:
    VinilProcessor();
    ~VinilProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // 33 1/3 rpm: one turn of the platter = 1.8 s of audio at speed 1
    static constexpr double secondsPerRev = 1.8;
    static constexpr int waveBins = 512;
    static constexpr float maxLoopSeconds = 30.0f;

    static constexpr float speedRange = 2.0f;       // the curve goes from -2x (reverse) to +2x

    juce::AudioProcessorValueTreeState apvts;

    //==== message thread ====================================================
    CurveModel curve;
    void curveEdited();                         // bake + publish after any change to `curve`
    std::atomic<int> curveVersion { 0 };

    // turntable input from the editor
    void setPlatterGrabbed (bool g) { grabbed = g; }
    void addPlatterAngle (float radians)
    {
        float cur = pendingAngle.load();
        while (! pendingAngle.compare_exchange_weak (cur, cur + radians)) {}
    }

    double getCycleSeconds() const { return (double) apvts.getRawParameterValue ("loop")->load(); }

    //==== audio thread -> editor ============================================
    std::atomic<float> cyclePhase { 0.0f }, speedNow { 1.0f }, platterAngle { 0.0f }, lagSeconds { 0.0f }, bpmNow { 120.0f };
    std::array<std::atomic<float>, waveBins> liveWave;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void parameterChanged (const juce::String&, float) override { triggerAsyncUpdate(); }
    void handleAsyncUpdate() override { curveEdited(); }

    double sr = 44100.0;
    juce::AudioBuffer<float> ring;
    int writePos = 0;

    // curve LUT, published by the message thread, copied by the audio thread when the lock is free
    juce::SpinLock lutLock;
    std::array<float, CurveModel::lutSize> lutShared {}, lutLocal {};
    int lutVersionSeen = -1;

    std::atomic<bool> grabbed { false };
    std::atomic<float> pendingAngle { 0.0f };

    // engine state (audio thread)
    double heads[2] { 0.0, 0.0 };   // how far behind the live input the read head is (samples); [1] = outgoing head during a cross-fade
    float fade = 1.0f;
    bool xfading = false;
    float spd = 1.0f;
    double debt = 0.0;
    double internalPpq = 0.0, lastPhase = 0.0, platterAng = 0.0;
    int lastBin = -1;
    float binAccum = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VinilProcessor)
};
