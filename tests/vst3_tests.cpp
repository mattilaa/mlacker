#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <thread>
#include <chrono>
#include <fstream>
#include <string>
extern "C" {
struct FloatList { int64_t size; const float *data; };
struct DoubleList { int64_t size; double *data; };
struct IntList { int64_t size; const int32_t *data; };
int32_t __mlang_std_audio_controller_instrument_sysex(int64_t, int64_t, int64_t, IntList);
DoubleList __mlang_std_audio_controller_instrument_markers(int64_t, int64_t, int64_t);
int32_t __mlang_std_audio_controller_instrument_set_markers(int64_t, int64_t, int64_t, DoubleList, int64_t);
int64_t __mlang_std_audio_controller_sample_data(int64_t, FloatList, int64_t, int64_t);
void mlacker_install_vst3_host();
int64_t __mlang_std_audio_controller_new(int64_t, int64_t);
int64_t __mlang_std_audio_controller_open(int64_t, int64_t);
int32_t __mlang_std_audio_controller_start(int64_t);
int32_t __mlang_std_audio_controller_load_processor(int64_t, const char*);
int32_t __mlang_std_audio_controller_load_instrument(int64_t, int64_t, const char*);
const char *__mlang_std_audio_controller_instrument_name(int64_t, int64_t);
double __mlang_std_audio_controller_parameter_info(int64_t, int64_t, int64_t, int64_t);
const char *__mlang_std_audio_controller_parameter_name(int64_t, int64_t, int64_t);
int32_t __mlang_std_audio_controller_set_parameter(int64_t, int64_t, int64_t, double);
int32_t __mlang_std_audio_controller_restore_parameter(int64_t, int64_t, int64_t, double);
int32_t __mlang_std_audio_controller_unload_instrument(int64_t, int64_t);
int32_t __mlang_std_audio_controller_instrument_clear_pad(int64_t, int64_t, int64_t);
int64_t __mlang_std_audio_controller_instrument_sampler(int64_t, int64_t, int64_t);
int64_t __mlang_std_audio_controller_instrument_outputs(int64_t, int64_t);
const char *__mlang_std_audio_controller_instrument_output_name(int64_t, int64_t, int64_t);
int32_t __mlang_std_audio_controller_instrument_output_route(int64_t, int64_t, int64_t, int64_t);
int32_t __mlang_std_audio_controller_instrument_input(int64_t, int64_t, int64_t);
int32_t __mlang_std_audio_controller_input_feed(int64_t, FloatList, int64_t, int64_t);
int32_t __mlang_std_audio_controller_input_peak(int64_t, int64_t);
int32_t __mlang_std_audio_controller_track_peak(int64_t, int64_t, int64_t);
int32_t __mlang_std_audio_controller_instrument_pad(int64_t, int64_t, int64_t, FloatList, int64_t, int64_t, const char*);
int32_t __mlang_std_audio_controller_instrument_text(int64_t, int64_t, int64_t, const char*);
int32_t __mlang_std_audio_controller_midi_target(int64_t, int64_t, int64_t);
int32_t __mlang_std_audio_controller_live_note(int64_t, int64_t, int64_t, int64_t, int64_t);
int32_t __mlang_std_audio_controller_live_control(int64_t, int64_t, int64_t, int64_t);
int32_t __mlang_std_audio_controller_track_volume(int64_t, int64_t, int64_t, int64_t);
int32_t __mlang_std_audio_controller_instrument_peak(int64_t, int64_t, int64_t);
int32_t __mlang_std_audio_controller_load_effect(int64_t, int64_t, const char*);
int32_t __mlang_std_audio_controller_load_insert(int64_t, int64_t, const char*);
int32_t __mlang_std_audio_controller_insert_route(int64_t, int64_t, int64_t, int64_t);
int32_t __mlang_std_audio_controller_effect_send(int64_t, int64_t, int64_t, int64_t, int64_t);
int32_t __mlang_std_audio_controller_effect_volume(int64_t, int64_t, int64_t);
int32_t __mlang_std_audio_controller_effect_peak(int64_t, int64_t, int64_t);
int32_t __mlang_std_audio_controller_midi_learn(int64_t, int64_t, int64_t, int64_t);
int64_t __mlang_std_audio_controller_midi_learn_info(int64_t, int64_t, int64_t);
int32_t __mlang_std_audio_controller_master_peak(int64_t, int64_t);
const char *__mlang_std_audio_controller_processor_name(int64_t);
int32_t __mlang_std_audio_controller_processor_support();
int32_t __mlang_std_audio_controller_post(int64_t, int64_t, int64_t, int64_t, int64_t, int64_t, int64_t, int64_t, int64_t, double);
int32_t __mlang_std_audio_controller_process(int64_t, int64_t, int64_t);
void __mlang_std_audio_controller_panic(int64_t);
int32_t __mlang_std_audio_controller_stop(int64_t);
int32_t __mlang_std_audio_controller_close(int64_t);
int64_t __mlang_std_audio_controller_info(int64_t, int64_t);
int32_t __mlang_std_audio_controller_transport(int64_t, double, double, int32_t);
double __mlang_std_audio_controller_transport_beat(int64_t);
int64_t __mlang_std_audio_pcm_block_new(int64_t);
float __mlang_std_audio_pcm_block_sample(int64_t, int64_t, int64_t);
int32_t __mlang_std_audio_pcm_block_close(int64_t);
const char *__mlang_std_audio_last_error();
}
#define CHECK(condition) do { if(!(condition)) { std::fprintf(stderr, "FAIL line %d: %s; %s\n", __LINE__, #condition, __mlang_std_audio_last_error()); std::exit(1); } } while(0)
int main(int argc, char **argv) {
    if(argc == 2 && std::strcmp(argv[1], "--handover-probe") == 0) {
        // Silent hardware diagnostic: no plugins, samples, or note events.
        const int64_t old = __mlang_std_audio_controller_open(-1, 128);
        CHECK(old && __mlang_std_audio_controller_start(old) == 0);
        const int64_t next = __mlang_std_audio_controller_open(-1, 128);
        CHECK(next && __mlang_std_audio_controller_stop(old) == 0);
        CHECK(__mlang_std_audio_controller_start(next) == 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        const auto before = __mlang_std_audio_controller_info(next, 2);
        CHECK(__mlang_std_audio_controller_close(old) == 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        const auto after = __mlang_std_audio_controller_info(next, 2);
        std::printf("Output handover frame clock: before=%lld after=%lld\n", (long long)before, (long long)after);
        CHECK(__mlang_std_audio_controller_close(next) == 0);
        CHECK(before > 0 && after > before);
        return 0;
    }
    if(argc == 3 && (std::strcmp(argv[1], "--restore-probe") == 0 || std::strcmp(argv[1], "--session-probe") == 0)) {
        const bool session = std::strcmp(argv[1], "--session-probe") == 0;
        std::string path = argv[2];
        std::vector<double> saved;
        std::vector<int64_t> ids;
        if(session) {
            // Read-only diagnostic for a single-plugin, sample-free 1.0 session.
            std::ifstream input(path, std::ios::binary);
            auto integer = [&]() { int64_t v = 0; CHECK(input.read(reinterpret_cast<char*>(&v), 8)); return v; };
            auto string = [&]() { const auto size = integer(); CHECK(size >= 0 && size <= 4096); std::string v(size, '\0'); CHECK(input.read(v.data(), size)); return v; };
            CHECK(string() == "MLACK" && integer() == 1 && integer() == 0);
            for(int i = 0; i < 24; ++i) integer();
            CHECK(integer() == 0); CHECK(integer() == 1); CHECK(integer() == 1);
            path = string();
            const auto count = integer(); CHECK(count > 0 && count <= 16384);
            for(int i = 0; i < count; ++i) {
                ids.push_back(integer()); double v = 0;
                CHECK(input.read(reinterpret_cast<char*>(&v), 8)); CHECK(std::isfinite(v) && v >= 0 && v <= 1); saved.push_back(v);
            }
        }
        mlacker_install_vst3_host();
        const int64_t block = __mlang_std_audio_pcm_block_new(128);
        std::vector<double> values;
        std::vector<double> defaults;
        for(int pass = 0; pass < (session ? 3 : 2); ++pass) {
            const int64_t controller = __mlang_std_audio_controller_new(48000, 128);
            CHECK(__mlang_std_audio_controller_load_instrument(controller, 1, path.c_str()) == 0);
            for(int n = 0; pass == 0 && n < 375; ++n) {
                CHECK(__mlang_std_audio_controller_process(controller, block, 128) == 0);
                std::this_thread::sleep_for(std::chrono::milliseconds(3));
            }
            const int count = (int)__mlang_std_audio_controller_parameter_info(controller, 1, 0, 0);
            if(session) CHECK(count == (int)saved.size());
            for(int i = 0; i < count; ++i) {
                if(pass == 0) {
                    const double initial = __mlang_std_audio_controller_parameter_info(controller, 1, i, 2);
                    defaults.push_back(initial);
                    values.push_back(session ? saved[i] : initial);
                    if(session) {
                        CHECK(ids[i] == __mlang_std_audio_controller_parameter_info(controller, 1, i, 4));
                        if(std::fabs(initial - saved[i]) > 0.000001)
                            std::printf("%d %s: fresh=%.6f saved=%.6f\n", i, __mlang_std_audio_controller_parameter_name(controller, 1, i), initial, saved[i]);
                    }
                }
                else if(pass == 1 || std::fabs(defaults[i] - values[i]) > 0.000001)
                    CHECK(__mlang_std_audio_controller_restore_parameter(controller, 1, i, values.at(i)) == 0);
            }
            for(int n = 0; pass > 0 && n < 375; ++n) {
                CHECK(__mlang_std_audio_controller_process(controller, block, 128) == 0);
                std::this_thread::sleep_for(std::chrono::milliseconds(3));
            }
            CHECK(__mlang_std_audio_controller_midi_target(controller, 0, 1) == 0);
            CHECK(__mlang_std_audio_controller_live_note(controller, 1, 0, 60, 100) == 0);
            double peak = 0;
            for(int n = 0; n < 1500; ++n) {
                CHECK(__mlang_std_audio_controller_process(controller, block, 128) == 0);
                for(int f = 0; f < 128; ++f)
                    peak = std::fmax(peak, std::fabs(__mlang_std_audio_pcm_block_sample(block, f, 0)));
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            std::printf("%s: parameters=%d peak=%f errors=%lld\n", pass == 2 ? "edits only" : (pass ? "restored" : "fresh"), count, peak,
                (long long)__mlang_std_audio_controller_info(controller, 4));
            std::fflush(stdout);
            CHECK(__mlang_std_audio_controller_close(controller) == 0);
        }
        CHECK(__mlang_std_audio_pcm_block_close(block) == 0);
        return 0;
    }
    CHECK(argc == 3);
    mlacker_install_vst3_host(); CHECK(__mlang_std_audio_controller_processor_support() == 1);
    int64_t c = __mlang_std_audio_controller_new(48000, 128), b = __mlang_std_audio_pcm_block_new(256);
    CHECK(c && b);
    CHECK(__mlang_std_audio_controller_load_processor(c, argv[1]) == 0);
    CHECK(std::strcmp(__mlang_std_audio_controller_processor_name(c), "Mlacker Test Instrument") == 0);
    CHECK(__mlang_std_audio_controller_post(c, 0, 1, 2, 69, 127, 2, 64, 0, 1) == 0);
    CHECK(__mlang_std_audio_controller_post(c, 0, 2, 2, 69, 0, 2, 192, 0, 1) == 0);
    CHECK(__mlang_std_audio_controller_process(c, b, 256) == 0);
    for(int f = 0; f < 256; ++f) {
        float expected = f >= 64 && f < 192 ? 0.125f : 0.f;
        CHECK(__mlang_std_audio_pcm_block_sample(b, f, 0) == expected);
        CHECK(__mlang_std_audio_pcm_block_sample(b, f, 1) == expected);
    }
    CHECK(__mlang_std_audio_controller_load_processor(c, "/nonexistent/mlacker.vst3") != 0);
    CHECK(std::strcmp(__mlang_std_audio_controller_processor_name(c), "Mlacker Test Instrument") == 0);
    // A future main-thread event cannot block the live MIDI producer lane.
    CHECK(__mlang_std_audio_controller_post(c, 0, 3, 0, 0, 0, 0, 4096, 0, 0.5) == 0);
    CHECK(__mlang_std_audio_controller_post(c, 1, 1, 0, 60, 127, 2147483647, -1, 0, 1) == 0);
    CHECK(__mlang_std_audio_controller_process(c, b, 256) == 0);
    CHECK(__mlang_std_audio_pcm_block_sample(b, 20, 0) == 0.125f);
    __mlang_std_audio_controller_panic(c);
    CHECK(__mlang_std_audio_controller_process(c, b, 256) == 0);
    CHECK(__mlang_std_audio_pcm_block_sample(b, 20, 0) == 0.f);
    CHECK(__mlang_std_audio_controller_info(c, 4) == 0);
    CHECK(__mlang_std_audio_controller_stop(c) == 0);
    CHECK(__mlang_std_audio_controller_load_processor(c, "") == 0);
    CHECK(*__mlang_std_audio_controller_processor_name(c) == 0);
    // Module can be unloaded and reloaded repeatedly without dangling factories.
    for(int i = 0; i < 3; ++i) {
        CHECK(__mlang_std_audio_controller_load_processor(c, argv[1]) == 0);
        CHECK(__mlang_std_audio_controller_process(c, b, 256) == 0);
        CHECK(__mlang_std_audio_controller_load_processor(c, "") == 0);
    }
    CHECK(__mlang_std_audio_controller_load_processor(c, argv[2]) == 0);
    int64_t dry = __mlang_std_audio_controller_new(48000, 128), dryBlock = __mlang_std_audio_pcm_block_new(256);
    CHECK(dry && dryBlock);
    CHECK(__mlang_std_audio_controller_post(c, 0, 1, 0, 69, 127, 0, -1, 0, 1) == 0);
    CHECK(__mlang_std_audio_controller_post(dry, 0, 1, 0, 69, 127, 0, -1, 0, 1) == 0);
    CHECK(__mlang_std_audio_controller_process(c, b, 256) == 0);
    CHECK(__mlang_std_audio_controller_process(dry, dryBlock, 256) == 0);
    for(int f = 0; f < 256; ++f)
        CHECK(std::abs(__mlang_std_audio_pcm_block_sample(b, f, 0) - __mlang_std_audio_pcm_block_sample(dryBlock, f, 0) * 0.5f) < 1.e-7f);
    CHECK(__mlang_std_audio_controller_close(dry) == 0);
    CHECK(__mlang_std_audio_pcm_block_close(dryBlock) == 0);
    CHECK(__mlang_std_audio_controller_load_processor(c, "") == 0);
    CHECK(__mlang_std_audio_controller_load_instrument(c, 0, argv[1]) != 0);
    CHECK(__mlang_std_audio_controller_load_instrument(c, 33, argv[1]) != 0);
    CHECK(__mlang_std_audio_controller_load_instrument(c, 1, argv[2]) != 0);
    CHECK(__mlang_std_audio_controller_load_instrument(c, 1, argv[1]) == 0);
    CHECK(__mlang_std_audio_controller_load_instrument(c, 2, argv[1]) == 0);
    CHECK(__mlang_std_audio_controller_load_instrument(c, 1, argv[2]) != 0);
    CHECK(std::strcmp(__mlang_std_audio_controller_instrument_name(c, 1), "Mlacker Test Instrument") == 0);
    {
        // Sampler pads: a plain instrument rejects them; bad slots/formats never reach a plugin.
        const std::vector<float> pcm(64, 0.5f);
        CHECK(__mlang_std_audio_controller_instrument_pad(c, 1, 0, FloatList{64, pcm.data()}, 1, 48000, "kick") != 0);
        CHECK(std::strstr(__mlang_std_audio_last_error(), "does not accept pad samples") != nullptr);
        CHECK(__mlang_std_audio_controller_instrument_sampler(c, 1, 0) == -1);
        CHECK(__mlang_std_audio_controller_instrument_clear_pad(c, 1, 0) != 0);
        CHECK(__mlang_std_audio_controller_instrument_pad(c, 5, 0, FloatList{64, pcm.data()}, 1, 48000, "kick") != 0);
        CHECK(__mlang_std_audio_controller_instrument_pad(c, 1, 0, FloatList{63, pcm.data()}, 2, 48000, "kick") != 0);
        CHECK(__mlang_std_audio_controller_instrument_pad(c, 1, 0, FloatList{64, pcm.data()}, 3, 48000, "kick") != 0);
        // End to end with Mla Drum when its bundle has been built (see CMakeLists).
        if(const char *drum = std::getenv("MLA_DRUM_VST3")) {
            CHECK(__mlang_std_audio_controller_load_instrument(c, 3, drum) == 0);
            CHECK(std::strcmp(__mlang_std_audio_controller_instrument_name(c, 3), "Mla Drum") == 0);
            const std::vector<float> hit(4800, 0.5f);
            CHECK(__mlang_std_audio_controller_instrument_pad(c, 3, 16, FloatList{4800, hit.data()}, 1, 48000, "kick") != 0);
            CHECK(std::strstr(__mlang_std_audio_last_error(), "pad must be 0-15") != nullptr);
            CHECK(__mlang_std_audio_controller_instrument_pad(c, 3, 1, FloatList{4800, hit.data()}, 1, 48000, "kick") == 0);
            CHECK(__mlang_std_audio_controller_instrument_sampler(c, 3, 0) == 36);
            CHECK(__mlang_std_audio_controller_instrument_sampler(c, 3, 1) == 16);
            CHECK(__mlang_std_audio_controller_instrument_sampler(c, 3, 2) == 2);
            CHECK(__mlang_std_audio_controller_instrument_sampler(c, 3, 3) == -1);
            // Clearing and overriding: pad 1 empties, then takes a new sample.
            CHECK(__mlang_std_audio_controller_instrument_clear_pad(c, 3, 1) == 0);
            CHECK(__mlang_std_audio_controller_instrument_sampler(c, 3, 2) == 0);
            CHECK(__mlang_std_audio_controller_instrument_pad(c, 3, 1, FloatList{4800, hit.data()}, 1, 48000, "kick") == 0);
            __mlang_std_audio_controller_panic(c);
            CHECK(__mlang_std_audio_controller_process(c, b, 256) == 0);
            const int64_t now = __mlang_std_audio_controller_info(c, 2);
            // Pad 2 sits one key above the default root (36).
            CHECK(__mlang_std_audio_controller_post(c, 0, 6, 0, 37, 127, 0, now + 32, 3, 1) == 0);
            CHECK(__mlang_std_audio_controller_process(c, b, 256) == 0);
            // Sample-accurate start; 0.5 through the controller's default 0.25 master gain.
            CHECK(__mlang_std_audio_pcm_block_sample(b, 31, 0) == 0.f);
            CHECK(std::abs(__mlang_std_audio_pcm_block_sample(b, 32, 0) - 0.125f) < 1.e-4f);
            CHECK(std::abs(__mlang_std_audio_pcm_block_sample(b, 128, 1) - 0.125f) < 1.e-4f);
            // Two instances of the same bundle must stay independent: slot 4
            // has no samples, so hitting it is silent and leaves slot 3 alone.
            __mlang_std_audio_controller_panic(c);
            CHECK(__mlang_std_audio_controller_process(c, b, 256) == 0);
            CHECK(__mlang_std_audio_controller_load_instrument(c, 4, drum) == 0);
            CHECK(__mlang_std_audio_controller_instrument_sampler(c, 4, 2) == 0);
            CHECK(__mlang_std_audio_controller_instrument_sampler(c, 3, 2) == 2);
            __mlang_std_audio_controller_instrument_peak(c, 3, 0); __mlang_std_audio_controller_instrument_peak(c, 4, 0);
            int64_t later = __mlang_std_audio_controller_info(c, 2);
            CHECK(__mlang_std_audio_controller_post(c, 0, 6, 0, 37, 127, 1, later + 16, 4, 1) == 0);
            CHECK(__mlang_std_audio_controller_process(c, b, 256) == 0);
            // Slot 3 may still be ringing from its earlier one-shot hit; slot 4 must stay silent.
            CHECK(__mlang_std_audio_controller_instrument_peak(c, 4, 0) == 0);
            CHECK(__mlang_std_audio_controller_instrument_peak(c, 4, 1) == 0);
            // Loading a pad into slot 4 leaves slot 3's pads untouched.
            CHECK(__mlang_std_audio_controller_instrument_pad(c, 4, 5, FloatList{4800, hit.data()}, 1, 48000, "hat") == 0);
            CHECK(__mlang_std_audio_controller_instrument_sampler(c, 4, 2) == 32);
            CHECK(__mlang_std_audio_controller_instrument_sampler(c, 3, 2) == 2);
            __mlang_std_audio_controller_panic(c);
            CHECK(__mlang_std_audio_controller_unload_instrument(c, 4) == 0);
            CHECK(__mlang_std_audio_controller_unload_instrument(c, 3) == 0);
            std::printf("vst3_host: Mla Drum pad end-to-end passed\n");
        }
    }
    __mlang_std_audio_controller_panic(c);
    CHECK(__mlang_std_audio_controller_process(c, b, 256) == 0);
    int64_t clock = __mlang_std_audio_controller_info(c, 2);
    // Same pitch/channel in separate instances: independent, additive, exact offsets.
    CHECK(__mlang_std_audio_controller_post(c, 0, 6, 0, 60, 127, 0, clock + 32, 1, 1) == 0);
    CHECK(__mlang_std_audio_controller_post(c, 0, 6, 0, 60, 127, 1, clock + 64, 2, 1) == 0);
    CHECK(__mlang_std_audio_controller_post(c, 0, 7, 0, 60, 0, 0, clock + 96, 1, 1) == 0);
    CHECK(__mlang_std_audio_controller_post(c, 0, 7, 0, 60, 0, 1, clock + 128, 2, 1) == 0);
    CHECK(__mlang_std_audio_controller_process(c, b, 256) == 0);
    for(int f = 0; f < 256; ++f) {
        float expected = (f >= 32 && f < 96 ? .125f : 0.f) + (f >= 64 && f < 128 ? .125f : 0.f);
        CHECK(__mlang_std_audio_pcm_block_sample(b, f, 0) == expected);
    }
    // Unassigned instrument notes never fall back to a sine voice.
    CHECK(__mlang_std_audio_controller_post(c, 0, 6, 0, 60, 127, 0, -1, 0, 1) == 0);
    CHECK(__mlang_std_audio_controller_process(c, b, 256) == 0);
    CHECK(__mlang_std_audio_pcm_block_sample(b, 200, 0) == 0.f);
    CHECK(__mlang_std_audio_controller_post(c, 0, 6, 0, 60, 127, 0, -1, 1, 1) == 0);
    CHECK(__mlang_std_audio_controller_process(c, b, 256) == 0);
    __mlang_std_audio_controller_panic(c);
    CHECK(__mlang_std_audio_controller_process(c, b, 256) == 0);
    CHECK(__mlang_std_audio_pcm_block_sample(b, 200, 0) == 0.f);
    __mlang_std_audio_controller_master_peak(c, 0);
    __mlang_std_audio_controller_master_peak(c, 1);
    CHECK(__mlang_std_audio_controller_midi_target(c, 0, 1) == 0);
    CHECK(__mlang_std_audio_controller_live_note(c, 1, 3, 60, 127) == 0);
    CHECK(__mlang_std_audio_controller_process(c, b, 256) == 0);
    CHECK(__mlang_std_audio_pcm_block_sample(b, 200, 0) == .125f);
    CHECK(__mlang_std_audio_controller_master_peak(c, 0) == 125);
    CHECK(__mlang_std_audio_controller_master_peak(c, 1) == 125);
    CHECK(__mlang_std_audio_controller_master_peak(c, 0) == 0);
    // Changing selection while held: the next key addresses slot 2; first off
    // must still reach slot 1. Both instrument PCMs contribute to master.
    CHECK(__mlang_std_audio_controller_midi_target(c, 1, 2) == 0);
    CHECK(__mlang_std_audio_controller_live_note(c, 1, 3, 62, 127) == 0);
    CHECK(__mlang_std_audio_controller_process(c, b, 256) == 0);
    CHECK(__mlang_std_audio_pcm_block_sample(b, 200, 0) == .25f);
    CHECK(__mlang_std_audio_controller_live_note(c, 0, 3, 60, 0) == 0);
    CHECK(__mlang_std_audio_controller_process(c, b, 256) == 0);
    CHECK(__mlang_std_audio_pcm_block_sample(b, 200, 0) == .125f);
    CHECK(__mlang_std_audio_controller_master_peak(c, 0) == 250); // peak hold across blocks
    CHECK(__mlang_std_audio_controller_master_peak(c, 1) == 250);
    CHECK(__mlang_std_audio_controller_midi_target(c, 2, -1) == 0);
    CHECK(__mlang_std_audio_controller_live_note(c, 0, 3, 62, 0) == 0);
    CHECK(__mlang_std_audio_controller_live_note(c, 1, 3, 64, 127) == 0);
    CHECK(__mlang_std_audio_controller_process(c, b, 256) == 0);
    CHECK(__mlang_std_audio_pcm_block_sample(b, 200, 0) == 0.f);
    CHECK(__mlang_std_audio_controller_master_peak(c, 0) == 0);
    CHECK(__mlang_std_audio_controller_midi_target(c, 0, 1) == 0);
    CHECK(__mlang_std_audio_controller_load_processor(c, argv[2]) == 0);
    CHECK(__mlang_std_audio_controller_post(c, 0, 3, 0, 0, 0, 0, -1, 0, 1.0) == 0);
    CHECK(__mlang_std_audio_controller_live_note(c, 1, 0, 60, 127) == 0);
    CHECK(__mlang_std_audio_controller_process(c, b, 256) == 0);
    CHECK(__mlang_std_audio_pcm_block_sample(b, 200, 0) == .25f);
    CHECK(__mlang_std_audio_controller_master_peak(c, 0) == 250); // post-effect and gain
    CHECK(__mlang_std_audio_controller_stop(c) == 0);
    CHECK(__mlang_std_audio_controller_master_peak(c, 1) == 0);
    CHECK(__mlang_std_audio_controller_load_processor(c, "") == 0);
    CHECK(__mlang_std_audio_controller_load_instrument(c, 1, argv[1]) == 0);
    clock = __mlang_std_audio_controller_info(c, 2);
    CHECK(__mlang_std_audio_controller_post(c, 0, 6, 0, 60, 127, 0, clock, 1, 1) == 0);
    CHECK(__mlang_std_audio_controller_post(c, 0, 9, 0, 1, 0, 0, clock + 32, 1, 1) == 0);
    CHECK(__mlang_std_audio_controller_post(c, 0, 9, 0, 1, 127, 0, clock + 64, 1, 1) == 0);
    CHECK(__mlang_std_audio_controller_post(c, 0, 9, 0, 74, 64, 0, clock + 96, 1, 1) == 0);
    CHECK(__mlang_std_audio_controller_post(c, 0, 9, 0, 129, 0, 0, clock + 128, 1, 1) == 0);
    CHECK(__mlang_std_audio_controller_post(c, 0, 9, 0, 129, 16383, 0, clock + 160, 1, 1) == 0);
    CHECK(__mlang_std_audio_controller_post(c, 0, 9, 0, 23, 0, 0, clock + 192, 1, 1) == 0); // unmapped: ignored
    CHECK(__mlang_std_audio_controller_post(c, 0, 9, 0, 1, 128, 0, -1, 1, 1) == -1);
    CHECK(__mlang_std_audio_controller_process(c, b, 256) == 0);
    for(int f = 0; f < 256; ++f) {
        float expected = (f >= 32 && f < 64) || (f >= 128 && f < 160) ? 0.f : (f >= 96 ? .5f * 64.f / 127.f : .5f);
        CHECK(std::abs(__mlang_std_audio_pcm_block_sample(b, f, 0) - expected) < 1.e-7f);
    }
    // System exclusive messages reach the plugin as kMidiSysEx data events
    // at their frame, F0 and F7 stripped: 7D 7F, then 7D 20.
    {
        const int32_t full[] = {0xF0, 0x7D, 0x7F, 0xF7}, quarter[] = {0xF0, 0x7D, 0x20, 0xF7}, status[] = {0xF0, 0x7D, 0x90, 0xF7};
        CHECK(__mlang_std_audio_controller_instrument_sysex(c, 1, 0, IntList{4, full}) == 0);
        CHECK(__mlang_std_audio_controller_instrument_sysex(c, 1, 1, IntList{4, quarter}) == 0);
        CHECK(__mlang_std_audio_controller_instrument_sysex(c, 1, 2, IntList{4, status}) == -1);
        CHECK(__mlang_std_audio_controller_instrument_sysex(c, 1, 256, IntList{4, full}) == -1);
        CHECK(__mlang_std_audio_controller_instrument_sysex(c, 9, 0, IntList{4, full}) == -1); // empty slot
        CHECK(__mlang_std_audio_controller_instrument_sysex(c, 0, 0, IntList{4, full}) == -1); // no master processor
        clock = __mlang_std_audio_controller_info(c, 2);
        CHECK(__mlang_std_audio_controller_post(c, 0, 12, 0, 0, 0, 0, clock + 32, 1, 1) == 0);
        CHECK(__mlang_std_audio_controller_post(c, 0, 12, 0, 1, 0, 0, clock + 64, 1, 1) == 0);
        CHECK(__mlang_std_audio_controller_post(c, 0, 12, 0, 256, 0, 0, -1, 1, 1) == -1);
        CHECK(__mlang_std_audio_controller_process(c, b, 256) == 0);
        for(int f = 0; f < 256; ++f) {
            float expected = f < 32 ? .5f * 64.f / 127.f : (f < 64 ? .5f : .5f * 32.f / 127.f);
            CHECK(std::abs(__mlang_std_audio_pcm_block_sample(b, f, 0) - expected) < 1.e-6f);
        }
        // Back to CC74's 64 for the checks below.
        const int32_t half[] = {0xF0, 0x7D, 0x40, 0xF7};
        CHECK(__mlang_std_audio_controller_instrument_sysex(c, 1, 3, IntList{4, half}) == 0);
        CHECK(__mlang_std_audio_controller_post(c, 0, 12, 0, 3, 0, 0, -1, 1, 1) == 0);
        CHECK(__mlang_std_audio_controller_process(c, b, 256) == 0);
        CHECK(std::abs(__mlang_std_audio_pcm_block_sample(b, 0, 0) - .5f * 64.f / 127.f) < 1.e-6f);
        std::puts("PASS: system exclusive messages reach the instrument at their frame");
    }
    CHECK(__mlang_std_audio_controller_parameter_info(c, 1, 0, 0) == 40);
    CHECK(std::strcmp(__mlang_std_audio_controller_parameter_name(c, 1, 0), "Modulation") == 0);
    CHECK(__mlang_std_audio_controller_parameter_info(c, 1, 1, 2) == 64.0 / 127.0);
    CHECK(__mlang_std_audio_controller_parameter_info(c, 1, 3, 1) == 3);
    CHECK(__mlang_std_audio_controller_parameter_info(c, 1, 4, 3) == 1);
    CHECK(__mlang_std_audio_controller_set_parameter(c, 1, 3, 1.5) == -1);
    CHECK(__mlang_std_audio_controller_set_parameter(c, 1, 3, 4) == -1);
    CHECK(__mlang_std_audio_controller_set_parameter(c, 1, 4, 0.2) == -1);
    CHECK(__mlang_std_audio_controller_set_parameter(c, 1, 40, 0.2) == -1);
    CHECK(__mlang_std_audio_controller_set_parameter(c, 0, 0, 0.2) == -1);
    CHECK(__mlang_std_audio_controller_set_parameter(c, 1, 0, NAN) == -1);
    CHECK(__mlang_std_audio_controller_set_parameter(c, 1, 0, INFINITY) == -1);
    CHECK(__mlang_std_audio_controller_set_parameter(c, 1, 0, 1.01) == -1);
    CHECK(__mlang_std_audio_controller_set_parameter(c, 1, 0, 0.5) == 0);
    CHECK(__mlang_std_audio_controller_parameter_info(c, 1, 0, 2) == 0.5); // also cached without hardware rendering
    CHECK(__mlang_std_audio_controller_set_parameter(c, 1, 3, 2) == 0);
    CHECK(__mlang_std_audio_controller_process(c, b, 256) == 0);
    CHECK(__mlang_std_audio_controller_parameter_info(c, 1, 0, 2) == 0.5);
    CHECK(__mlang_std_audio_controller_parameter_info(c, 1, 3, 2) == 2.0 / 3.0);
    CHECK(std::abs(__mlang_std_audio_pcm_block_sample(b, 200, 0) - .25f * 64.f / 127.f) < 1.e-7f);
    CHECK(__mlang_std_audio_controller_stop(c) == 0);
    CHECK(__mlang_std_audio_controller_unload_instrument(c, 1) == 0);
    CHECK(__mlang_std_audio_controller_parameter_info(c, 1, 0, 0) == -1);
    CHECK(__mlang_std_audio_controller_set_parameter(c, 1, 0, 0.5) == -1);
    CHECK(*__mlang_std_audio_controller_instrument_name(c, 1) == 0);
    CHECK(*__mlang_std_audio_controller_instrument_name(c, 2) != 0);
    CHECK(__mlang_std_audio_controller_load_instrument(c, 1, argv[1]) == 0);
    // Empty parameter blocks preserve the plugin's last value; exercise idle processing.
    for(int i = 0; i < 20000; ++i) CHECK(__mlang_std_audio_controller_process(c, b, 256) == 0);
    CHECK(__mlang_std_audio_pcm_block_sample(b, 200, 0) == 0.f);
    CHECK(__mlang_std_audio_controller_load_processor(c, argv[2]) == 0);
    CHECK(__mlang_std_audio_controller_post(c, 0, 8, 0, 74, 0, 0, -1, 0, 1) == 0);
    CHECK(__mlang_std_audio_controller_process(c, b, 256) == 0);
    CHECK(__mlang_std_audio_pcm_block_sample(b, 200, 0) == 0.f);
    CHECK(__mlang_std_audio_controller_close(c) == 0);
    // Session restore must produce PCM, not merely expose cached parameters.
    c = __mlang_std_audio_controller_new(48000, 128);
    CHECK(__mlang_std_audio_controller_load_instrument(c, 1, argv[1]) == 0);
    const int count = (int)__mlang_std_audio_controller_parameter_info(c, 1, 0, 0);
    for(int i = 0; i < count; ++i) {
        double value = __mlang_std_audio_controller_parameter_info(c, 1, i, 2);
        CHECK(__mlang_std_audio_controller_restore_parameter(c, 1, i, value) == 0);
    }
    CHECK(__mlang_std_audio_controller_restore_parameter(c, 1, 0, 0.25) == 0);
    CHECK(__mlang_std_audio_controller_restore_parameter(c, 1, 0, 0.25) == 0);
    CHECK(__mlang_std_audio_controller_midi_target(c, 0, 1) == 0);
    CHECK(__mlang_std_audio_controller_live_note(c, 1, 0, 60, 127) == 0);
    CHECK(__mlang_std_audio_controller_process(c, b, 256) == 0);
    CHECK(__mlang_std_audio_pcm_block_sample(b, 200, 0) == 0.03125f);
    CHECK(__mlang_std_audio_controller_master_peak(c, 0) > 0);
    CHECK(__mlang_std_audio_controller_master_peak(c, 1) > 0);
    // Live CC and velocity reach the same processor as sequenced events.
    CHECK(__mlang_std_audio_controller_live_control(c, 0, 1, 127) == 0);
    CHECK(__mlang_std_audio_controller_live_note(c, 1, 0, 60, 32) == 0);
    CHECK(__mlang_std_audio_controller_process(c, b, 256) == 0);
    const float soft = __mlang_std_audio_pcm_block_sample(b, 200, 0);
    CHECK(soft > 0);
    __mlang_std_audio_controller_instrument_peak(c, 1, 0);
    __mlang_std_audio_controller_instrument_peak(c, 1, 1);
    CHECK(__mlang_std_audio_controller_live_note(c, 1, 0, 60, 96) == 0);
    CHECK(__mlang_std_audio_controller_process(c, b, 256) == 0);
    const float hard = __mlang_std_audio_pcm_block_sample(b, 200, 0);
    CHECK(std::fabs(hard - soft * 3) < 1.e-7f);
    const int pcmLeft = __mlang_std_audio_controller_instrument_peak(c, 1, 0);
    const int pcmRight = __mlang_std_audio_controller_instrument_peak(c, 1, 1);
    CHECK(pcmLeft > 0 && pcmRight == pcmLeft);
    CHECK(std::abs(pcmLeft - (int)(hard * 4000)) <= 1); // before master gain .25
    CHECK(__mlang_std_audio_controller_instrument_peak(c, 1, 0) == 0);
    CHECK(__mlang_std_audio_controller_instrument_peak(c, 2, 0) == 0);
    CHECK(__mlang_std_audio_controller_instrument_peak(c, 0, 0) == 0);
    CHECK(__mlang_std_audio_controller_instrument_peak(c, 1, 2) == 0);
    CHECK(__mlang_std_audio_controller_track_volume(c, 0, 1, 50) == 0);
    CHECK(__mlang_std_audio_controller_process(c, b, 256) == 0);
    CHECK(std::fabs(__mlang_std_audio_pcm_block_sample(b, 200, 0) - hard * .5f) < 1.e-7f);
    CHECK(std::abs(__mlang_std_audio_controller_instrument_peak(c, 1, 0) * 2 - pcmLeft) <= 1);
    CHECK(std::abs(__mlang_std_audio_controller_instrument_peak(c, 1, 1) * 2 - pcmRight) <= 1);
    CHECK(__mlang_std_audio_controller_track_volume(c, 0, 1, 100) == 0);
    CHECK(__mlang_std_audio_controller_track_volume(c, 0, 1, 101) == -1);
    CHECK(__mlang_std_audio_controller_live_control(c, 0, 1, 0) == 0);
    CHECK(__mlang_std_audio_controller_process(c, b, 256) == 0);
    CHECK(__mlang_std_audio_pcm_block_sample(b, 200, 0) == 0.f);
    CHECK(__mlang_std_audio_controller_live_control(c, 0, 1, 127) == 0);
    CHECK(__mlang_std_audio_controller_process(c, b, 256) == 0);
    CHECK(__mlang_std_audio_pcm_block_sample(b, 200, 0) == hard);
    CHECK(__mlang_std_audio_controller_live_control(c, 0, 129, 0) == 0);
    CHECK(__mlang_std_audio_controller_process(c, b, 256) == 0);
    CHECK(__mlang_std_audio_pcm_block_sample(b, 200, 0) == 0.f);
    CHECK(__mlang_std_audio_controller_live_control(c, 0, 129, 16383) == 0);
    CHECK(__mlang_std_audio_controller_process(c, b, 256) == 0);
    CHECK(__mlang_std_audio_pcm_block_sample(b, 200, 0) == hard);
    CHECK(__mlang_std_audio_controller_load_instrument(c, 2, argv[1]) == 0);
    CHECK(__mlang_std_audio_controller_midi_target(c, 1, 2) == 0);
    CHECK(__mlang_std_audio_controller_live_control(c, 3, 1, 64) == 0);
    CHECK(__mlang_std_audio_controller_process(c, b, 256) == 0);
    CHECK(__mlang_std_audio_controller_parameter_info(c, 1, 0, 2) == 1.0);
    CHECK(__mlang_std_audio_controller_parameter_info(c, 2, 0, 2) == 64.0 / 127.0);
    CHECK(__mlang_std_audio_controller_midi_target(c, 1, -1) == 0);
    CHECK(__mlang_std_audio_controller_live_control(c, 3, 1, 0) == 0);
    CHECK(__mlang_std_audio_controller_process(c, b, 256) == 0);
    CHECK(__mlang_std_audio_controller_parameter_info(c, 2, 0, 2) == 64.0 / 127.0);
    CHECK(__mlang_std_audio_controller_live_control(c, 16, 1, 0) == -1);
    // 128 is aftertouch (channel pressure), 0..127; nothing sits above pitch bend.
    CHECK(__mlang_std_audio_controller_live_control(c, 0, 128, 64) == 0);
    CHECK(__mlang_std_audio_controller_live_control(c, 0, 128, 128) == -1);
    CHECK(__mlang_std_audio_controller_live_control(c, 0, 130, 0) == -1);
    CHECK(__mlang_std_audio_controller_live_control(c, 0, 1, 128) == -1);
    CHECK(__mlang_std_audio_controller_live_control(c, 0, 129, 16384) == -1);
    // Learn is global to this controller/session, not the selected track.
    CHECK(__mlang_std_audio_controller_midi_learn(c, -1, 2, 0) == 0);
    CHECK(__mlang_std_audio_controller_midi_learn_info(c, -1, 0) == 2);
    CHECK(__mlang_std_audio_controller_live_control(c, 3, 7, 32) == 0);
    CHECK(__mlang_std_audio_controller_midi_learn_info(c, -1, 0) == 0);
    CHECK(__mlang_std_audio_controller_midi_learn_info(c, 3 * 128 + 7, 0) == 2);
    CHECK(__mlang_std_audio_controller_process(c, b, 256) == 0);
    CHECK(__mlang_std_audio_controller_parameter_info(c, 2, 0, 2) == 32.0 / 127.0);
    CHECK(__mlang_std_audio_controller_midi_target(c, 0, 1) == 0);
    CHECK(__mlang_std_audio_controller_live_control(c, 3, 7, 127) == 0);
    CHECK(__mlang_std_audio_controller_live_control(c, 2, 7, 0) == 0); // different channel, not bound
    CHECK(__mlang_std_audio_controller_process(c, b, 256) == 0);
    CHECK(__mlang_std_audio_controller_parameter_info(c, 2, 0, 2) == 1);
    // A pattern CC for the learned instrument drives the learned parameter,
    // whatever channel its track plays on; other instruments keep raw CCs.
    CHECK(__mlang_std_audio_controller_post(c, 0, 9, 5, 7, 32, 1, -1, 2, 1) == 0);
    CHECK(__mlang_std_audio_controller_process(c, b, 256) == 0);
    CHECK(__mlang_std_audio_controller_parameter_info(c, 2, 0, 2) == 32.0 / 127.0);
    CHECK(__mlang_std_audio_controller_post(c, 0, 9, 5, 7, 127, 1, -1, 1, 1) == 0);
    CHECK(__mlang_std_audio_controller_post(c, 1, 9, 5, 7, 127, 65537, -1, 2, 1) == 0); // live, unbound channel
    CHECK(__mlang_std_audio_controller_process(c, b, 256) == 0);
    CHECK(__mlang_std_audio_controller_parameter_info(c, 2, 0, 2) == 32.0 / 127.0);
    CHECK(__mlang_std_audio_controller_live_control(c, 3, 7, 127) == 0);
    CHECK(__mlang_std_audio_controller_process(c, b, 256) == 0);
    CHECK(__mlang_std_audio_controller_parameter_info(c, 2, 0, 2) == 1);
    CHECK(__mlang_std_audio_controller_midi_learn(c, -1, 2, 4) == -1); // read-only
    CHECK(__mlang_std_audio_controller_midi_learn(c, -1, 2, 40) == -1);
    CHECK(__mlang_std_audio_controller_midi_learn(c, 2048, 2, 0) == -1);
    CHECK(__mlang_std_audio_controller_midi_learn(c, -1, 2, 3) == 0);
    CHECK(__mlang_std_audio_controller_live_control(c, 3, 7, 64) == 0); // replaces binding, quantized
    CHECK(__mlang_std_audio_controller_process(c, b, 256) == 0);
    CHECK(__mlang_std_audio_controller_parameter_info(c, 2, 3, 2) == 2.0 / 3.0);
    CHECK(__mlang_std_audio_controller_midi_learn_info(c, 3 * 128 + 7, 1) == 3);
    CHECK(__mlang_std_audio_controller_midi_learn(c, -1, 2, 1) == 0);
    CHECK(__mlang_std_audio_controller_midi_learn(c, -1, 0, 0) == 0); // cancel
    CHECK(__mlang_std_audio_controller_live_control(c, 3, 8, 64) == 0);
    CHECK(__mlang_std_audio_controller_midi_learn_info(c, 3 * 128 + 8, 0) == 0);
    CHECK(__mlang_std_audio_controller_midi_learn(c, -1, 2, 1) == 0);
    CHECK(__mlang_std_audio_controller_unload_instrument(c, 2) == 0);
    CHECK(__mlang_std_audio_controller_midi_learn_info(c, -1, 0) == 0);
    CHECK(__mlang_std_audio_controller_midi_learn_info(c, 3 * 128 + 7, 0) == 0);
    CHECK(__mlang_std_audio_controller_close(c) == 0);
    // Half wet: dry .125 * .5 + send(.5) * effect(.5) * return(1) => .09375.
    c = __mlang_std_audio_controller_new(48000, 128);
    CHECK(__mlang_std_audio_controller_load_instrument(c, 1, argv[1]) == 0);
    CHECK(__mlang_std_audio_controller_load_effect(c, 0, argv[2]) == 0);
    CHECK(__mlang_std_audio_controller_load_effect(c, 0, argv[1]) == -1); // preserve prior effect
    CHECK(__mlang_std_audio_controller_parameter_info(c, 33, 0, 0) == 40);
    CHECK(__mlang_std_audio_controller_effect_send(c, 0, 1, 0, 50) == 0);
    CHECK(__mlang_std_audio_controller_midi_target(c, 0, 1) == 0);
    CHECK(__mlang_std_audio_controller_live_note(c, 1, 0, 60, 127) == 0);
    CHECK(__mlang_std_audio_controller_process(c, b, 256) == 0);
    CHECK(std::abs(__mlang_std_audio_pcm_block_sample(b, 200, 0) - .09375f) < 1.e-7f);
    CHECK(__mlang_std_audio_controller_effect_peak(c, 0, 0) == 125);
    CHECK(__mlang_std_audio_controller_effect_peak(c, 0, 1) == 125);
    CHECK(__mlang_std_audio_controller_effect_peak(c, 0, 0) == 0);
    CHECK(__mlang_std_audio_controller_effect_volume(c, 0, 50) == 0);
    CHECK(__mlang_std_audio_controller_process(c, b, 256) == 0);
    CHECK(std::abs(__mlang_std_audio_pcm_block_sample(b, 200, 0) - .078125f) < 1.e-7f);
    CHECK(__mlang_std_audio_controller_set_parameter(c, 33, 0, .5) == 0);
    CHECK(__mlang_std_audio_controller_process(c, b, 256) == 0);
    CHECK(std::abs(__mlang_std_audio_pcm_block_sample(b, 200, 0) - .0703125f) < 1.e-7f);
    CHECK(__mlang_std_audio_controller_effect_send(c, 0, 1, 0, 100) == 0);
    CHECK(__mlang_std_audio_controller_process(c, b, 256) == 0);
    CHECK(std::abs(__mlang_std_audio_pcm_block_sample(b, 200, 0) - .015625f) < 1.e-7f); // no dry signal
    // The sequencer transport reaches the effect's ProcessContext: 120 BPM halves it.
    CHECK(__mlang_std_audio_controller_transport(c, 120, 4, 1) == 0);
    CHECK(__mlang_std_audio_controller_process(c, b, 256) == 0);
    CHECK(std::abs(__mlang_std_audio_pcm_block_sample(b, 200, 0) - .0078125f) < 1.e-7f);
    CHECK(std::abs(__mlang_std_audio_controller_transport_beat(c) - (4 + 256 * 2 / 48000.0)) < 1.e-9);
    CHECK(__mlang_std_audio_controller_transport(c, 120, -1, 0) == 0);
    CHECK(__mlang_std_audio_controller_process(c, b, 256) == 0);
    CHECK(std::abs(__mlang_std_audio_pcm_block_sample(b, 200, 0) - .015625f) < 1.e-7f); // stopped
    CHECK(__mlang_std_audio_controller_effect_send(c, 0, 1, 0, 0) == 0);
    CHECK(__mlang_std_audio_controller_process(c, b, 256) == 0);
    CHECK(__mlang_std_audio_pcm_block_sample(b, 200, 0) == .125f); // dry unchanged
    CHECK(__mlang_std_audio_controller_effect_send(c, 64, 1, 0, 50) == -1);
    CHECK(__mlang_std_audio_controller_effect_send(c, 0, 1, 8, 50) == -1);
    CHECK(__mlang_std_audio_controller_effect_send(c, 0, 1, 0, 101) == -1);
    CHECK(__mlang_std_audio_controller_load_effect(c, 8, argv[2]) == -1);
    CHECK(__mlang_std_audio_controller_load_effect(c, 0, "") == 0);
    CHECK(__mlang_std_audio_controller_effect_send(c, 0, 1, 0, 100) == 0);
    CHECK(__mlang_std_audio_controller_live_note(c, 1, 0, 60, 127) == 0);
    CHECK(__mlang_std_audio_controller_process(c, b, 256) == 0);
    CHECK(__mlang_std_audio_pcm_block_sample(b, 200, 0) == .125f); // empty FX preserves dry
    CHECK(__mlang_std_audio_controller_parameter_info(c, 33, 0, 0) == -1);
    CHECK(__mlang_std_audio_controller_close(c) == 0);
    c = __mlang_std_audio_controller_new(48000, 128);
    CHECK(__mlang_std_audio_controller_load_instrument(c, 1, argv[1]) == 0);
    for(int n = 0; n < 4; ++n) {
        CHECK(__mlang_std_audio_controller_load_insert(c, n + 1, argv[2]) == 0);
        CHECK(__mlang_std_audio_controller_insert_route(c, 64, n, n + 1) == 0);
    }
    CHECK(__mlang_std_audio_controller_load_insert(c, 1, argv[1]) == -1);
    CHECK(__mlang_std_audio_controller_parameter_info(c, 41, 0, 0) == 40);
    CHECK(__mlang_std_audio_controller_insert_route(c, 64, 4, 1) == -1);
    CHECK(__mlang_std_audio_controller_midi_target(c, 0, 1) == 0);
    CHECK(__mlang_std_audio_controller_live_note(c, 1, 0, 60, 127) == 0);
    CHECK(__mlang_std_audio_controller_process(c, b, 256) == 0);
    CHECK(std::abs(__mlang_std_audio_pcm_block_sample(b, 200, 0) - .0078125f) < 1.e-7f);
    CHECK(__mlang_std_audio_controller_set_parameter(c, 41, 0, .5) == 0);
    CHECK(__mlang_std_audio_controller_process(c, b, 256) == 0);
    CHECK(std::abs(__mlang_std_audio_pcm_block_sample(b, 200, 0) - .00390625f) < 1.e-7f);
    CHECK(__mlang_std_audio_controller_insert_route(c, 64, 0, 0) == 0);
    CHECK(__mlang_std_audio_controller_process(c, b, 256) == 0);
    CHECK(std::abs(__mlang_std_audio_pcm_block_sample(b, 200, 0) - .015625f) < 1.e-7f);
    CHECK(__mlang_std_audio_controller_close(c) == 0);
    c = __mlang_std_audio_controller_new(48000, 128);
    CHECK(__mlang_std_audio_controller_load_insert(c, 1, argv[2]) == 0);
    CHECK(__mlang_std_audio_controller_insert_route(c, 0, 0, 1) == 0);
    std::vector<float> pcm(2048, .5f);
    CHECK(__mlang_std_audio_controller_sample_data(c, FloatList{2048, pcm.data()}, 1, 48000) == 0);
    CHECK(__mlang_std_audio_controller_post(c, 0, 4, 0, 0, 0, 0, -1, 0, 1) == 0);
    CHECK(__mlang_std_audio_controller_post(c, 0, 4, 0, 0, 0, 1, -1, 0, 1) == 0);
    CHECK(__mlang_std_audio_controller_process(c, b, 256) == 0);
    CHECK(std::abs(__mlang_std_audio_pcm_block_sample(b, 200, 0) - .1875f) < 1.e-7f); // one wet track, one unaffected
    CHECK(__mlang_std_audio_controller_track_volume(c, 0, 0, 0) == 0);
    CHECK(__mlang_std_audio_controller_process(c, b, 256) == 0);
    CHECK(__mlang_std_audio_pcm_block_sample(b, 200, 0) == .125f); // mute also mutes insert output
    CHECK(__mlang_std_audio_controller_close(c) == 0);
    // Mla Delay with Tempo Source = Host follows the sequencer tempo mlacker
    // sends: a one-beat echo lands 500 ms after an impulse at 120 BPM and
    // 250 ms after it at 240 BPM.
    if(const char *delay = std::getenv("MLA_DELAY_VST3")) {
        const auto echo_after = [&](double bpm) -> int64_t {
            int64_t d = __mlang_std_audio_controller_new(48000, 128);
            // As aux effect 1 (parameter slot 33), fed only by track 0's send.
            CHECK(__mlang_std_audio_controller_load_effect(d, 0, delay) == 0);
            CHECK(__mlang_std_audio_controller_effect_send(d, 0, 0, 0, 100) == 0);
            // Parameter indices follow the plugin's IDs 100 + index; stepped
            // controls take their step, continuous ones 0-1.
            CHECK(__mlang_std_audio_controller_set_parameter(d, 33, 0, 0) == 0);            // Forward
            CHECK(__mlang_std_audio_controller_set_parameter(d, 33, 2, 0) == 0);            // no feedback
            CHECK(__mlang_std_audio_controller_set_parameter(d, 33, 3, 1) == 0);            // wet only
            CHECK(__mlang_std_audio_controller_set_parameter(d, 33, 4, 0) == 0);            // no filter
            CHECK(__mlang_std_audio_controller_set_parameter(d, 33, 10, 2) == 0);           // Tempo Source: Host
            CHECK(__mlang_std_audio_controller_set_parameter(d, 33, 12, (1 - .0625) / (16 - .0625)) == 0); // 1 beat
            CHECK(__mlang_std_audio_controller_transport(d, bpm, 0, 1) == 0);
            // Let the delay-time and mix ramps settle before the impulse.
            for(int i = 0; i < 40; ++i) CHECK(__mlang_std_audio_controller_process(d, b, 256) == 0);
            // 10 ms: sample voices fade in over 3 ms, so a single-frame click
            // would be nearly silent.
            std::vector<float> click(480, 1.f);
            CHECK(__mlang_std_audio_controller_sample_data(d, FloatList{480, click.data()}, 1, 48000) == 0);
            const int64_t start = __mlang_std_audio_controller_info(d, 2);
            CHECK(__mlang_std_audio_controller_post(d, 0, 4, 0, 0, 0, 0, start, 0, 1) == 0);
            int64_t found = -1;
            for(int block = 0; block < 160 && found < 0; ++block) {
                CHECK(__mlang_std_audio_controller_process(d, b, 256) == 0);
                for(int f = 0; f < 256 && found < 0; ++f)
                    if(std::abs(__mlang_std_audio_pcm_block_sample(b, f, 0)) > 1.e-4f) found = start + block * 256 + f;
            }
            CHECK(__mlang_std_audio_controller_close(d) == 0);
            return found < 0 ? -1 : found - start;
        };
        const int64_t slow = echo_after(120), fast = echo_after(240);
        CHECK(std::llabs(slow - 24000) <= 2 && std::llabs(fast - 12000) <= 2);
        std::puts("PASS: Mla Delay follows the host tempo");
    }
    // Mla Vocoder has a sidechain (aux) input bus next to its main input; the
    // host loads it as an instrument and as an effect, leaving the sidechain
    // inactive. As an instrument it takes a PCM track's voices as its input:
    // a sample on track 0 (fader down, so only the vocoder is heard) and a
    // note on the vocoder vocode only while the input route is set.
    if(const char *vocoder = std::getenv("MLA_VOCODER_VST3")) {
        int64_t d = __mlang_std_audio_controller_new(48000, 128);
        CHECK(__mlang_std_audio_controller_load_instrument(d, 1, vocoder) == 0);
        CHECK(std::strcmp(__mlang_std_audio_controller_instrument_name(d, 1), "Mla Vocoder") == 0);
        CHECK(__mlang_std_audio_controller_load_effect(d, 0, vocoder) == 0);
        CHECK(__mlang_std_audio_controller_load_effect(d, 0, "") == 0);
        CHECK(__mlang_std_audio_controller_instrument_input(d, 1, 66) == -1);
        CHECK(__mlang_std_audio_controller_instrument_input(d, 33, 1) == -1);
        std::vector<float> voice(48000 * 4);
        for(size_t i = 0; i < voice.size(); ++i) voice[i] = 0.5f * (2.f * std::fmod(150.f * i / 48000.f, 1.f) - 1.f);
        CHECK(__mlang_std_audio_controller_sample_data(d, FloatList{(int64_t)voice.size(), voice.data()}, 1, 48000) == 0);
        CHECK(__mlang_std_audio_controller_track_volume(d, 0, 0, 0) == 0);
        const auto level = [&](int64_t input) -> double {
            CHECK(__mlang_std_audio_controller_instrument_input(d, 1, input) == 0);
            __mlang_std_audio_controller_panic(d);
            CHECK(__mlang_std_audio_controller_process(d, b, 256) == 0);
            const int64_t at = __mlang_std_audio_controller_info(d, 2);
            CHECK(__mlang_std_audio_controller_post(d, 0, 4, 0, 0, 0, 0, at, 0, 1) == 0);
            CHECK(__mlang_std_audio_controller_post(d, 0, 6, 0, 48, 100, 0, at, 1, 1) == 0);
            double sum = 0; int count = 0;
            for(int block = 0; block < 120; ++block) {
                CHECK(__mlang_std_audio_controller_process(d, b, 256) == 0);
                if(block < 40) continue;
                for(int f = 0; f < 256; ++f) { double v = __mlang_std_audio_pcm_block_sample(b, f, 0); sum += v * v; ++count; }
            }
            return std::sqrt(sum / count);
        };
        const double unrouted = level(0), routed = level(1);
        std::printf("vst3_host: Mla Vocoder rms without input %.6f, fed by track 1 %.6f\n", unrouted, routed);
        CHECK(unrouted < 1.e-4);
        CHECK(routed > 0.01);
        // The live input (65), fed block by block at 44.1 kHz as a device on
        // its own clock would, resampled to the controller's 48 kHz.
        CHECK(__mlang_std_audio_controller_input_peak(d, 0) == 0);
        CHECK(__mlang_std_audio_controller_instrument_input(d, 1, 65) == 0);
        __mlang_std_audio_controller_panic(d);
        CHECK(__mlang_std_audio_controller_process(d, b, 256) == 0);
        const int64_t at = __mlang_std_audio_controller_info(d, 2);
        CHECK(__mlang_std_audio_controller_post(d, 0, 6, 0, 48, 100, 0, at, 1, 1) == 0);
        std::vector<float> chunk(235);
        int64_t fed = 0; double sum = 0; int count = 0;
        for(int block = 0; block < 120; ++block) {
            for(auto &v : chunk) { v = 0.5f * (2.f * std::fmod(150.f * fed / 44100.f, 1.f) - 1.f); ++fed; }
            CHECK(__mlang_std_audio_controller_input_feed(d, FloatList{(int64_t)chunk.size(), chunk.data()}, 1, 44100) == 0);
            CHECK(__mlang_std_audio_controller_process(d, b, 256) == 0);
            if(block < 40) continue;
            for(int f = 0; f < 256; ++f) { double v = __mlang_std_audio_pcm_block_sample(b, f, 0); sum += v * v; ++count; }
        }
        const double live = std::sqrt(sum / count);
        std::printf("vst3_host: Mla Vocoder rms fed by the live input %.6f\n", live);
        CHECK(live > 0.01);
        CHECK(__mlang_std_audio_controller_input_peak(d, 0) >= 490);
        CHECK(__mlang_std_audio_controller_input_peak(d, 0) == 0);
        CHECK(__mlang_std_audio_controller_input_feed(d, FloatList{2, chunk.data()}, 3, 44100) == -1);
        CHECK(__mlang_std_audio_controller_close(d) == 0);
        std::puts("PASS: Mla Vocoder loads with its sidechain bus and takes a track as input");
    }
    // Mla Speech speaks the phrase a text event attaches to the next note-on
    // (mlacker's TEXT cells): the host stores the phrase, then sends it as a
    // note-expression text event of that note. Longer words speak longer, and
    // the phrase outlasts the note.
    if(const char *speech = std::getenv("MLA_SPEECH_VST3")) {
        int64_t d = __mlang_std_audio_controller_new(48000, 128);
        CHECK(__mlang_std_audio_controller_load_instrument(d, 1, speech) == 0);
        CHECK(std::strcmp(__mlang_std_audio_controller_instrument_name(d, 1), "Mla Speech") == 0);
        CHECK(__mlang_std_audio_controller_instrument_text(d, 1, 256, "x") == -1);
        CHECK(__mlang_std_audio_controller_instrument_text(d, 2, 0, "x") == -1);
        CHECK(__mlang_std_audio_controller_post(d, 2, 11, 0, 256, 0, 0, -1, 1, 0) == -1);
        const auto spoken = [&](int64_t phrase, const char *words) -> double {
            CHECK(__mlang_std_audio_controller_instrument_text(d, 1, phrase, words) == 0);
            __mlang_std_audio_controller_panic(d);
            CHECK(__mlang_std_audio_controller_process(d, b, 256) == 0);
            const int64_t at = __mlang_std_audio_controller_info(d, 2) + 64;
            CHECK(__mlang_std_audio_controller_post(d, 2, 11, 3, phrase, 0, 3, at, 1, 0) == 0);
            CHECK(__mlang_std_audio_controller_post(d, 2, 6, 3, 48, 100, 3, at, 1, 1) == 0);
            CHECK(__mlang_std_audio_controller_post(d, 2, 7, 3, 48, 0, 3, at + 256, 1, 1) == 0);
            int last = 0;
            for(int block = 0; block < 48000 * 8 / 256; ++block) {
                CHECK(__mlang_std_audio_controller_process(d, b, 256) == 0);
                double sum = 0;
                for(int f = 0; f < 256; ++f) { double v = __mlang_std_audio_pcm_block_sample(b, f, 0); sum += v * v; }
                if(std::sqrt(sum / 256) > 1e-3) last = block + 1;
            }
            return last * 256 / 48000.0;
        };
        const double hi = spoken(0, "Hi."), sentence = spoken(1, "This is the Atari speech synthesizer, talking from a tracker.");
        std::printf("vst3_host: Mla Speech \"Hi.\" %.2f s, sentence %.2f s\n", hi, sentence);
        CHECK(hi > 0.1 && hi < 0.9);
        CHECK(sentence > 2.5 && sentence < 7.5);
        // A note without words repeats the last phrase.
        __mlang_std_audio_controller_panic(d);
        CHECK(__mlang_std_audio_controller_process(d, b, 256) == 0);
        CHECK(__mlang_std_audio_controller_post(d, 2, 6, 3, 60, 100, 3, __mlang_std_audio_controller_info(d, 2), 1, 1) == 0);
        int last = 0;
        for(int block = 0; block < 48000 * 8 / 256; ++block) {
            CHECK(__mlang_std_audio_controller_process(d, b, 256) == 0);
            double sum = 0;
            for(int f = 0; f < 256; ++f) { double v = __mlang_std_audio_pcm_block_sample(b, f, 0); sum += v * v; }
            if(std::sqrt(sum / 256) > 1e-3) last = block + 1;
        }
        CHECK(std::fabs(last * 256 / 48000.0 - sentence) < 0.5);
        CHECK(__mlang_std_audio_controller_close(d) == 0);
        std::puts("PASS: Mla Speech speaks the text attached to a note");
    }
    // Mla 06 plays its drums on General MIDI keys from an instrument track:
    // a kick starts sample-accurately and rings out on its own (note-offs
    // are ignored); keys outside the kit stay silent.
    if(const char *m06 = std::getenv("MLA_06_VST3")) {
        int64_t d = __mlang_std_audio_controller_new(48000, 128);
        CHECK(__mlang_std_audio_controller_load_instrument(d, 1, m06) == 0);
        CHECK(std::strcmp(__mlang_std_audio_controller_instrument_name(d, 1), "Mla 06") == 0);
        CHECK(__mlang_std_audio_controller_process(d, b, 256) == 0);
        const int64_t at = __mlang_std_audio_controller_info(d, 2) + 64;
        CHECK(__mlang_std_audio_controller_post(d, 2, 6, 9, 60, 100, 3, at, 1, 1) == 0);
        CHECK(__mlang_std_audio_controller_post(d, 2, 6, 9, 36, 100, 3, at + 32, 1, 1) == 0);
        CHECK(__mlang_std_audio_controller_post(d, 2, 7, 9, 36, 0, 3, at + 64, 1, 1) == 0);
        CHECK(__mlang_std_audio_controller_process(d, b, 256) == 0);
        double before = 0, after = 0;
        for(int f = 0; f < 96; ++f) before = std::max(before, std::fabs(static_cast<double>(__mlang_std_audio_pcm_block_sample(b, f, 0))));
        for(int f = 96; f < 256; ++f) after = std::max(after, std::fabs(static_cast<double>(__mlang_std_audio_pcm_block_sample(b, f, 0))));
        CHECK(before == 0.0 && after > 1e-3);
        // Still ringing 100 ms later, despite the note-off.
        for(int block = 0; block < 18; ++block) CHECK(__mlang_std_audio_controller_process(d, b, 256) == 0);
        double ring = 0;
        for(int f = 0; f < 256; ++f) ring = std::max(ring, std::fabs(static_cast<double>(__mlang_std_audio_pcm_block_sample(b, f, 0))));
        CHECK(ring > 1e-3);
        CHECK(__mlang_std_audio_controller_close(d) == 0);
        std::puts("PASS: Mla 06 plays its kit from instrument notes");
    }
    // Mla 08 plays its drums on General MIDI keys from an instrument track:
    // a kick starts sample-accurately and rings out on its own (note-offs
    // are ignored); keys outside the kit stay silent.
    if(const char *m08 = std::getenv("MLA_08_VST3")) {
        int64_t d = __mlang_std_audio_controller_new(48000, 128);
        CHECK(__mlang_std_audio_controller_load_instrument(d, 1, m08) == 0);
        CHECK(std::strcmp(__mlang_std_audio_controller_instrument_name(d, 1), "Mla 08") == 0);
        CHECK(__mlang_std_audio_controller_process(d, b, 256) == 0);
        const int64_t at = __mlang_std_audio_controller_info(d, 2) + 64;
        CHECK(__mlang_std_audio_controller_post(d, 2, 6, 9, 60, 100, 3, at, 1, 1) == 0);
        CHECK(__mlang_std_audio_controller_post(d, 2, 6, 9, 36, 100, 3, at + 32, 1, 1) == 0);
        CHECK(__mlang_std_audio_controller_post(d, 2, 7, 9, 36, 0, 3, at + 64, 1, 1) == 0);
        CHECK(__mlang_std_audio_controller_process(d, b, 256) == 0);
        double before = 0, after = 0;
        for(int f = 0; f < 96; ++f) before = std::max(before, std::fabs(static_cast<double>(__mlang_std_audio_pcm_block_sample(b, f, 0))));
        for(int f = 96; f < 256; ++f) after = std::max(after, std::fabs(static_cast<double>(__mlang_std_audio_pcm_block_sample(b, f, 0))));
        CHECK(before == 0.0 && after > 1e-3);
        // Still ringing 100 ms later, despite the note-off.
        for(int block = 0; block < 18; ++block) CHECK(__mlang_std_audio_controller_process(d, b, 256) == 0);
        double ring = 0;
        for(int f = 0; f < 256; ++f) ring = std::max(ring, std::fabs(static_cast<double>(__mlang_std_audio_pcm_block_sample(b, f, 0))));
        CHECK(ring > 1e-3);
        CHECK(__mlang_std_audio_controller_close(d) == 0);
        std::puts("PASS: Mla 08 plays its kit from instrument notes");
    }
    // Mla SID plays notes from an instrument track, sample-accurately, and
    // an instrument CC writes a SID register: CC 44 is $D418 (Mode/Vol), so
    // CC 44 = 0 turns the volume to 0.
    if(const char *sid = std::getenv("MLA_SID_VST3")) {
        int64_t d = __mlang_std_audio_controller_new(48000, 128);
        CHECK(__mlang_std_audio_controller_load_instrument(d, 1, sid) == 0);
        CHECK(std::strcmp(__mlang_std_audio_controller_instrument_name(d, 1), "Mla SID") == 0);
        CHECK(__mlang_std_audio_controller_process(d, b, 256) == 0);
        const int64_t at = __mlang_std_audio_controller_info(d, 2) + 64;
        CHECK(__mlang_std_audio_controller_post(d, 2, 6, 0, 57, 100, 3, at + 32, 1, 1) == 0);
        CHECK(__mlang_std_audio_controller_process(d, b, 256) == 0);
        double before = 0, after = 0;
        for(int f = 0; f < 96; ++f) before = std::max(before, std::fabs(static_cast<double>(__mlang_std_audio_pcm_block_sample(b, f, 0))));
        for(int f = 96; f < 256; ++f) after = std::max(after, std::fabs(static_cast<double>(__mlang_std_audio_pcm_block_sample(b, f, 0))));
        CHECK(before < 1e-6 && after > 1e-3);
        const int64_t later = __mlang_std_audio_controller_info(d, 2);
        CHECK(__mlang_std_audio_controller_post(d, 2, 9, 0, 44, 0, 3, later, 1, 1) == 0);
        for(int block = 0; block < 40; ++block) CHECK(__mlang_std_audio_controller_process(d, b, 256) == 0);
        double rest = 0;
        for(int f = 0; f < 256; ++f) rest = std::max(rest, std::fabs(static_cast<double>(__mlang_std_audio_pcm_block_sample(b, f, 0))));
        CHECK(rest < 1e-3);
        CHECK(__mlang_std_audio_controller_close(d) == 0);
        std::puts("PASS: Mla SID plays notes and takes register CCs");
    }
    // Mla Sampler has ten output buses: Main, Out 2-8, Send A and Send B.
    // Its aux buses follow the main output until routed to master, a PCM
    // track's channel, an aux effect channel's input or nowhere.
    if(const char *sampler = std::getenv("MLA_SAMPLER_VST3")) {
        int64_t d = __mlang_std_audio_controller_new(48000, 128);
        CHECK(__mlang_std_audio_controller_load_instrument(d, 1, sampler) == 0);
        CHECK(std::strcmp(__mlang_std_audio_controller_instrument_name(d, 1), "Mla Sampler") == 0);
        CHECK(__mlang_std_audio_controller_instrument_sampler(d, 1, 1) == 16);
        CHECK(__mlang_std_audio_controller_instrument_outputs(d, 1) == 10);
        CHECK(__mlang_std_audio_controller_instrument_outputs(d, 2) == -1);
        CHECK(std::strcmp(__mlang_std_audio_controller_instrument_output_name(d, 1, 0), "Main") == 0);
        CHECK(std::strcmp(__mlang_std_audio_controller_instrument_output_name(d, 1, 1), "Out 2") == 0);
        CHECK(std::strcmp(__mlang_std_audio_controller_instrument_output_name(d, 1, 8), "Send A") == 0);
        CHECK(std::strcmp(__mlang_std_audio_controller_instrument_output_name(d, 1, 10), "") == 0);
        CHECK(__mlang_std_audio_controller_instrument_output_route(d, 1, 0, 0) == -1); // main is not an aux bus
        CHECK(__mlang_std_audio_controller_instrument_output_route(d, 1, 1, 74) == -1);
        const std::vector<float> hit_pcm(4800, 0.5f);
        CHECK(__mlang_std_audio_controller_instrument_pad(d, 1, 0, FloatList{4800, hit_pcm.data()}, 1, 48000, "hit") == 0);
        // Parameter 11 is Slot 1 Output (ID 203), stepped: 1 = Out 2.
        CHECK(__mlang_std_audio_controller_parameter_info(d, 1, 11, 4) == 203);
        CHECK(__mlang_std_audio_controller_set_parameter(d, 1, 11, 1) == 0);
        CHECK(__mlang_std_audio_controller_process(d, b, 256) == 0);
        const int64_t now = __mlang_std_audio_controller_info(d, 2);
        CHECK(__mlang_std_audio_controller_post(d, 0, 6, 0, 36, 127, 0, now + 32, 1, 1) == 0);
        CHECK(__mlang_std_audio_controller_process(d, b, 256) == 0);
        // Out 2 follows Main: 0.5 through the controller's 0.25 master gain.
        CHECK(std::abs(__mlang_std_audio_pcm_block_sample(b, 128, 0) - 0.125f) < 1.e-4f);
        // Release 1 ms (parameter 7): the panic before each hit ends the last
        // one. A panic drops queued events, so render the change first.
        CHECK(__mlang_std_audio_controller_set_parameter(d, 1, 7, 0) == 0);
        CHECK(__mlang_std_audio_controller_process(d, b, 256) == 0);
        const auto hit = [&]() -> float {
            __mlang_std_audio_controller_panic(d);
            CHECK(__mlang_std_audio_controller_process(d, b, 256) == 0);
            const int64_t at = __mlang_std_audio_controller_info(d, 2);
            CHECK(__mlang_std_audio_controller_post(d, 0, 6, 0, 36, 127, 0, at + 32, 1, 1) == 0);
            CHECK(__mlang_std_audio_controller_process(d, b, 256) == 0);
            return __mlang_std_audio_pcm_block_sample(b, 128, 0);
        };
        // The instrument's fader applies to a following bus...
        CHECK(__mlang_std_audio_controller_track_volume(d, 0, 1, 0) == 0);
        CHECK(std::abs(hit()) < 1.e-6f);
        // ...but not to one routed away: straight to master...
        CHECK(__mlang_std_audio_controller_instrument_output_route(d, 1, 1, 0) == 0);
        CHECK(std::abs(hit() - 0.125f) < 1.e-4f);
        // ...or into track 3's channel, whose own fader applies.
        CHECK(__mlang_std_audio_controller_instrument_output_route(d, 1, 1, 3) == 0);
        CHECK(__mlang_std_audio_controller_track_volume(d, 2, 0, 50) == 0);
        __mlang_std_audio_controller_track_peak(d, 2, 0);
        CHECK(std::abs(hit() - 0.0625f) < 1.e-4f);
        CHECK(__mlang_std_audio_controller_track_peak(d, 2, 0) >= 249);
        // A slot back on Main plays through the instrument fader again.
        CHECK(__mlang_std_audio_controller_set_parameter(d, 1, 11, 0) == 0);
        CHECK(__mlang_std_audio_controller_process(d, b, 256) == 0);
        CHECK(std::abs(hit()) < 1.e-6f);
        CHECK(__mlang_std_audio_controller_track_volume(d, 0, 1, 100) == 0);
        CHECK(std::abs(hit() - 0.125f) < 1.e-4f);
        // Reloading resets the routes to follow Main.
        CHECK(__mlang_std_audio_controller_set_parameter(d, 1, 11, 1) == 0);
        CHECK(__mlang_std_audio_controller_load_instrument(d, 1, sampler) == 0);
        CHECK(__mlang_std_audio_controller_instrument_pad(d, 1, 0, FloatList{4800, hit_pcm.data()}, 1, 48000, "hit") == 0);
        CHECK(__mlang_std_audio_controller_set_parameter(d, 1, 11, 1) == 0);
        CHECK(__mlang_std_audio_controller_set_parameter(d, 1, 7, 0) == 0);
        CHECK(__mlang_std_audio_controller_process(d, b, 256) == 0);
        CHECK(std::abs(hit() - 0.125f) < 1.e-4f);
        // Send A (bus 8) at full on slot 1: left following Main it doubles
        // the slot; routed nowhere it adds nothing...
        CHECK(__mlang_std_audio_controller_set_parameter(d, 1, 11, 0) == 0);
        int send = -1;
        for(int i = 0; i < __mlang_std_audio_controller_parameter_info(d, 1, 0, 0); ++i)
            if(__mlang_std_audio_controller_parameter_info(d, 1, i, 4) == 1700) send = i;
        CHECK(send >= 0);
        CHECK(__mlang_std_audio_controller_set_parameter(d, 1, send, 1.0) == 0);
        CHECK(__mlang_std_audio_controller_process(d, b, 256) == 0);
        CHECK(std::abs(hit() - 0.25f) < 1.e-4f);
        CHECK(__mlang_std_audio_controller_instrument_output_route(d, 1, 8, 73) == 0);
        CHECK(std::abs(hit() - 0.125f) < 1.e-4f);
        // ...and into effect channel 1 it feeds only the effect (x0.5 here):
        // silent while the channel is empty.
        CHECK(__mlang_std_audio_controller_instrument_output_route(d, 1, 8, 65) == 0);
        CHECK(std::abs(hit() - 0.125f) < 1.e-4f);
        CHECK(__mlang_std_audio_controller_load_effect(d, 0, argv[2]) == 0);
        CHECK(std::abs(hit() - 0.1875f) < 1.e-4f);
        // Slice markers: four hits a quarter second apart are four markers;
        // the host can replace them and have them detected again.
        std::vector<float> hits(48000, 0.f);
        for(int h = 0; h < 4; ++h) for(int f = 0; f < 200; ++f) hits[h * 12000 + f] = (f % 8 < 4) ? .8f : -.8f;
        CHECK(__mlang_std_audio_controller_instrument_pad(d, 1, 2, FloatList{48000, hits.data()}, 1, 48000, "hits") == 0);
        const auto markers = [&]() {
            DoubleList list = __mlang_std_audio_controller_instrument_markers(d, 1, 2);
            std::vector<double> out(list.data, list.data + list.size); std::free(list.data); return out;
        };
        const auto found = markers();
        CHECK(found.size() == 4 && found[0] == 0.0 && std::abs(found[3] - 36000.0) <= 64.0);
        std::vector<double> two{0.0, 24000.0};
        CHECK(__mlang_std_audio_controller_instrument_set_markers(d, 1, 2, DoubleList{2, two.data()}, 0) == 0);
        CHECK(markers() == two);
        CHECK(__mlang_std_audio_controller_instrument_set_markers(d, 1, 2, DoubleList{0, nullptr}, 1) == 0);
        CHECK(markers().size() == 4);
        CHECK(__mlang_std_audio_controller_instrument_markers(d, 1, 9).size == 0); // an empty pad
        CHECK(__mlang_std_audio_controller_close(d) == 0);
        std::puts("PASS: Mla Sampler aux outputs follow Main or route to master, tracks and effect sends; slice markers");
    }
    {
        // Lane 2 carries sequencer events stamped with the frame they sound
        // on: one stamped 100 frames into a block starts there, and one
        // stamped far ahead holds back only its own lane.
        int64_t d = __mlang_std_audio_controller_new(48000, 128);
        std::vector<float> tone(4800, 1.f);
        CHECK(__mlang_std_audio_controller_sample_data(d, FloatList{4800, tone.data()}, 1, 48000) == 0);
        CHECK(__mlang_std_audio_controller_post(d, 3, 4, 0, 0, 0, 0, -1, 0, 1) == -1); // three lanes only
        int64_t start = __mlang_std_audio_controller_info(d, 2);
        CHECK(__mlang_std_audio_controller_post(d, 2, 4, 0, 0, 0, 0, start + 100, 0, 1) == 0);
        CHECK(__mlang_std_audio_controller_process(d, b, 256) == 0);
        CHECK(__mlang_std_audio_pcm_block_sample(b, 99, 0) == 0.f && __mlang_std_audio_pcm_block_sample(b, 200, 0) > 0.f);
        CHECK(__mlang_std_audio_controller_close(d) == 0);
        d = __mlang_std_audio_controller_new(48000, 128);
        CHECK(__mlang_std_audio_controller_sample_data(d, FloatList{4800, tone.data()}, 1, 48000) == 0);
        start = __mlang_std_audio_controller_info(d, 2);
        CHECK(__mlang_std_audio_controller_post(d, 2, 4, 0, 0, 0, 0, start + 48000, 0, 1) == 0);
        CHECK(__mlang_std_audio_controller_post(d, 0, 4, 0, 0, 0, 0, -1, 0, 1) == 0);
        CHECK(__mlang_std_audio_controller_process(d, b, 256) == 0);
        CHECK(__mlang_std_audio_pcm_block_sample(b, 200, 0) > 0.f);
        CHECK(__mlang_std_audio_controller_close(d) == 0);
        std::puts("PASS: frame-stamped sequencer lane");
    }
    CHECK(__mlang_std_audio_pcm_block_close(b) == 0);
    std::puts("PASS: real VST3 bundle load, frame-timed MIDI, output, panic, failed replacement, reload");
}
