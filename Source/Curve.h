#pragma once
#include <juce_core/juce_core.h>
#include <vector>
#include <cmath>

// One breakpoint of the speed curve. x = position inside the loop 0..1,
// y = playback speed divided by the Range setting, -1..1 (so +-1 is always the top / bottom of the display).
struct CurvePoint { float x, y; };

class CurveModel
{
public:
    static constexpr int lutSize = 1024;
    static constexpr float minGap = 0.002f;

    CurveModel() { reset (0.5f); }

    void reset (float u) { pts = { { 0.0f, u }, { 1.0f, u } }; }

    // sample the curve into out[0..n-1]; smooth = monotone cubic (no overshoot between points), otherwise straight lines
    void bake (float* out, int n, bool smooth) const
    {
        const int np = (int) pts.size();
        if (np < 2) { for (int i = 0; i < n; ++i) out[i] = np == 1 ? pts[0].y : 0.0f; return; }

        std::vector<float> h ((size_t) np - 1), d ((size_t) np - 1), m ((size_t) np, 0.0f);
        for (int k = 0; k < np - 1; ++k)
        {
            h[(size_t) k] = juce::jmax (1.0e-6f, pts[(size_t) k + 1].x - pts[(size_t) k].x);
            d[(size_t) k] = (pts[(size_t) k + 1].y - pts[(size_t) k].y) / h[(size_t) k];
        }
        if (smooth)
        {
            m[0] = d[0];
            m[(size_t) np - 1] = d[(size_t) np - 2];
            for (int k = 1; k < np - 1; ++k)
            {
                const float a = d[(size_t) k - 1], b = d[(size_t) k];
                if (a * b <= 0.0f) { m[(size_t) k] = 0.0f; continue; }
                const float w1 = 2.0f * h[(size_t) k] + h[(size_t) k - 1];
                const float w2 = h[(size_t) k] + 2.0f * h[(size_t) k - 1];
                m[(size_t) k] = (w1 + w2) / (w1 / a + w2 / b);
            }
        }

        int seg = 0;
        for (int i = 0; i < n; ++i)
        {
            const float x = n > 1 ? (float) i / (float) (n - 1) : 0.0f;
            while (seg < np - 2 && x > pts[(size_t) seg + 1].x) ++seg;

            const auto& p0 = pts[(size_t) seg];
            const auto& p1 = pts[(size_t) seg + 1];
            const float hh = h[(size_t) seg];
            const float t = juce::jlimit (0.0f, 1.0f, (x - p0.x) / hh);
            float y;
            if (smooth)
            {
                const float t2 = t * t, t3 = t2 * t;
                y = (2 * t3 - 3 * t2 + 1) * p0.y + (t3 - 2 * t2 + t) * hh * m[(size_t) seg]
                    + (-2 * t3 + 3 * t2) * p1.y + (t3 - t2) * hh * m[(size_t) seg + 1];
            }
            else
                y = p0.y + (p1.y - p0.y) * t;
            out[i] = juce::jlimit (-1.0f, 1.0f, y);
        }
    }

    juce::String toString() const
    {
        juce::String s;
        for (auto& p : pts)
            s << juce::String (p.x, 5) << "," << juce::String (p.y, 5) << ";";
        return s;
    }

    void fromString (const juce::String& s)
    {
        std::vector<CurvePoint> v;
        for (auto& tok : juce::StringArray::fromTokens (s, ";", ""))
        {
            auto xy = juce::StringArray::fromTokens (tok, ",", "");
            if (xy.size() == 2)
                v.push_back ({ juce::jlimit (0.0f, 1.0f, xy[0].getFloatValue()), juce::jlimit (-1.0f, 1.0f, xy[1].getFloatValue()) });
        }
        if (v.size() < 2 || v.front().x > 0.0001f || v.back().x < 0.9999f) { reset (0.5f); return; }
        for (size_t i = 1; i < v.size(); ++i)
            if (v[i].x <= v[i - 1].x) { reset (0.5f); return; }
        pts = std::move (v);
    }

    std::vector<CurvePoint> pts;
};

// ready-made curve shapes (the pads); speeds are in x-speed units, scaled to the current Range
namespace shapes
{
    inline const char* const names[12] = { "FLAT", "SINE", "TRI", "SAW", "SQR", "STOP", "START", "REV", "BABY", "CHIRP", "GATE", "PULSE" };

    inline std::vector<CurvePoint> make (int idx, float range)
    {
        constexpr float twoPi = juce::MathConstants<float>::twoPi;
        std::vector<CurvePoint> v;
        auto add = [&] (float x, float speed) { v.push_back ({ x, juce::jlimit (-1.0f, 1.0f, speed / range) }); };
        auto sampled = [&] (int n, auto f) { for (int i = 0; i <= n; ++i) { const float x = (float) i / (float) n; add (x, f (x)); } };
        auto smoothstep = [] (float t) { t = juce::jlimit (0.0f, 1.0f, t); return t * t * (3.0f - 2.0f * t); };

        switch (idx)
        {
            case 0:  add (0.0f, 1.0f); add (1.0f, 1.0f); break;
            case 1:  sampled (16, [&] (float x) { return 1.5f * std::sin (twoPi * x); }); break;
            case 2:  add (0.0f, 0.0f); add (0.25f, 1.5f); add (0.75f, -1.5f); add (1.0f, 0.0f); break;
            case 3:  add (0.0f, -1.5f); add (1.0f, 1.5f); break;
            case 4:  add (0.0f, 1.5f); add (0.498f, 1.5f); add (0.502f, -1.5f); add (1.0f, -1.5f); break;
            case 5:  sampled (10, [&] (float x) { const float t = juce::jmax (0.0f, 1.0f - x / 0.7f); return t * t; }); break;
            case 6:  sampled (10, [&] (float x) { return smoothstep (x / 0.7f); }); break;
            case 7:  add (0.0f, -1.0f); add (1.0f, -1.0f); break;
            case 8:  sampled (24, [&] (float x) { return 1.6f * std::sin (twoPi * 2.0f * x); }); break;
            case 9:  sampled (32, [&] (float x) { return 1.2f * std::sin (twoPi * 4.0f * x); }); break;
            case 10:
                for (int k = 0; k < 8; ++k)
                {
                    const float s = (k % 2 == 0) ? 1.0f : 0.0f;
                    add ((float) k / 8.0f + (k > 0 ? 0.003f : 0.0f), s);
                    add ((float) (k + 1) / 8.0f - (k < 7 ? 0.003f : 0.0f), s);
                }
                break;
            default: sampled (36, [&] (float x) { return 1.6f * std::abs (std::sin (juce::MathConstants<float>::pi * 3.0f * x)); }); break;
        }
        return v;
    }
}
