#include "chiptune/audio/envelope.hpp"

#include <algorithm>
#include <cmath>

namespace chiptune::audio {
namespace {

float finite_nonnegative(float value) noexcept {
    return std::isfinite(value) ? std::max(0.0F, value) : 0.0F;
}

float level_before_release(float time, float attack, float decay, float sustain) noexcept {
    if (attack > 0.0F && time < attack) return time / attack;
    if (decay > 0.0F && time < attack + decay) {
        const float progress = (time - attack) / decay;
        return 1.0F + (sustain - 1.0F) * progress;
    }
    return sustain;
}

} // namespace

void apply_adsr(std::span<float> samples, std::uint32_t sample_rate,
                std::size_t gate_frames, const AdsrParameters& parameters) noexcept {
    if (sample_rate == 0) {
        std::fill(samples.begin(), samples.end(), 0.0F);
        return;
    }

    const float attack = finite_nonnegative(parameters.attack_seconds);
    const float decay = finite_nonnegative(parameters.decay_seconds);
    const float sustain = std::isfinite(parameters.sustain_level)
        ? std::clamp(parameters.sustain_level, 0.0F, 1.0F) : 0.0F;
    const float release = finite_nonnegative(parameters.release_seconds);
    const float sample_rate_f = static_cast<float>(sample_rate);
    const float gate_time = static_cast<float>(gate_frames) / sample_rate_f;
    const float release_start = level_before_release(gate_time, attack, decay, sustain);

    for (std::size_t frame = 0; frame < samples.size(); ++frame) {
        const float time = static_cast<float>(frame) / sample_rate_f;
        float gain = 0.0F;
        if (frame < gate_frames) {
            gain = level_before_release(time, attack, decay, sustain);
        } else if (release > 0.0F) {
            const float release_time = static_cast<float>(frame - gate_frames) / sample_rate_f;
            gain = release_start * std::max(0.0F, 1.0F - release_time / release);
        }
        samples[frame] *= gain;
    }
}

} // namespace chiptune::audio
