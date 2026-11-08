#pragma once

#include <cstdint>
#include <span>

namespace chiptune::audio {

enum class Waveform {
    sine,
    square,
    triangle,
    sawtooth,
    noise
};

// A single, mono oscillator. render() writes normalized float samples to the
// caller's buffer and does not allocate. Configure it before rendering or from
// the same thread that owns the oscillator.
class Oscillator {
public:
    explicit Oscillator(std::uint32_t sample_rate);

    void set_waveform(Waveform waveform);
    void set_frequency(float frequency_hz);
    void set_volume(float volume);
    void set_duty_cycle(float duty_cycle);
    void set_vibrato(float depth_cents, float rate_hz);
    void set_noise_seed(std::uint32_t seed) noexcept;
    void reset_phase() noexcept;

    [[nodiscard]] Waveform waveform() const noexcept;
    [[nodiscard]] float frequency() const noexcept;
    [[nodiscard]] float volume() const noexcept;
    [[nodiscard]] float duty_cycle() const noexcept;
    [[nodiscard]] float vibrato_depth() const noexcept;
    [[nodiscard]] float vibrato_rate() const noexcept;
    [[nodiscard]] std::uint32_t sample_rate() const noexcept;

    void render(std::span<float> output) noexcept;

private:
    [[nodiscard]] float next_noise_sample() noexcept;

    std::uint32_t sample_rate_;
    Waveform waveform_{Waveform::square};
    float frequency_hz_{440.0F};
    float volume_{0.25F};
    float duty_cycle_{0.5F};
    float vibrato_depth_cents_{};
    float vibrato_rate_hz_{5.0F};
    double phase_{};
    double vibrato_phase_{};
    std::uint32_t noise_state_{0x6d2b79f5U};
};

} // namespace chiptune::audio
