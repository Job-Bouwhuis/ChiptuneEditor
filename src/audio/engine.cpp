#include "chiptune/audio/engine.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace chiptune::audio {

AudioEngine::AudioEngine(std::uint32_t output_sample_rate)
    : output_sample_rate_(output_sample_rate) {
    if (output_sample_rate == 0) throw std::invalid_argument("Output sample rate must be greater than zero");
}

void AudioEngine::load(PcmAudio audio) {
    if (playing_.load(std::memory_order_acquire))
        throw std::logic_error("Stop playback before loading another audio source");
    if (audio.sample_rate == 0 || (audio.channels != 1 && audio.channels != 2))
        throw std::invalid_argument("Audio source must have a valid sample rate and one or two channels");
    if (audio.samples.size() % audio.channels != 0)
        throw std::invalid_argument("Audio source must contain complete frames");
    source_ = std::move(audio);
    source_position_ = 0.0;
    for (auto& voice : voices_) voice.active.store(false, std::memory_order_release);
}

bool AudioEngine::add_voice(PcmAudio audio) {
    if (audio.sample_rate == 0 || (audio.channels != 1 && audio.channels != 2) ||
        audio.samples.size() % audio.channels != 0 || audio.samples.empty())
        throw std::invalid_argument("Voice audio must contain valid mono or stereo PCM frames");
    for (auto& voice : voices_) {
        if (voice.active.load(std::memory_order_acquire)) continue;
        voice.source = std::move(audio);
        voice.position = 0.0;
        voice.active.store(true, std::memory_order_release);
        return true;
    }
    return false;
}

void AudioEngine::play() noexcept {
    if (!source_.samples.empty()) {
        restart_requested_.store(true, std::memory_order_release);
        playing_.store(true, std::memory_order_release);
    }
}

void AudioEngine::set_looping(bool looping) noexcept {
    looping_.store(looping, std::memory_order_release);
}

void AudioEngine::stop() noexcept {
    playing_.store(false, std::memory_order_release);
    for (auto& voice : voices_) voice.active.store(false, std::memory_order_release);
}

bool AudioEngine::is_playing() const noexcept {
    if (playing_.load(std::memory_order_acquire)) return true;
    for (const auto& voice : voices_)
        if (voice.active.load(std::memory_order_acquire)) return true;
    return false;
}

std::uint32_t AudioEngine::output_sample_rate() const noexcept {
    return output_sample_rate_;
}

void AudioEngine::render(std::span<std::int16_t> output, std::uint16_t output_channels) noexcept {
    std::fill(output.begin(), output.end(), 0);
    if (restart_requested_.exchange(false, std::memory_order_acq_rel)) source_position_ = 0.0;
    if ((output_channels != 1 && output_channels != 2) || output.size() % output_channels != 0) return;

    const auto output_frames = output.size() / output_channels;
    const auto mix_source = [&](const PcmAudio& source, double& position, std::atomic<bool>* active) {
        const auto frames = source.frame_count();
        if (frames == 0) {
            if (active) active->store(false, std::memory_order_release);
            return;
        }
        const double step = static_cast<double>(source.sample_rate) / output_sample_rate_;
        for (std::size_t frame = 0; frame < output_frames; ++frame) {
            if (position >= static_cast<double>(frames)) {
                if (!active && looping_.load(std::memory_order_acquire)) {
                    position = std::fmod(position, static_cast<double>(frames));
                } else {
                    if (active) active->store(false, std::memory_order_release);
                    else playing_.store(false, std::memory_order_release);
                    break;
                }
            }
            const auto index = static_cast<std::uint64_t>(position);
            const auto next = std::min(index + 1, frames - 1);
            const auto fraction = position - static_cast<double>(index);
            const auto sample = [&](std::uint16_t channel) noexcept {
                const auto source_channel = source.channels == 1 ? 0 : channel;
                const auto a = source.samples[static_cast<std::size_t>(index * source.channels + source_channel)];
                const auto b = source.samples[static_cast<std::size_t>(next * source.channels + source_channel)];
                return static_cast<std::int16_t>(std::lround(static_cast<double>(a) +
                                                             (static_cast<double>(b) - a) * fraction));
            };
            if (source.channels == 1 && output_channels == 2) {
                const auto mono = sample(0);
                output[frame * 2] = static_cast<std::int16_t>(std::clamp<int>(output[frame * 2] + mono, -32768, 32767));
                output[frame * 2 + 1] = static_cast<std::int16_t>(std::clamp<int>(output[frame * 2 + 1] + mono, -32768, 32767));
            } else if (source.channels == 2 && output_channels == 1) {
                const auto sum = (static_cast<std::int32_t>(sample(0)) + sample(1)) / 2;
                output[frame] = static_cast<std::int16_t>(std::clamp<int>(output[frame] + sum, -32768, 32767));
            } else {
                for (std::uint16_t channel = 0; channel < output_channels; ++channel) {
                    const auto offset = frame * output_channels + channel;
                    output[offset] = static_cast<std::int16_t>(std::clamp<int>(output[offset] + sample(channel), -32768, 32767));
                }
            }
            position += step;
        }
    };
    if (playing_.load(std::memory_order_acquire)) {
        if (restart_requested_.exchange(false, std::memory_order_acq_rel)) source_position_ = 0.0;
        mix_source(source_, source_position_, nullptr);
    }
    for (auto& voice : voices_)
        if (voice.active.load(std::memory_order_acquire)) mix_source(voice.source, voice.position, &voice.active);
}

} // namespace chiptune::audio
