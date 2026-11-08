#include "chiptune/audio/wav.hpp"

#include <exception>
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: chiptune_wav_info <audio.wav>\n";
        return 2;
    }
    try {
        const auto audio = chiptune::audio::read_wav(argv[1]);
        const double duration = static_cast<double>(audio.frame_count()) / audio.sample_rate;
        std::cout << "Sample rate: " << audio.sample_rate << " Hz\n"
                  << "Channels: " << audio.channels << "\n"
                  << "Frames: " << audio.frame_count() << "\n"
                  << "Duration: " << duration << " s\n";
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
