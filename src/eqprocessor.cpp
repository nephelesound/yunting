#include "eqprocessor.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr double kFrequencies[EqProcessor::Bands] = {
    31, 62, 125, 250, 500, 1000, 2000, 4000, 8000, 16000
};
constexpr double kQ = 1.2;
} // namespace

void EqProcessor::Biquad::reset()
{
    for (int c = 0; c < MaxChannels; ++c)
        z1[c] = z2[c] = 0;
}

EqProcessor::EqProcessor() = default;

void EqProcessor::configure(int sampleRate, int channels)
{
    m_sampleRate = std::max(8000, sampleRate);
    m_channels = std::clamp(channels, 1, MaxChannels);
    for (int i = 0; i < Bands; ++i) {
        m_bands[i].reset();
        update(i);
    }
    m_bass.reset();
    updateBass();
}

void EqProcessor::setGain(int band, double db)
{
    if (band < 0 || band >= Bands)
        return;
    m_gains[band] = std::clamp(db, -12.0, 12.0);
    update(band);
}

double EqProcessor::gain(int band) const
{
    return band >= 0 && band < Bands ? m_gains[band] : 0;
}

void EqProcessor::setBassBoost(double db)
{
    m_bassGain = std::clamp(db, -12.0, 12.0);
    updateBass();
}

void EqProcessor::setStereoWidth(double width)
{
    m_width = std::clamp(width, 0.0, 2.0);
}

bool EqProcessor::neutral() const
{
    return m_bassGain == 0 && m_width == 1.0
        && std::all_of(m_gains.begin(), m_gains.end(), [](double g) { return g == 0; });
}

void EqProcessor::updateBass()
{
    if (m_bassGain == 0) {
        m_bass.b0 = 1;
        m_bass.b1 = m_bass.b2 = m_bass.a1 = m_bass.a2 = 0;
        m_bass.reset();
        return;
    }

    // RBJ low shelf, S = 1, f0 = 120 Hz
    const double A = std::pow(10.0, m_bassGain / 40.0);
    const double w0 = 2.0 * M_PI * 120.0 / m_sampleRate;
    const double cosw = std::cos(w0);
    const double alpha = std::sin(w0) / 2.0 * std::sqrt((A + 1.0 / A) * (1.0 / 1.0 - 1.0) + 2.0);
    const double twoSqrtAAlpha = 2.0 * std::sqrt(A) * alpha;

    const double a0 = (A + 1.0) + (A - 1.0) * cosw + twoSqrtAAlpha;
    m_bass.b0 = A * ((A + 1.0) - (A - 1.0) * cosw + twoSqrtAAlpha) / a0;
    m_bass.b1 = 2.0 * A * ((A - 1.0) - (A + 1.0) * cosw) / a0;
    m_bass.b2 = A * ((A + 1.0) - (A - 1.0) * cosw - twoSqrtAAlpha) / a0;
    m_bass.a1 = -2.0 * ((A - 1.0) + (A + 1.0) * cosw) / a0;
    m_bass.a2 = ((A + 1.0) + (A - 1.0) * cosw - twoSqrtAAlpha) / a0;
    m_bass.reset();
}

void EqProcessor::update(int band)
{
    const double gain = m_gains[band];
    if (gain == 0) {
        m_bands[band].b0 = 1;
        m_bands[band].b1 = m_bands[band].b2 = m_bands[band].a1 = m_bands[band].a2 = 0;
        m_bands[band].reset();
        return;
    }

    const double A = std::pow(10.0, gain / 40.0);
    const double w0 = 2.0 * M_PI * kFrequencies[band] / m_sampleRate;
    const double alpha = std::sin(w0) / (2.0 * kQ);
    const double cosw = std::cos(w0);
    const double a0 = 1.0 + alpha / A;

    auto &b = m_bands[band];
    b.b0 = (1.0 + alpha * A) / a0;
    b.b1 = (-2.0 * cosw) / a0;
    b.b2 = (1.0 - alpha * A) / a0;
    b.a1 = (-2.0 * cosw) / a0;
    b.a2 = (1.0 - alpha / A) / a0;
}

void EqProcessor::process(float *interleaved, std::size_t frames)
{
    if (!interleaved || neutral())
        return;

    for (std::size_t frame = 0; frame < frames; ++frame) {
        for (int ch = 0; ch < m_channels; ++ch) {
            double x = interleaved[frame * m_channels + ch];
            for (Biquad &b : m_bands) {
                const double y = b.b0 * x + b.z1[ch];
                b.z1[ch] = b.b1 * x - b.a1 * y + b.z2[ch];
                b.z2[ch] = b.b2 * x - b.a2 * y;
                x = y;
            }
            if (m_bassGain != 0) {
                const double y = m_bass.b0 * x + m_bass.z1[ch];
                m_bass.z1[ch] = m_bass.b1 * x - m_bass.a1 * y + m_bass.z2[ch];
                m_bass.z2[ch] = m_bass.b2 * x - m_bass.a2 * y;
                x = y;
            }
            interleaved[frame * m_channels + ch] = float(x);
        }
        if (m_channels == 2 && m_width != 1.0) {
            const double l = interleaved[frame * 2];
            const double r = interleaved[frame * 2 + 1];
            const double mid = (l + r) * 0.5;
            const double side = (l - r) * 0.5 * m_width;
            interleaved[frame * 2] = float(mid + side);
            interleaved[frame * 2 + 1] = float(mid - side);
        }
    }
}
