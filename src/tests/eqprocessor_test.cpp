#include "../eqprocessor.h"

#include <QTest>
#include <cmath>
#include <vector>

class EqProcessorTest : public QObject {
    Q_OBJECT
private slots:
    void neutralIsBitExact()
    {
        EqProcessor eq;
        eq.configure(48000, 2);
        std::vector<float> data{0.1f, -0.2f, 0.3f, -0.4f};
        const auto before = data;
        eq.process(data.data(), 2);
        QCOMPARE(data, before);
    }

    void negativeGainAttenuatesCenterFrequency()
    {
        EqProcessor eq;
        eq.configure(48000, 2);
        eq.setGain(5, -12);   // 1 kHz
        constexpr int frames = 48000;
        std::vector<float> data(frames * 2);
        for (int i = 0; i < frames; ++i) {
            const float s = float(std::sin(2.0 * M_PI * 1000.0 * i / 48000.0));
            data[i * 2] = s;
            data[i * 2 + 1] = s;
        }
        auto input = data;
        eq.process(data.data(), frames);
        double in = 0, out = 0;
        for (int i = frames / 2; i < frames; ++i) {
            in += input[i * 2] * input[i * 2];
            out += data[i * 2] * data[i * 2];
        }
        QVERIFY(out < in * 0.2);   // -12 dB 至少 4 倍功率衰减
    }

    void resetOnConfigure()
    {
        EqProcessor eq;
        eq.configure(44100, 2);
        eq.setGain(0, 6);
        QCOMPARE(eq.gain(0), 6.0);
        eq.configure(48000, 2);
        QCOMPARE(eq.gain(0), 6.0);
        QVERIFY(!eq.neutral());
    }

    void stereoWidthZeroCollapsesToMono()
    {
        EqProcessor eq;
        eq.configure(48000, 2);
        eq.setStereoWidth(0);
        std::vector<float> data{1.0f, -0.5f};
        eq.process(data.data(), 1);
        QCOMPARE(data[0], 0.25f);
        QCOMPARE(data[1], 0.25f);
    }

    void bassBoostRaisesLowFrequency()
    {
        EqProcessor eq;
        eq.configure(48000, 2);
        eq.setBassBoost(9);
        constexpr int frames = 24000;
        std::vector<float> data(frames * 2);
        for (int i = 0; i < frames; ++i) {
            const float s = float(std::sin(2.0 * M_PI * 60.0 * i / 48000.0));
            data[i * 2] = s;
            data[i * 2 + 1] = s;
        }
        auto input = data;
        eq.process(data.data(), frames);
        double in = 0, out = 0;
        for (int i = frames / 2; i < frames; ++i) {
            in += input[i * 2] * input[i * 2];
            out += data[i * 2] * data[i * 2];
        }
        QVERIFY(out > in * 2.0);
    }
};

QTEST_GUILESS_MAIN(EqProcessorTest)
#include "eqprocessor_test.moc"
