#include <libretro.h>
#include "test_core_api.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>

static retro_environment_t environment;
static retro_video_refresh_t video;
static retro_audio_sample_batch_t audio;
static retro_input_poll_t poll;
static std::mutex mutex;
static TestCoreSnapshot snapshot;
static uint64_t value = 0;
static bool contentless = false;
#ifdef CABINET_TEST_FULLPATH
static constexpr bool need_fullpath = true;
#else
static constexpr bool need_fullpath = false;
#endif
static void (*observer)() = nullptr;
static const char* option(const char* key) {
    retro_variable variable{key, nullptr};
    return environment(RETRO_ENVIRONMENT_GET_VARIABLE, &variable) && variable.value ? variable.value : "";
}
extern "C" {
TestCoreSnapshot cabinet_test_read() { std::lock_guard lock(mutex); return snapshot; }
void cabinet_test_observe(void (*callback)()) { observer = callback; }
void retro_set_environment(retro_environment_t callback) { environment = callback; }
void retro_set_video_refresh(retro_video_refresh_t callback) { video = callback; }
void retro_set_audio_sample(retro_audio_sample_t) {}
void retro_set_audio_sample_batch(retro_audio_sample_batch_t callback) { audio = callback; }
void retro_set_input_poll(retro_input_poll_t callback) { poll = callback; }
void retro_set_input_state(retro_input_state_t) {}
void retro_set_controller_port_device(unsigned, unsigned) {}
unsigned retro_api_version() { return RETRO_API_VERSION; }
void retro_init() {
    contentless = std::strcmp(option("cabinet_test_contentless"), "enabled") == 0;
    environment(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &contentless);
    retro_pixel_format format = RETRO_PIXEL_FORMAT_XRGB8888;
    environment(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &format);
    retro_variable dolphin[]{{"dolphin_efb_scale", "Resolution; 1|2|3"},
        {"dolphin_main_cpu_thread", "Dual core; enabled|disabled"}, {nullptr, nullptr}};
    retro_variable mupen[]{{"mupen64plus-rdp-plugin", "Renderer; gliden64|parallel"},
        {"mupen64plus-rsp-plugin", "RSP; hle|parallel"},
        {"mupen64plus-43screensize", "Resolution; 640x480|1280x960"},
        {"mupen64plus-parallel-rdp-upscaling", "Upscaling; 1x|2x"}, {nullptr, nullptr}};
    const bool n64 = std::strcmp(option("cabinet_test_mode"), "mupen") == 0;
    const std::string options_api = option("cabinet_test_options_api");
    if (!options_api.empty()) {
        unsigned version = 0; environment(RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION, &version);
        if (version != 2) return;
        retro_core_option_definition definitions[2]{};
        definitions[0].key = "fixture_enum"; definitions[0].desc = "Fixture option"; definitions[0].info = "Fixture description";
        definitions[0].values[0] = {"first", "First label"}; definitions[0].values[1] = {"second", "Second label"};
        definitions[0].default_value = "second";
        if (options_api == "v1") environment(RETRO_ENVIRONMENT_SET_CORE_OPTIONS, definitions);
        else if (options_api == "v1-intl") {
            retro_core_options_intl intl{definitions,nullptr}; environment(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL,&intl);
        } else {
            retro_core_option_v2_definition v2[2]{};
            v2[0].key = definitions[0].key; v2[0].desc = definitions[0].desc; v2[0].info = definitions[0].info;
            v2[0].values[0] = definitions[0].values[0]; v2[0].values[1] = definitions[0].values[1];
            v2[0].default_value = definitions[0].default_value;
            retro_core_options_v2 modern{nullptr,v2};
            if (options_api == "v2") environment(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2,&modern);
            else { retro_core_options_v2_intl intl{&modern,nullptr}; environment(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL,&intl); }
        }
    } else if (std::strcmp(option("cabinet_test_mode"), "ds") == 0) {
        retro_variable ds[]{{"melonds_screen_layout1", "Layout; top-bottom|left-right"}, {nullptr, nullptr}};
        environment(RETRO_ENVIRONMENT_SET_VARIABLES, ds);
    } else environment(RETRO_ENVIRONMENT_SET_VARIABLES, n64 ? mupen : dolphin);
    std::lock_guard lock(mutex);
    ++snapshot.boots; value = 0;
    std::snprintf(snapshot.resolution, sizeof(snapshot.resolution), "%s", option(!options_api.empty() ? "fixture_enum" : n64 ? "mupen64plus-43screensize" : "dolphin_efb_scale"));
    std::snprintf(snapshot.dual_core, sizeof(snapshot.dual_core), "%s", option("dolphin_main_cpu_thread"));
    std::snprintf(snapshot.renderer, sizeof(snapshot.renderer), "%s", option("mupen64plus-rdp-plugin"));
    std::snprintf(snapshot.rsp, sizeof(snapshot.rsp), "%s", option("mupen64plus-rsp-plugin"));
}
void retro_deinit() {}
void retro_get_system_info(retro_system_info* info)
{
    *info = {"Cabinet test core", "1", "bin", need_fullpath, false};
}
void retro_get_system_av_info(retro_system_av_info* info) { *info = {{8,8,8,8,1}, {60,contentless ? 0.0 : 48000.0}}; }
bool retro_load_game(const retro_game_info* game) {
    if (std::strcmp(option("cabinet_test_frame_time"), "enabled") == 0) {
        retro_frame_time_callback timing{[](retro_usec_t usec) {
            std::lock_guard lock(mutex); ++snapshot.frame_time_calls; snapshot.frame_time = usec;
        }, 16667};
        if (!environment(RETRO_ENVIRONMENT_SET_FRAME_TIME_CALLBACK, &timing)) return false;
    }
    if (contentless)
    {
        return game == nullptr;
    }
    if (!game)
    {
        return false;
    }
    if (need_fullpath)
    {
        if (!game->path || game->data || game->size)
        {
            return false;
        }
        std::ifstream content(game->path, std::ios::binary);
        return content.peek() != std::char_traits<char>::eof();
    }
    return game->data && game->size;
}
bool retro_load_game_special(unsigned, const retro_game_info*, size_t) { return false; }
void retro_unload_game() {}
void retro_reset() { std::lock_guard lock(mutex); ++snapshot.resets; value = 0; }
void retro_run() {
    bool fast_forward = false;
    retro_throttle_state throttle{};
    environment(RETRO_ENVIRONMENT_GET_FASTFORWARDING, &fast_forward);
    environment(RETRO_ENVIRONMENT_GET_THROTTLE_STATE, &throttle);
    if (observer) observer();
    poll();
    std::array<uint32_t, 64> pixels;
    { std::lock_guard lock(mutex); ++snapshot.runs; snapshot.fast_forward = fast_forward; snapshot.throttle = throttle.mode;
        pixels.fill(static_cast<uint32_t>(++value) & 255); }
    video(pixels.data(), 8, 8, 8 * sizeof(uint32_t));
    std::array<int16_t, 1600> samples{};
    audio(samples.data(), samples.size() / 2);
    if (observer) observer();
}
size_t retro_serialize_size() { return sizeof(value); }
bool retro_serialize(void* data, size_t size) {
    if (size != sizeof(value)) return false;
    std::lock_guard lock(mutex); std::memcpy(data, &value, size); ++snapshot.saves; return true;
}
bool retro_unserialize(const void* data, size_t size) {
    if (size != sizeof(value)) return false;
    std::lock_guard lock(mutex); std::memcpy(&value, data, size); snapshot.loaded_value = value; ++snapshot.loads; return true;
}
void* retro_get_memory_data(unsigned) { return nullptr; }
size_t retro_get_memory_size(unsigned) { return 0; }
unsigned retro_get_region() { return RETRO_REGION_NTSC; }
void retro_cheat_reset() {}
void retro_cheat_set(unsigned, bool, const char*) {}
}
