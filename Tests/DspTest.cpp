// Offline checks of the scratch engine: no clicks, no NaN, right direction, catch-up and the curve.
#include "../Source/PluginEditor.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (! (cond)) { ++failures; std::printf ("FAIL: %s\n", msg); } else std::printf ("ok:   %s\n", msg); } while (0)

static void setParam (VinilProcessor& p, const char* id, float denorm)
{
    auto* prm = p.apvts.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (denorm));
}

struct Run
{
    std::vector<float> out;
    float maxJump = 0.0f;
    bool bad = false;
};

static Run render (VinilProcessor& p, int blocks, int bs, double sr, std::function<void (int)> perBlock = {})
{
    Run r;
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> buf (2, bs);
    long t = 0;
    float prev = 0.0f;
    for (int b = 0; b < blocks; ++b)
    {
        if (perBlock) perBlock (b);
        for (int i = 0; i < bs; ++i, ++t)
        {
            const float s = 0.5f * std::sin ((float) (juce::MathConstants<double>::twoPi * 440.0 * (double) t / sr));
            buf.setSample (0, i, s);
            buf.setSample (1, i, s);
        }
        p.processBlock (buf, midi);
        for (int i = 0; i < bs; ++i)
        {
            const float o = buf.getSample (0, i);
            if (! std::isfinite (o) || std::abs (o) > 2.0f) r.bad = true;
            if (t > 0) r.maxJump = juce::jmax (r.maxJump, std::abs (o - prev));
            prev = o;
            r.out.push_back (o);
        }
    }
    return r;
}

static void snapshot (const char* path)
{
    juce::ScopedJuceInitialiser_GUI gui;
    VinilProcessor p;
    p.prepareToPlay (48000.0, 256);
    p.curve.pts = shapes::make (1, 2.0f);
    for (int b = 0; b < 200; ++b)
    {
        juce::AudioBuffer<float> buf (2, 256);
        juce::MidiBuffer m;
        for (int i = 0; i < 256; ++i)
        {
            const float v = 0.5f * std::sin (0.06f * (float) (b * 256 + i)) * (0.4f + 0.6f * std::sin (0.0007f * (float) (b * 256 + i)));
            buf.setSample (0, i, v);
            buf.setSample (1, i, v);
        }
        p.processBlock (buf, m);
    }
    p.curveEdited();
    p.addPlatterAngle (0.4f);
    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
    juce::Thread::sleep (50);
    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
    juce::File (path).deleteFile();
    juce::PNGImageFormat png;
    juce::FileOutputStream os { juce::File (path) };
    png.writeImageToStream (img, os);
}

