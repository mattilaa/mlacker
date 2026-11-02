# Mla Convolution

Stereo partitioned FFT convolution reverb. Load it on an FX return, a track
insert, or the master, then choose **Effect > Load convolution WAV...**.
Select a WAV and confirm the popup to replace the impulse response. For an
insert or master instance, open its parameter editor first to target it.
Cancelling or loading an invalid file keeps the current response.

The WAV is the impulse response that defines the room/filter sound; the track
supplies the live signal being reverberated. Mono responses apply to both
channels; stereo responses convolve left and right independently. Mix defaults
to 100% wet for auxiliary sends; reduce Mix for an insert. No response passes
the input through.

Supports mono/stereo RIFF PCM 8/16/24/32-bit and float 32/64-bit WAV, including
WAVE_FORMAT_EXTENSIBLE, at 8–384 kHz. Responses are resampled to the output rate,
limited to 30 seconds and files smaller than 128 MiB. There is 256 samples of
processing latency (5.33 ms at 48 kHz); mlacker currently does not compensate
plugin latency between tracks. Processing allocates no memory or locks.

Sessions retain the WAV's path and reload it on opening or changing devices.
Keep that WAV available at its original location; it is not embedded into a
project or an exposed-parameter preset. Other VST3 hosts can preserve plugin
state, but mlacker supplies the WAV chooser through its own Effect menu.

Build with `./build.sh --plugins`. Test with
`ctest --test-dir build/plugins -R convolution_processor --output-on-failure`.
