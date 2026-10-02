#include "audio.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QProcess>
#include <QThread>
#include <iostream>
#include <cmath>

int main(int argc, char **argv) {
    gst_init(&argc, &argv); QCoreApplication app(argc, argv);
    Audio audio; if (!audio.supported()) return 1;
    QObject::connect(&audio, &Audio::error, [](QString e) { std::cerr << e.toStdString() << '\n'; });
    QProcess allowed, excluded;
    auto start = [](QProcess &p, QString name, QString frequency) {
        p.start("pw-cat", {"--playback", "--raw", "--format=f32", "--rate=48000", "--channels=2",
            "--target=lazarus-test-sink", "--properties=application.name=" + name + " node.dont-fallback=true node.dont-move=true", "-"});
        if (!p.waitForStarted()) return false;
        QByteArray samples(48000 * 12 * 2 * int(sizeof(float)), Qt::Uninitialized);
        auto *values = reinterpret_cast<float *>(samples.data());
        for (int i = 0; i < 48000 * 12; ++i) values[2*i] = values[2*i+1] = float(.1 * std::sin(2 * M_PI * frequency.toDouble() * i / 48000));
        p.write(samples); p.closeWriteChannel(); return true;
    };
    if (!start(allowed, "LazarusAllowedTest", "440") || !start(excluded, "LazarusExcludedTest", "880")) return 1;
    QElapsedTimer elapsed; elapsed.start(); QString selected;
    bool found = false;
    while (elapsed.elapsed() < 7000) {
        QCoreApplication::processEvents(); QThread::msleep(20);
        for (auto a : audio.applications()) if (a.name.contains("Allowed")) { selected = a.id; found = true; }
        if (found && audio.applications().size() >= 2) break;
    }
    bool failed = !found; audio.select({selected});
    if (!found) for (auto a : audio.applications()) std::cerr << "Discovered test application: " << a.name.toStdString() << '\n';
    double c440 = 0, s440 = 0, c880 = 0, s880 = 0, energy = 0; quint64 n = 0;
    elapsed.restart();
    while (elapsed.elapsed() < 2500 && !failed) {
        QCoreApplication::processEvents(); QThread::msleep(10);
        for (auto *sample : audio.takeSamples()) {
            GstMapInfo map;
            if (gst_buffer_map(gst_sample_get_buffer(sample), &map, GST_MAP_READ)) {
                auto *values = reinterpret_cast<const float *>(map.data);
                for (size_t i = 0; i + 1 < map.size / sizeof(float); i += 2, ++n) {
                    double value = values[i]; double t = double(n) / 48000;
                    c440 += value * std::cos(2 * M_PI * 440 * t); s440 += value * std::sin(2 * M_PI * 440 * t);
                    c880 += value * std::cos(2 * M_PI * 880 * t); s880 += value * std::sin(2 * M_PI * 880 * t); energy += value * value;
                }
                gst_buffer_unmap(gst_sample_get_buffer(sample), &map);
            }
            gst_sample_unref(sample);
        }
    }
    double a440 = n ? std::hypot(c440, s440) / n : 0, a880 = n ? std::hypot(c880, s880) / n : 0;
    std::cout << "Authorized tone amplitude=" << a440 << ", excluded tone amplitude=" << a880 << ", samples=" << n << '\n';
    failed |= n < 48000 || energy < 1 || a440 < .005 || a880 > a440 * .05;
    audio.select({});
    for (auto *sample : audio.takeSamples()) { failed = true; gst_sample_unref(sample); }
    allowed.terminate(); excluded.terminate(); allowed.waitForFinished(); excluded.waitForFinished();
    if (failed) std::cerr << allowed.readAllStandardOutput().toStdString() << allowed.readAllStandardError().toStdString() << excluded.readAllStandardOutput().toStdString() << excluded.readAllStandardError().toStdString();
    return failed ? 1 : 0;
}
