#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    constexpr float twoPi = juce::MathConstants<float>::twoPi;
    constexpr float ringSeconds = VinilProcessor::maxLoopSeconds + 10.0f;

    // 4-point Hermite read from a circular buffer at a fractional position
    inline float hermite (const float* d, int size, double pos)
    {
        pos = std::fmod (pos, (double) size);
        if (pos < 0.0) pos += (double) size;
        const int i1 = (int) std::floor (pos);
        const float f = (float) (pos - (double) i1);
        auto at = [&] (int i) { i %= size; if (i < 0) i += size; return d[i]; };
        const float y0 = at (i1 - 1), y1 = at (i1), y2 = at (i1 + 1), y3 = at (i1 + 2);
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * f + c2) * f + c1) * f + y1;
    }
}

VinilProcessor::VinilProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    for (auto& w : liveWave) w = 0.0f;
    lutLocal.fill (0.5f);
    apvts.addParameterListener ("smooth", this);
    curveEdited();
}

VinilProcessor::~VinilProcessor()
{
    cancelPendingUpdate();
    apvts.removeParameterListener ("smooth", this);
}

juce::AudioProcessorValueTreeState::ParameterLayout VinilProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto id = [] (const char* s) { return ParameterID { s, 1 }; };

    l.add (std::make_unique<AudioParameterFloat> (id ("loop"), "Loop", NormalisableRange<float> (5.0f, maxLoopSeconds, 1.0f), 10.0f,
                                                  AudioParameterFloatAttributes().withLabel ("s")));
    l.add (std::make_unique<AudioParameterBool> (id ("snap"), "Snap", true));
    l.add (std::make_unique<AudioParameterBool> (id ("smooth"), "Smooth", true));
    l.add (std::make_unique<AudioParameterFloat> (id ("mix"), "Mix", NormalisableRange<float> (0.0f, 1.0f, 0.001f), 1.0f));
    l.add (std::make_unique<AudioParameterFloat> (id ("weight"), "Weight", NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.4f));
    l.add (std::make_unique<AudioParameterFloat> (id ("drag"), "Drag", NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.35f));
    l.add (std::make_unique<AudioParameterFloat> (id ("preroll"), "Preroll", NormalisableRange<float> (0.0f, 1000.0f, 1.0f, 0.5f), 0.0f,
                                                  AudioParameterFloatAttributes().withLabel ("ms")));
    return l;
}

bool VinilProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono())
           && out == l.getMainInputChannelSet();
}

void VinilProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;
    ring.setSize (2, (int) ((double) ringSeconds * sr) + 32);
    ring.clear();
    writePos = 0;
    heads[0] = heads[1] = 0.0;
    fade = 1.0f;
    xfading = false;
    spd = 1.0f;
    debt = 0.0;
    lastPhase = 0.0;
    lastBin = -1;
    lutVersionSeen = -1;
    lutLocal.fill (0.5f);
    pendingAngle = 0.0f;
}

//==============================================================================
void VinilProcessor::curveEdited()
{
    std::array<float, CurveModel::lutSize> tmp;
    curve.bake (tmp.data(), CurveModel::lutSize, *apvts.getRawParameterValue ("smooth") > 0.5f);
    {
        const juce::SpinLock::ScopedLockType sl (lutLock);
        lutShared = tmp;
    }
    ++curveVersion;
}

