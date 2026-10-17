#pragma once
// 实验性 10 段 biquad EQ：只做 DSP，不依赖 QtMultimedia，便于单测和离线测量。
// 处理交错 float PCM；频段固定 31..16k，增益 -12..+12 dB。
#include <array>
#include <cstddef>

class EqProcessor {
public:
    static constexpr int Bands = 10;
    static constexpr int MaxChannels = 2;

    EqProcessor();

    void configure(int sampleRate, int channels);
    void setGain(int band, double db);
    double gain(int band) const;
    // 低音增强（低架，120Hz）与立体声宽度（0 单声道 .. 1 原样 .. 2 加宽）
    void setBassBoost(double db);
    double bassBoost() const { return m_bassGain; }
    void setStereoWidth(double width);
    double stereoWidth() const { return m_width; }
    bool neutral() const;
    void process(float *interleaved, std::size_t frames);

private:
    struct Biquad {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        double z1[MaxChannels]{};
        double z2[MaxChannels]{};
        void reset();
    };

    void update(int band);
    void updateBass();

    int m_sampleRate = 48000;
    int m_channels = 2;
    std::array<double, Bands> m_gains{};
    std::array<Biquad, Bands> m_bands{};
    Biquad m_bass;
    double m_bassGain = 0;
    double m_width = 1;
};
