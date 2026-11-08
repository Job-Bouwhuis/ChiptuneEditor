#include "chiptune/audio/alsa_output.hpp"

#include <alsa/asoundlib.h>

#include <array>
#include <atomic>
#include <stdexcept>
#include <string>
#include <thread>

namespace chiptune::audio {

struct AlsaOutput::Impl {
    AudioEngine& engine;
    std::uint16_t channels;
    snd_pcm_t* pcm{};
    std::thread worker;
    std::atomic<bool> running{false};

    Impl(AudioEngine& engine_ref, std::uint16_t channel_count)
        : engine(engine_ref), channels(channel_count) {
        if (channels != 1 && channels != 2)
            throw std::invalid_argument("ALSA output supports mono or stereo");
    }

    ~Impl() { stop(); }

    void start() {
        if (running.load(std::memory_order_acquire)) return;
        if (worker.joinable()) worker.join();
        if (pcm) {
            snd_pcm_drop(pcm);
            snd_pcm_close(pcm);
            pcm = nullptr;
        }
        const int open_result = snd_pcm_open(&pcm, "default", SND_PCM_STREAM_PLAYBACK, 0);
        if (open_result < 0)
            throw std::runtime_error(std::string("Could not open ALSA playback device: ") + snd_strerror(open_result));

        const int params_result = snd_pcm_set_params(
            pcm, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED,
            channels, engine_sample_rate(), 1, 100000);
        if (params_result < 0) {
            snd_pcm_close(pcm);
            pcm = nullptr;
            throw std::runtime_error(std::string("Could not configure ALSA playback: ") + snd_strerror(params_result));
        }

        running.store(true, std::memory_order_release);
        try {
            worker = std::thread([this] { run(); });
        } catch (...) {
            running.store(false, std::memory_order_release);
            snd_pcm_close(pcm);
            pcm = nullptr;
            throw;
        }
    }

    std::uint32_t engine_sample_rate() const noexcept { return engine.output_sample_rate(); }

    void run() noexcept {
        constexpr std::size_t frames = 256;
        std::array<std::int16_t, frames * 2> buffer{};
        while (running.load(std::memory_order_acquire)) {
            engine.render(std::span<std::int16_t>(buffer.data(), frames * channels), channels);
            snd_pcm_uframes_t offset = 0;
            while (offset < frames && running.load(std::memory_order_acquire)) {
                const auto* start = buffer.data() + static_cast<std::size_t>(offset) * channels;
                const snd_pcm_sframes_t written = snd_pcm_writei(pcm, start, frames - offset);
                if (written < 0) {
                    if (snd_pcm_recover(pcm, static_cast<int>(written), 1) < 0) {
                        running.store(false, std::memory_order_release);
                        break;
                    }
                } else if (written == 0) {
                    running.store(false, std::memory_order_release);
                    break;
                } else {
                    offset += static_cast<snd_pcm_uframes_t>(written);
                }
            }
        }
    }

    void stop() noexcept {
        if (!running.exchange(false, std::memory_order_acq_rel) && !pcm) return;
        if (worker.joinable()) worker.join();
        if (pcm) {
            snd_pcm_drop(pcm);
            snd_pcm_close(pcm);
            pcm = nullptr;
        }
    }
};

AlsaOutput::AlsaOutput(AudioEngine& engine, std::uint16_t channels)
    : impl_(std::make_unique<Impl>(engine, channels)) {}
AlsaOutput::~AlsaOutput() = default;
void AlsaOutput::start() { impl_->start(); }
void AlsaOutput::stop() noexcept { impl_->stop(); }
bool AlsaOutput::is_running() const noexcept { return impl_->running.load(std::memory_order_acquire); }

} // namespace chiptune::audio