//==============================================================================
void VinilProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numCh = juce::jmin (buffer.getNumChannels(), 2);
    const int n = buffer.getNumSamples();
    const int ringSize = ring.getNumSamples();
    const double fs = sr;

    auto param = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    const float mix      = param ("mix");
    const float weight   = param ("weight");
    const float drag     = param ("drag");
    const double prerollSamples = (double) param ("preroll") * 0.001 * fs;
    const double loopSeconds = (double) param ("loop");

    // host tempo and position: the loop window is locked to the project timeline
    double bpm = 120.0, hostPpq = 0.0;
    bool usePpq = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) bpm = juce::jlimit (20.0, 400.0, *b);
            if (pos->getIsPlaying())
                if (auto p = pos->getPpqPosition()) { hostPpq = *p; usePpq = true; }
        }
    const double ppqBase = usePpq ? hostPpq : internalPpq;
    const double ppqInc = bpm / 60.0 / fs;
    const double cycleBeats = juce::jmax (0.01, loopSeconds * bpm / 60.0);

    if (lutVersionSeen != curveVersion.load())
    {
        const juce::SpinLock::ScopedTryLockType tl (lutLock);
        if (tl.isLocked()) { lutLocal = lutShared; lutVersionSeen = curveVersion.load(); }
    }

    // turntable input: the mouse moves a target and the record follows it with a lag set by Weight
    debt += (double) pendingAngle.exchange (0.0f) * secondsPerRev * fs / (double) twoPi;
    const bool isGrabbed = grabbed.load();

    const double tauGrab = 0.003 + 0.05 * (double) weight;
    const float kGrab = (float) (1.0 - std::exp (-1.0 / (tauGrab * fs)));
    const double tauCurve = 0.002 + 0.3 * (double) drag * (double) drag;
    const float kCurve = (float) (1.0 - std::exp (-1.0 / (tauCurve * fs)));
    const float xfadeInc = (float) (1.0 / (0.006 * fs));
    const double maxLag = (double) ringSize - 16.0;

    auto jumpTo = [&] (double target)
    {
        if (std::abs (target - heads[0]) < 1.0) return;
        heads[1] = heads[0];
        heads[0] = target;
        fade = 0.0f;
        xfading = true;
    };
    auto readHead = [&] (int ch, double lag)
    {
        return hermite (ring.getReadPointer (ch), ringSize, (double) writePos - lag - 2.0);
    };

    double ph = lastPhase;
    for (int i = 0; i < n; ++i)
    {
        // loop clock
        ph = std::fmod ((ppqBase + (double) i * ppqInc) / cycleBeats, 1.0);
        if (ph < 0.0) ph += 1.0;
        const bool wrapped = ph < lastPhase;
        lastPhase = ph;

        float inAbs = 0.0f;
        for (int ch = 0; ch < numCh; ++ch)
        {
            const float s = buffer.getSample (ch, i);
            ring.setSample (ch, writePos, s);
            inAbs = juce::jmax (inAbs, std::abs (s));
        }
        const int bin = juce::jlimit (0, waveBins - 1, (int) (ph * waveBins));
        if (bin != lastBin) { binAccum = 0.0f; lastBin = bin; }
        binAccum = juce::jmax (binAccum, inAbs);
        liveWave[(size_t) bin].store (binAccum, std::memory_order_relaxed);

        // ---- speed -------------------------------------------------------
        const bool following = isGrabbed || std::abs (debt) > 1.0;
        if (following)
        {
            const double step = debt * (double) kGrab;
            debt -= step;
            spd = (float) step;
        }
        else
        {
            // like a real record: let go and the platter returns to the speed of the curve (normal speed) and plays on from where it is
            const float fp = (float) ph * (float) (CurveModel::lutSize - 1);
            const int i0 = (int) fp;
            const float u = lutLocal[(size_t) i0] + (lutLocal[(size_t) juce::jmin (i0 + 1, CurveModel::lutSize - 1)] - lutLocal[(size_t) i0]) * (fp - (float) i0);
            spd += (u * speedRange - spd) * kCurve;
        }
        spd = juce::jlimit (-24.0f, 24.0f, spd);

        // ---- loop restart: the curve starts over and the head is re-synced -----
        if (wrapped && ! following)
            jumpTo (prerollSamples);

        // ---- move the heads (lag grows when slower than live, shrinks when faster) ----
        for (int k = 0; k < (xfading ? 2 : 1); ++k)
            heads[k] = juce::jlimit (0.0, maxLag, heads[k] + 1.0 - (double) spd);

        // ---- read and mix ------------------------------------------------
        const float holdGain = juce::jlimit (0.0f, 1.0f, std::abs (spd) * 20.0f);   // a record that stands still: silence, not a DC step
        for (int ch = 0; ch < numCh; ++ch)
        {
            const float dry = ring.getSample (ch, (writePos - 2 + ringSize) % ringSize);
            float wet = readHead (ch, heads[0]);
            if (xfading)
                wet = readHead (ch, heads[1]) * (1.0f - fade) + wet * fade;
            wet *= holdGain;
            buffer.setSample (ch, i, dry * (1.0f - mix) + wet * mix);
        }
        if (xfading)
        {
            fade += xfadeInc;
            if (fade >= 1.0f) { fade = 1.0f; xfading = false; }
        }

        platterAng += (double) spd * (double) twoPi / (secondsPerRev * fs);
        if (++writePos >= ringSize) writePos = 0;
    }

    internalPpq = ppqBase + (double) n * ppqInc;
    platterAng = std::fmod (platterAng, (double) twoPi);
    if (platterAng < 0.0) platterAng += (double) twoPi;

    cyclePhase = (float) ph;
    speedNow = spd;
    platterAngle = (float) platterAng;
    lagSeconds = (float) (heads[0] / fs);
    bpmNow = (float) bpm;

    for (int ch = numCh; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, n);
}

//==============================================================================
juce::AudioProcessorEditor* VinilProcessor::createEditor()
{
    return new VinilEditor (*this);
}

void VinilProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty ("curve", curve.toString(), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, dest);
}

void VinilProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;

    apvts.replaceState (juce::ValueTree::fromXml (*xml));
    curve.fromString (apvts.state.getProperty ("curve").toString());
    curveEdited();
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new VinilProcessor(); }
