#include "ogg.h"

#include <iostream>
#include <vorbis/codec.h>
#include <vorbis/vorbisfile.h>

#include "../audio/audio.h"
#include "../typedefs.h"

using namespace audio;

static inline const char* vorbis_error_message(int code) {
    switch (code) {
        case 0: return "no error";
        case OV_EREAD: return "a read from media returned an error";
        case OV_ENOTVORBIS: return "bitstream does not contain any Vorbis data";
        case OV_EVERSION: return "vorbis version mismatch";
        case OV_EBADHEADER: return "invalid Vorbis bitstream header";
        case OV_EFAULT: return "internal logic fault";
        case OV_EINVAL: return "invalid read operation";
        default:
            return "unknown";
    }
}

audio::PCM* ogg::load_pcm(const std::filesystem::path& file, bool headerOnly) {
    OggVorbis_File vf;
    int code;
    if ((code = ov_fopen(file.u8string().c_str(), &vf))) {
        throw std::runtime_error(vorbis_error_message(code));
    }
    std::vector<char> data;

    vorbis_info* info = ov_info(&vf, -1);
    uint channels = info->channels;
    uint sampleRate = info->rate;
    bool seekable = ov_seekable(&vf);
    size_t totalSamples = seekable ? ov_pcm_total(&vf, -1) : 0;

    if (!headerOnly) {
        const int bufferSize = 4096;
        int section = 0;
        char buffer[bufferSize];

        bool eof = false;
        while (!eof) {
            long ret = ov_read(&vf, buffer, bufferSize, 0, 2, true, &section);
            if (ret == 0) {
                eof = true;
            } else if (ret < 0) {
                std::cerr << "ogg::load_pcm: " << vorbis_error_message(ret) << std::endl;
            } else {
                data.insert(data.end(), std::begin(buffer), std::begin(buffer)+ret);
            }
        }
        totalSamples = data.size() / channels / 2;
    }
    ov_clear(&vf);
    return new PCM(std::move(data), totalSamples, channels, 16, sampleRate, seekable);
}

class OggStream : public PCMStream {
    OggVorbis_File vf;
    bool closed = false;
    uint channels;
    uint sampleRate;
    size_t totalSamples = 0;
    bool seekable;
public:
    OggStream(OggVorbis_File vf) : vf(std::move(vf)) {
        vorbis_info* info = ov_info(&vf, -1);
        channels = info->channels;
        sampleRate = info->rate;
        seekable = ov_seekable(&vf);
        if (seekable) {
            totalSamples = ov_pcm_total(&vf, -1);
        }
    }

    ~OggStream() {
        if (!closed) {
            close();
        }
    }

    size_t read(char* buffer, size_t bufferSize) override {
        if (closed) {
            return 0;
        }
        int bitstream = 0;
        long bytes = ov_read(&vf, buffer, bufferSize, 0, 2, true, &bitstream);
        if (bytes < 0) {
            std::cerr << "ogg::load_pcm: " << vorbis_error_message(bytes) << " " << bytes << std::endl;
            return PCMStream::ERROR;
        }
        return bytes;
    }

    void close() override {
        if (!closed) {
            ov_clear(&vf);
            closed = true;
        }
    }

    bool isOpen() const override {
        return !closed;
    }

    size_t getTotalSamples() const override {
        return totalSamples;
    }

    duration_t getTotalDuration() const override {
        return static_cast<duration_t>(totalSamples) /
               static_cast<duration_t>(sampleRate);
    }

    uint getChannels() const override {
        return channels;
    }

    uint getSampleRate() const override {
        return sampleRate;
    }

    uint getBitsPerSample() const override {
        return 16;
    }

    bool isSeekable() const override {
        return seekable;
    }

    void seek(size_t position) override {
        if (!closed && seekable) {
            ov_pcm_seek(&vf, position);
        }
    }
};

PCMStream* ogg::create_stream(const std::filesystem::path& file) {
    OggVorbis_File vf;
    int code;
    if ((code = ov_fopen(file.u8string().c_str(), &vf))) {
        throw std::runtime_error(vorbis_error_message(code));
    }
    return new OggStream(std::move(vf));
}