int main (int argc, char** argv)
{
    if (argc > 2 && juce::String (argv[1]) == "--snapshot") { snapshot (argv[2]); return 0; }

    const double sr = 48000.0;
    const int bs = 256;

    // 1. transparent when untouched
    {
        VinilProcessor p;
        p.prepareToPlay (sr, bs);
        auto r = render (p, 100, bs, sr);
        float err = 0.0f;
        for (size_t i = 100; i < r.out.size(); ++i)
        {
            const float expect = 0.5f * std::sin ((float) (juce::MathConstants<double>::twoPi * 440.0 * (double) ((long) i - 2) / sr));
            err = juce::jmax (err, std::abs (r.out[i] - expect));
        }
        std::printf ("      untouched max error %.6f\n", err);
        CHECK (err < 1.0e-3f && ! r.bad, "untouched is transparent (2-sample delay)");
    }

    // 2. scratching: forward and back, no clicks; like a real record it plays on from where it was left
    {
        VinilProcessor p;
        p.prepareToPlay (sr, bs);
        float lagMax = 0.0f;
        auto r = render (p, 400, bs, sr, [&] (int b)
        {
            if (b == 40) p.setPlatterGrabbed (true);
            if (b >= 40 && b < 80)  p.addPlatterAngle (-0.05f);   // pull back
            if (b >= 80 && b < 120) p.addPlatterAngle (0.06f);    // push forward
            if (b == 120) p.setPlatterGrabbed (false);
            lagMax = juce::jmax (lagMax, p.lagSeconds.load());
        });
        std::printf ("      scratch max jump %.4f, max lag %.3fs, final lag %.4fs\n", r.maxJump, lagMax, p.lagSeconds.load());
        CHECK (! r.bad, "scratching stays finite and bounded");
        CHECK (r.maxJump < 0.13f, "scratching has no clicks (sample-to-sample jump)");
        CHECK (lagMax > 0.3f, "pulling the record back makes the head lag behind live");
        CHECK (std::abs (p.speedNow.load() - 1.0f) < 0.02f, "after release the platter is back at normal speed");
        CHECK (p.lagSeconds.load() > 0.0f, "and it plays on from where it was left (no jump back to live)");
    }

    // 3. holding the record still stops the sound
    {
        VinilProcessor p;
        p.prepareToPlay (sr, bs);
        auto r = render (p, 100, bs, sr, [&] (int b) { if (b == 20) p.setPlatterGrabbed (true); });
        float tail = 0.0f;
        for (size_t i = r.out.size() - 2000; i < r.out.size(); ++i) tail = juce::jmax (tail, std::abs (r.out[i]));
        CHECK (tail < 0.01f, "holding the record still silences the output");
    }

    // 4. curve STOP over a 5 s loop: finite, smooth, speed falls to zero
    {
        VinilProcessor p;
        p.prepareToPlay (sr, bs);
        setParam (p, "loop", 5.0f);
        p.curve.pts = shapes::make (5, 2.0f);
        p.curveEdited();
        float minSpeed = 9.0f;
        auto r = render (p, 1000, bs, sr, [&] (int) { minSpeed = juce::jmin (minSpeed, p.speedNow.load()); });
        std::printf ("      stop curve: min speed %.3f, max jump %.4f\n", minSpeed, r.maxJump);
        CHECK (! r.bad && r.maxJump < 0.2f, "curve STOP is finite and click-free across loop restarts");
        CHECK (minSpeed < 0.05f, "curve STOP drives the speed to ~0");
    }

    // 5. curve REV: speed settles at -1
    {
        VinilProcessor p;
        p.prepareToPlay (sr, bs);
        p.curve.pts = shapes::make (7, 2.0f);
        p.curveEdited();
        render (p, 100, bs, sr);
        std::printf ("      reverse speed %.3f\n", p.speedNow.load());
        CHECK (std::abs (p.speedNow.load() + 1.0f) < 0.02f, "curve REV gives speed -1");
    }

    // 6. state round trip keeps the curve
    {
        VinilProcessor p;
        p.curve.pts = shapes::make (8, 2.0f);
        juce::MemoryBlock mb;
        p.getStateInformation (mb);
        VinilProcessor q;
        q.setStateInformation (mb.getData(), (int) mb.getSize());
        CHECK (q.curve.pts.size() == p.curve.pts.size(), "state round trip keeps the curve");
    }

    // 7. LOOP is a time window: 10 s by default, 30 s at most; the waveform fills the whole window
    {
        VinilProcessor p;
        p.prepareToPlay (sr, bs);
        auto r = render (p, (int) (10.3 * sr / bs), bs, sr);
        int filled = 0;
        for (int i = 0; i < VinilProcessor::waveBins; ++i) if (p.liveWave[(size_t) i].load() > 0.05f) ++filled;
        std::printf ("      window %.2f s, %d/%d wave bins filled\n", p.getCycleSeconds(), filled, VinilProcessor::waveBins);
        CHECK (std::abs (p.getCycleSeconds() - 10.0) < 1.0e-6, "default LOOP window is 10 s");
        CHECK (filled > VinilProcessor::waveBins * 9 / 10 && ! r.bad, "the whole window shows up in the waveform");

        setParam (p, "loop", 30.0f);
        CHECK (std::abs (p.getCycleSeconds() - 30.0) < 1.0e-6, "LOOP goes up to 30 s");
        setParam (p, "loop", 17.0f);
        CHECK (std::abs (p.getCycleSeconds() - 17.0) < 1.0e-6, "LOOP moves in 1 s steps");
    }

    std::printf (failures == 0 ? "\nALL PASSED\n" : "\n%d FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
