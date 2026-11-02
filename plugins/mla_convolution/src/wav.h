#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <vector>
namespace mla_convolution
{
inline uint32_t u32(const unsigned char* p)
{
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) |
           (uint32_t(p[3]) << 24);
}
inline uint16_t u16(const unsigned char* p)
{
    return uint16_t(p[0]) | (uint16_t(p[1]) << 8);
}
// Decode RIFF WAV only; enforce bounds before allocating or replacing a live
// IR.
inline std::vector<float> loadWav(const char* path, double targetRate)
{
    if(!std::isfinite(targetRate) || targetRate < 8000 || targetRate > 384000)
        throw std::runtime_error("Unsupported sample rate");
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    const auto bytes = file.tellg();
    if(bytes < 12 || bytes > 128 * 1024 * 1024)
        throw std::runtime_error(
            "Cannot read WAV: file missing, too short, or larger than 128 MiB");
    std::vector<unsigned char> data(static_cast<size_t>(bytes));
    file.seekg(0);
    if(!file.read(reinterpret_cast<char*>(data.data()), bytes))
        throw std::runtime_error("Cannot read WAV");
    if(std::memcmp(data.data(), "RIFF", 4) ||
       std::memcmp(data.data() + 8, "WAVE", 4))
        throw std::runtime_error("Choose a RIFF WAV file");
    size_t end = size_t(u32(data.data() + 4)) + 8;
    if(end > data.size() || end < 12)
        throw std::runtime_error("Truncated WAV");
    const unsigned char *fmt = nullptr, *pcm = nullptr;
    size_t fmtSize = 0, pcmSize = 0;
    for(size_t at = 12; at + 8 <= end;)
    {
        size_t n = u32(data.data() + at + 4), start = at + 8;
        if(n > end - start)
            throw std::runtime_error("Truncated WAV chunk");
        if(!std::memcmp(data.data() + at, "fmt ", 4))
        {
            fmt = data.data() + start;
            fmtSize = n;
        }
        if(!std::memcmp(data.data() + at, "data", 4))
        {
            pcm = data.data() + start;
            pcmSize = n;
        }
        at = start + n + (n & 1);
    }
    if(!fmt || fmtSize < 16 || !pcm)
        throw std::runtime_error("WAV needs format and sample data");
    int format = u16(fmt), channels = u16(fmt + 2), bits = u16(fmt + 14),
        stride = u16(fmt + 12);
    const uint32_t rate = u32(fmt + 4);
    if(format == 0xfffe && fmtSize >= 40 && u16(fmt + 16) >= 22)
    {
        static const unsigned char suffix[14]{0, 0, 0,    0, 0x10, 0,    0x80,
                                              0, 0, 0xaa, 0, 0x38, 0x9b, 0x71};
        if(std::memcmp(fmt + 26, suffix, 14))
            throw std::runtime_error("Unsupported WAV encoding");
        format = u16(fmt + 24);
    }
    if(channels < 1 || channels > 2 || rate < 8000 || rate > 384000 ||
       stride != channels * (bits / 8) ||
       !((format == 1 &&
          (bits == 8 || bits == 16 || bits == 24 || bits == 32)) ||
         (format == 3 && (bits == 32 || bits == 64))))
        throw std::runtime_error(
            "Use a mono/stereo PCM or floating-point WAV (8-384 kHz)");
    const size_t frames = pcmSize / stride;
    if(!frames || pcmSize % stride || frames > size_t(rate) * 30)
        throw std::runtime_error(
            "Impulse response must contain 1 frame to 30 seconds");
    auto sample = [&](size_t frame, int channel)
    {
        const auto* p =
            pcm + frame * stride + std::min(channel, channels - 1) * (bits / 8);
        float value = 0;
        if(format == 3)
        {
            if(bits == 32)
            {
                uint32_t v = u32(p);
                std::memcpy(&value, &v, 4);
            }
            else
            {
                uint64_t v = uint64_t(u32(p)) | (uint64_t(u32(p + 4)) << 32);
                double d;
                std::memcpy(&d, &v, 8);
                value = float(d);
            }
        }
        else if(bits == 8)
            value = (int(p[0]) - 128) / 128.f;
        else if(bits == 16)
            value = int16_t(u16(p)) / 32768.f;
        else if(bits == 24)
        {
            int32_t v = int32_t(uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
                                (uint32_t(p[2]) << 16));
            if(v & 0x800000)
                v -= 0x1000000;
            value = v / 8388608.f;
        }
        else
            value = int32_t(u32(p)) / 2147483648.f;
        if(!std::isfinite(value) || std::abs(value) > 16)
            throw std::runtime_error("Invalid or excessive WAV sample values");
        return value;
    };
    const size_t count =
        std::max(size_t(1), size_t(std::ceil(frames * targetRate / rate)));
    std::vector<float> result(count * 2);
    for(size_t i = 0; i < count; ++i)
    {
        const double pos =
            std::min(double(frames - 1), i * double(rate) / targetRate);
        const size_t a = size_t(pos), b = std::min(a + 1, frames - 1);
        const float t = float(pos - a);
        for(int c = 0; c < 2; ++c)
            result[i * 2 + c] = sample(a, c) * (1 - t) + sample(b, c) * t;
    }
    return result;
}
} // namespace mla_convolution
