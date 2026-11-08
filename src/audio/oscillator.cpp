#include "chiptune/audio/oscillator.hpp"

#include <cmath>
#include <numbers>
#include <stdexcept>

namespace chiptune::audio {
namespace {

bool is_valid_waveform(Waveform waveform) noexcept {
    switch (waveform) {
    case Waveform::sine:
    case Waveform::square:
    case Waveform::triangle:
    case Waveform::sawtooth:
    case Waveform::noise:
        return true;
    }
    return false;
}

} // namespace

Oscillator::Oscillator(std::uint32_t sample_rate) : sample_rate_(sample_rate) {
    if (sample_rate == 0) throw std::invalid_argument("Oscillator sample rate must be greater than zero");
}

void Oscillator::set_waveform(Waveform waveform) {
    if (!is_valid_waveform(waveform)) throw std::invalid_argument("Unknown oscillator waveform");
    waveform_ = waveform;
}

void Oscillator::set_frequency(float frequency_hz) {
    if (!std::isfinite(frequency_hz) || frequency_hz < 0.0F)
        throw std::invalid_argument("Oscillator frequency must be finite and non-negative");
    frequency_hz_ = frequency_hz;
}

void Oscillator::set_volume(float volume) {
    if (!std::isfinite(volume) || volume < 0.0F || volume > 1.0F)
        throw std::invalid_argument("Oscillator volume must be between 0 and 1");
    volume_ = volume;
}

void Oscillator::set_duty_cycle(float duty_cycle) {
    if (!std::isfinite(duty_cycle) || duty_cycle <= 0.0F || duty_cycle >= 1.0F)
        throw std::invalid_argument("Oscillator duty cycle must be between 0 and 1");
    duty_cycle_ = duty_cycle;
}

void Oscillator::set_vibrato(float depth_cents, float rate_hz) {
    if (!std::isfinite(depth_cents) || depth_cents < 0.0F || depth_cents > 1200.0F)
        throw std::invalid_argument("Vibrato depth must be between 0 and 1200 cents");
    if (!std::isfinite(rate_hz) || rate_hz < 0.0F)
        throw std::invalid_argument("Vibrato rate must be finite and non-negative");
    vibrato_depth_cents_ = depth_cents;
    vibrato_rate_hz_ = rate_hz;
}

void Oscillator::set_noise_seed(std::uint32_t seed) noexcept {
    noise_state_ = seed == 0 ? 0x6d2b79f5U : seed;
}

void Oscillator::reset_phase() noexcept {
    phase_ = 0.0;
    vibrato_phase_ = 0.0;
}

Waveform Oscillator::waveform() const noexcept { return waveform_; }
float Oscillator::frequency() const noexcept { return frequency_hz_; }
float Oscillator::volume() const noexcept { return volume_; }
float Oscillator::duty_cycle() const noexcept { return duty_cycle_; }
float Oscillator::vibrato_depth() const noexcept { return vibrato_depth_cents_; }
float Oscillator::vibrato_rate() const noexcept { return vibrato_rate_hz_; }
std::uint32_t Oscillator::sample_rate() const noexcept { return sample_rate_; }

float Oscillator::next_noise_sample() noexcept {
    // xorshift32 is a fast, allocation-free pseudo-random noise source.
    noise_state_ ^= noise_state_ << 13;
    noise_state_ ^= noise_state_ >> 17;
    noise_state_ ^= noise_state_ << 5;
    const auto value = noise_state_ >> 8;
    return (static_cast<float>(value) / 16777215.0F) * 2.0F - 1.0F;
}

void Oscillator::render(std::span<float> output) noexcept {
    const double base_phase_step = static_cast<double>(frequency_hz_) / sample_rate_;
    const double vibrato_step = static_cast<double>(vibrato_rate_hz_) / sample_rate_;
    constexpr double tau = 2.0 * std::numbers::pi_v<double>;
    for (auto& sample : output) {
        float value = 0.0F;
        switch (waveform_) {
        case Waveform::sine:
            value = static_cast<float>(std::sin(tau * phase_));
            break;
        case Waveform::square:
            value = phase_ < duty_cycle_ ? 1.0F : -1.0F;
            break;
        case Waveform::triangle:
            value = static_cast<float>(1.0 - 4.0 * std::abs(phase_ - 0.5));
            break;
        case Waveform::sawtooth:
            value = static_cast<float>(2.0 * phase_ - 1.0);
            break;
        case Waveform::noise:
            value = next_noise_sample();
            break;
        }
        sample = value * volume_;
        double phase_step = base_phase_step;
        if (vibrato_depth_cents_ > 0.0F) {
            const double cents = vibrato_depth_cents_ * std::sin(tau * vibrato_phase_);
            phase_step *= std::exp2(cents / 1200.0);
        }
        phase_ += phase_step;
        phase_ -= std::floor(phase_);
        vibrato_phase_ += vibrato_step;
        vibrato_phase_ -= std::floor(vibrato_phase_);
    }
}

} // namespace chiptune::audio
