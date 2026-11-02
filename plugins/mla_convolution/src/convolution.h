#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <vector>

namespace mla_convolution
{
// Uniform partitioned overlap-add. All storage and IR transforms are prepared
// off the audio thread. One block of latency keeps cost independent of host
// blocks.
class Convolution
{
public:
    static constexpr size_t block = 256, size = block * 2;
    using Spectrum = std::array<std::complex<float>, size>;
    static void fft(Spectrum& a, bool inverse)
    {
        for(size_t i = 1, j = 0; i < size; ++i)
        {
            size_t bit = size >> 1;
            for(; j & bit; bit >>= 1)
                j ^= bit;
            j ^= bit;
            if(i < j)
                std::swap(a[i], a[j]);
        }
        for(size_t n = 2; n <= size; n <<= 1)
        {
            const float angle =
                (inverse ? 2.f : -2.f) * 3.14159265358979323846f / float(n);
            const std::complex<float> w0(std::cos(angle), std::sin(angle));
            for(size_t i = 0; i < size; i += n)
            {
                std::complex<float> w(1, 0);
                for(size_t j = 0; j < n / 2; ++j)
                {
                    auto u = a[i + j], v = a[i + j + n / 2] * w;
                    a[i + j] = u + v;
                    a[i + j + n / 2] = u - v;
                    w *= w0;
                }
            }
        }
        if(inverse)
            for(auto& v : a)
                v /= float(size);
    }
    explicit Convolution(const std::vector<float>& interleaved)
    {
        const size_t frames = interleaved.size() / 2;
        partitions = (frames + block - 1) / block;
        for(int c = 0; c < 2; ++c)
        {
            ir[c].resize(partitions);
            history[c].resize(partitions);
            for(size_t p = 0; p < partitions; ++p)
            {
                for(size_t i = 0; i < block && p * block + i < frames; ++i)
                    ir[c][p][i] = interleaved[(p * block + i) * 2 + c];
                fft(ir[c][p], false);
            }
        }
    }
    void reset()
    {
        position = head = 0;
        for(int c = 0; c < 2; ++c)
        {
            input[c].fill(0);
            output[c].fill(0);
            overlap[c].fill(0);
            for(auto& h : history[c])
                h.fill({});
        }
    }
    void process(float left, float right, float mix, float& outL, float& outR)
    {
        const float values[2]{left, right};
        float* outputs[2]{&outL, &outR};
        for(int c = 0; c < 2; ++c)
        {
            // Reuse the previous input block as a latency-matched dry delay.
            *outputs[c] =
                input[c][position] * (1 - mix) + output[c][position] * mix;
            input[c][position] = values[c];
        }
        if(++position != block)
            return;
        position = 0;
        for(int c = 0; c < 2; ++c)
        {
            auto& current = history[c][head];
            current.fill({});
            for(size_t i = 0; i < block; ++i)
                current[i] = input[c][i];
            fft(current, false);
            Spectrum sum{};
            for(size_t p = 0; p < partitions; ++p)
            {
                const auto& h =
                    history[c][(head + partitions - p) % partitions];
                for(size_t i = 0; i < size; ++i)
                    sum[i] += h[i] * ir[c][p][i];
            }
            fft(sum, true);
            for(size_t i = 0; i < block; ++i)
            {
                output[c][i] = sum[i].real() + overlap[c][i];
                overlap[c][i] = sum[i + block].real();
            }
        }
        head = (head + 1) % partitions;
    }

private:
    size_t partitions = 0, position = 0, head = 0;
    std::vector<Spectrum> ir[2], history[2];
    std::array<float, block> input[2]{}, output[2]{}, overlap[2]{};
};
} // namespace mla_convolution
