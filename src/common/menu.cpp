// the cheats panel, on the touch controls' gear button or F10
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <map>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <jni.h>
#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_internal.h>

#include "app/app.h"
#include "audio/output.h"
#include "common/keys.h"
#include "common/menu.h"
#include "common/mouse_keyboard.h"
#include "common/options.h"
#include "common/translations.h"
#include "game/weapons.h"

// native/gyro.cpp: aiming by turning the phone
namespace gyro {
uint32_t CurrentMode();
const char* ModeName();
void SetMode(uint32_t mode);
float Sensitivity();
void SetSensitivity(float s);
bool InvertX();
bool InvertY();
void SetInvert(bool x, bool y);
}  // namespace gyro

// native/touch.cpp: the touch controls' layout
namespace touch {
const char* LayoutName();
void StepLayout(int step);
uint32_t ControlsOpacity();
void StepOpacity(int step);
uint32_t ControlsSize();
void StepSize(int step);
void EditLayout();
const char* SkinName();
void StepSkin(int step);
const char* StickModeName();
void StepStickMode(int step);
}  // namespace touch

// native/dynamic_resolution.cpp: the render resolution in use, percent of the screen's
namespace dynres {
uint32_t Percent();
}  // namespace dynres

// native/update.cpp: the activity asks GitHub for a newer release and says what it found
namespace host {
void CheckForUpdates();
}  // namespace host

namespace app {

Settings DefaultSettings();  // native/main.cpp: a phone's first run, unlimited frame rate and vsync off

namespace {

std::atomic<bool> g_open{false};

// the cover art's brass on black
constexpr ImVec4 kPanelBg{0.035f, 0.03f, 0.025f, 0.94f};
constexpr ImVec4 kBorder{0.72f, 0.58f, 0.30f, 0.85f};
constexpr ImVec4 kText{0.90f, 0.88f, 0.84f, 1.0f};
constexpr ImVec4 kHighlight{0.30f, 0.23f, 0.10f, 1.0f};
constexpr ImVec4 kHeading{0.86f, 0.69f, 0.34f, 1.0f};
constexpr ImVec4 kDim{0.62f, 0.58f, 0.50f, 1.0f};
constexpr ImVec4 kTab{0.12f, 0.10f, 0.07f, 1.0f};
constexpr ImVec4 kTabHovered{0.20f, 0.16f, 0.09f, 1.0f};
constexpr ImVec4 kButton{0.17f, 0.14f, 0.08f, 1.0f};
// lighter than the highlight, so it shows on a selected row
constexpr ImVec4 kButtonHovered{0.40f, 0.31f, 0.14f, 1.0f};
constexpr ImVec4 kLine{0.86f, 0.69f, 0.34f, 0.35f};
constexpr std::pair<ImGuiCol, ImVec4> kPanelColors[] = {
    {ImGuiCol_WindowBg, kPanelBg}, {ImGuiCol_Border, kBorder}, {ImGuiCol_Text, kText},
    {ImGuiCol_Header, kHighlight}, {ImGuiCol_HeaderHovered, kHighlight}, {ImGuiCol_HeaderActive, kHighlight},
    {ImGuiCol_Button, kButton}, {ImGuiCol_ButtonHovered, kButtonHovered}, {ImGuiCol_ButtonActive, kButtonHovered},
    {ImGuiCol_FrameBg, kTab}, {ImGuiCol_FrameBgHovered, kTabHovered}, {ImGuiCol_FrameBgActive, kHighlight},
    {ImGuiCol_ScrollbarGrab, kHighlight}, {ImGuiCol_ScrollbarGrabHovered, kButtonHovered},
    {ImGuiCol_ScrollbarGrabActive, kHeading}, {ImGuiCol_Separator, kLine}, {ImGuiCol_NavHighlight, kHeading},
};

constexpr uint32_t kAnisoValues[] = {0, 2, 4, 8, 16};
const char* const kAnisoNames[] = {"GAME DEFAULT", "2x", "4x", "8x", "16x"};
constexpr uint32_t kDistances[] = {1, 2, 4, 8};
const char* const kDistanceNames[] = {"GAME DEFAULT", "2x", "4x", "8x"};
constexpr uint32_t kAntiAliasings[] = {0, 1, 2};
const char* const kAntiAliasingNames[] = {"OFF", "FXAA", "SSAA 2X"};
constexpr uint32_t kFmvScales[] = {0, 1};
const char* const kFmvScaleNames[] = {"FILL", "4:3"};
constexpr uint32_t kScreenFilters[] = {0, 1, 2, 3, 4, 5, 6, 7, 8};
const char* const kScreenFilterNames[] = {"OFF",  "CRT LIGHT", "CRT STRONG",   "SHARPEN", "VIBRANT",
                                          "NOIR", "SEPIA",     "NIGHT VISION", "VHS"};
constexpr uint32_t kAudioOutputs[] = {audio::kOutputAuto, audio::kOutputMono, audio::kOutputStereo,
                                      audio::kOutputSurround};
const char* const kAudioOutputNames[] = {"AUTOMATIC", "MONO", "2.0 STEREO", "5.1 SURROUND"};
const char* const kLanguageNames[] = {"ENGLISH", "FRENCH", "GERMAN", "ITALIAN", "SPANISH",
                                      "INDONESIAN"};  // as game::kLanguages
constexpr uint32_t kFpsCaps[] = {30, 60, 75, 90, 120, 144, 165, 240, 360, kFpsCapRefresh, 0};

int StepIndex(int index, int step, int count) {
    if (count <= 0) return 0;
    index = (index + step) % count;
    return index < 0 ? index + count : index;
}

int NearestIndex(const uint32_t* values, int count, uint32_t value) {
    int best = 0;
    uint32_t best_d = UINT32_MAX;
    for (int i = 0; i < count; i++) {
        uint32_t d = values[i] > value ? values[i] - value : value - values[i];
        if (d < best_d) {
            best_d = d;
            best = i;
        }
    }
    return best;
}

// index nearest value, stepped first
template <size_t N>
int StepTable(const uint32_t (&values)[N], uint32_t& value, int step) {
    int i = NearestIndex(values, int(N), value);
    if (step) {
        i = StepIndex(i, step, int(N));
        value = values[i];
    }
    return i;
}

// value clamped to lo to hi, stepped by inc through the values StepIndex wraps over
int StepRange(int value, int lo, int hi, int inc, int step) {
    int count = (hi - lo) / inc + 1;
    return lo + inc * StepIndex((std::clamp(value, lo, hi) - lo) / inc, step, count);
}

void Section(const char* title) {
    float em = ImGui::GetFontSize();
    ImGui::Dummy(ImVec2(0, em * 0.5f));
    ImGui::PushStyleColor(ImGuiCol_Text, kHeading);
    ImGui::TextUnformatted(Ui(title).c_str());
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, em * 0.2f));
}

constexpr float kValueEms = 12.0f;  // the value column's width

// a row's place and size: the label left, the value block (an arrow each side of the value) right
struct RowBox {
    float em;
    float row_w;
    float row_h;
    float value_w;
    float block_w;
    float row_x;
    float row_y;
};

RowBox MeasureRow() {
    const ImGuiStyle& style = ImGui::GetStyle();
    RowBox box;
    box.em = ImGui::GetFontSize();
    box.row_w = ImGui::GetContentRegionAvail().x;
    box.row_h = ImGui::GetFrameHeight();
    box.value_w = box.em * kValueEms;
    box.block_w = box.row_h * 2 + box.value_w + style.ItemSpacing.x * 2;
    box.row_x = ImGui::GetCursorPosX();
    box.row_y = ImGui::GetCursorPosY();
    return box;
}

void RowLabel(const RowBox& box, const char* label) {
    ImGui::SetCursorPos(ImVec2(box.row_x + ImGui::GetStyle().ItemInnerSpacing.x, box.row_y));
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(Ui(label).c_str());
}

// the cursor back to the row's start, then past the whole row
void EndRow(const RowBox& box) {
    ImGui::SetCursorPos(ImVec2(box.row_x, box.row_y));
    ImGui::Dummy(ImVec2(box.row_w, box.row_h));
}

void FillItem() {
    ImGui::GetWindowDrawList()->AddRectFilled(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                                              ImGui::GetColorU32(ImGuiCol_Header));
}

// label left, arrows and value right, the row is the only nav stop
int OptionRow(const char* label, const char* value) {
    const ImGuiStyle& style = ImGui::GetStyle();
    RowBox box = MeasureRow();
    ImGui::PushID(label);
    int step = 0;
    ImGui::Selectable("##row", false, ImGuiSelectableFlags_AllowOverlap, ImVec2(box.row_w, box.row_h));
    if (ImGui::IsItemFocused()) {
        FillItem();
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadLeft)) step = -1;
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow) || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadRight)) step = 1;
    }
    RowLabel(box, label);
    ImGui::PushItemFlag(ImGuiItemFlags_NoNav | ImGuiItemFlags_ButtonRepeat, true);  // held arrows keep stepping
    float block_x = box.row_x + box.row_w - box.block_w;
    ImGui::SetCursorPos(ImVec2(block_x, box.row_y));
    if (ImGui::ArrowButton("##dec", ImGuiDir_Left)) step = -1;
    float value_x = block_x + box.row_h + style.ItemSpacing.x;
    float pad = (box.value_w - ImGui::CalcTextSize(value).x) * 0.5f;
    ImGui::SetCursorPos(ImVec2(value_x + std::max(pad, 0.0f), box.row_y));
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(value);
    ImGui::SetCursorPos(ImVec2(value_x + box.value_w + style.ItemSpacing.x, box.row_y));
    if (ImGui::ArrowButton("##inc", ImGuiDir_Right)) step = 1;
    ImGui::PopItemFlag();
    ImGui::PopID();
    EndRow(box);
    return step;
}

const char* OnOff(bool on) { return on ? "ON" : "OFF"; }

void Flip(bool& on, int step) {
    if (step) on = !on;
}

// applies and saves
void Changed() {
    audio::SetOutputMode(game::Opt().audio_output);
    Save();
}

enum RowId { kRenderResolution, kMaxFps, kVsync, kFpsCounter, kAniso, kReloadBlur, kSkipIntros, kSensitivity,
             kInvertMouse, kWalkSpeed, kFov, kViewmodelFov, kHoldAim, kDrawDistance, kAudioOutput, kLanguage,
             kFmvScale, kGyro, kGyroSensitivity, kTouchLayout, kTouchOpacity, kTouchSize, kTouchEdit,
             kInfiniteAmmo, kRapidFire, kNoRecoil, kGameSpeed, kGodMode, kOneHitKill, kInfiniteGrenades,
             kCheckUpdates, kTouchSensitivity, kLeftDeadzone, kRightDeadzone, kLeftSensitivity, kRightSensitivity,
             kScreenFilter, kGyroInvertX, kGyroInvertY, kUnlockLevels, kUnlockBlackOps, kTouchSkin, kCredit, kBloom,
             kFog, kAntiAliasing, kHdr, kEsp, kAimbot, kTouchStick, kDynamicRes, kDynamicTarget, kDynamicMin,
             kDynamicMax, kDynamicHold, kAutoUpdate };

using rows::Kind;
using rows::Row;

// the recomp's settings in pages of their own, grouped within; the game's options menu shows the headings too
constexpr Row kGraphicsRows[] = {
    {Kind::Heading, -1, "DISPLAY"},
    {Kind::Value, kRenderResolution, "RENDER RESOLUTION"},
    {Kind::Value, kFov, "FIELD OF VIEW"},
    {Kind::Value, kViewmodelFov, "VIEWMODEL FIELD OF VIEW"},
    {Kind::Value, kFmvScale, "FMV SCALE"},
    {Kind::Value, kScreenFilter, "SCREEN FILTER"},
    {Kind::Heading, -1, "PERFORMANCE"},
    {Kind::Value, kVsync, "VERTICAL SYNC"},
    {Kind::Value, kMaxFps, "MAX FRAME RATE"},
    {Kind::Value, kFpsCounter, "FPS COUNTER"},
    {Kind::Value, kDynamicRes, "DYNAMIC RESOLUTION"},
    {Kind::Value, kDynamicTarget, "TARGET FRAME RATE"},
    {Kind::Value, kDynamicMin, "MIN RESOLUTION"},
    {Kind::Value, kDynamicMax, "MAX RESOLUTION"},
    {Kind::Value, kDynamicHold, "HOLD BEFORE RAISING"},
    {Kind::Heading, -1, "QUALITY"},
    {Kind::Value, kAniso, "ANISOTROPIC FILTERING"},
    {Kind::Value, kDrawDistance, "DRAW DISTANCE"},
    {Kind::Value, kReloadBlur, "RELOAD BLUR"},
    {Kind::Value, kBloom, "BLOOM"},
    {Kind::Value, kFog, "FOG"},
    {Kind::Value, kAntiAliasing, "ANTI-ALIASING"},
    {Kind::Value, kHdr, "HDR"},
};

constexpr Row kTouchRows[] = {
    {Kind::Heading, -1, "TOUCH"},
    {Kind::Value, kTouchSensitivity, "TOUCH SENSITIVITY"},
    {Kind::Value, kTouchLayout, "CONTROLS LAYOUT"},
    {Kind::Value, kTouchOpacity, "CONTROLS OPACITY"},
    {Kind::Value, kTouchSize, "CONTROLS SIZE"},
    {Kind::Value, kTouchSkin, "BUTTON STYLE"},
    {Kind::Value, kTouchStick, "ANALOG STICK"},
    {Kind::Value, kTouchEdit, "EDIT CONTROLS POSITION"},
    {Kind::Heading, -1, "MOTION CONTROLS"},
    {Kind::Value, kGyro, "GYROSCOPE"},
    {Kind::Value, kGyroSensitivity, "GYRO SENSITIVITY"},
    {Kind::Value, kGyroInvertX, "GYRO INVERT X"},
    {Kind::Value, kGyroInvertY, "GYRO INVERT Y"},
};

// on the game's control setup page, under its own rows
constexpr Row kControllerRows[] = {
    {Kind::Value, kLeftDeadzone, "LEFT STICK DEADZONE"},
    {Kind::Value, kRightDeadzone, "RIGHT STICK DEADZONE"},
    {Kind::Value, kLeftSensitivity, "LEFT STICK SENSITIVITY"},
    {Kind::Value, kRightSensitivity, "RIGHT STICK SENSITIVITY"},
};

// LANGUAGE always shows: Indonesian is there whatever the disc
constexpr Row kGeneralRows[] = {
    {Kind::Value, kLanguage, "LANGUAGE"},
    {Kind::Value, kSkipIntros, "SKIP INTROS"},
    {Kind::Value, kAudioOutput, "AUDIO OUTPUT"},
    {Kind::Heading, -1, "UPDATES"},
    {Kind::Value, kCheckUpdates, "CHECK FOR UPDATES"},
    {Kind::Value, kAutoUpdate, "STARTUP UPDATE CHECK"},
};


// a page of its own, apart from the recomp's settings
constexpr Row kCheatRows[] = {
    {Kind::Heading, -1, "WEAPONS"},
    {Kind::Value, kInfiniteAmmo, "INFINITE AMMO"},
    {Kind::Value, kInfiniteGrenades, "INFINITE GRENADES"},
    {Kind::Value, kRapidFire, "RAPID FIRE"},
    {Kind::Value, kNoRecoil, "NO RECOIL"},
    {Kind::Heading, -1, "PLAYER"},
    {Kind::Value, kGodMode, "GOD MODE"},
    {Kind::Value, kOneHitKill, "ONE-HIT KILL"},
    {Kind::Value, kEsp, "ESP LINES"},
    {Kind::Value, kAimbot, "AIMBOT (WHILE AIMING IN)"},  // esp.cpp turns the view only at a zoom above 1
    {Kind::Heading, -1, "GAME"},
    {Kind::Value, kGameSpeed, "GAME SPEED"},
    {Kind::Value, kUnlockLevels, "UNLOCK ALL MISSIONS"},
    {Kind::Value, kUnlockBlackOps, "UNLOCK BLACK OPS"},
};

// the title menu's CREDITS page: who and what, and the links, which open in the browser. Its rows take no step; a
// row's id is kCredit and its place in kCredits
struct Credit {
    const char* label;  // a heading when there is no value
    const char* value;
    const char* url;
};
constexpr Credit kCredits[] = {
    {"CONTRIBUTORS", nullptr, nullptr},
    {"Rangga Casper", "ANDROID PORT", "https://github.com/RanggaCasper"},
    {"DATFOOLDIVE", "ANY WEAPON IN ANY LEVEL", "https://github.com/datfooldive"},
    {"BAGASROMADON", "BUTTON SKIN", "https://github.com/bagasromadon"},
    {"Zukiru", "HDR SHADER", nullptr},
    {"BUILT WITH", nullptr, nullptr},
    {"XBOXXBE-RECOMP", "TGP482", nullptr},
    {"SDL3, GLSLANG, TOML++", "LIBRARIES", nullptr},
    {"DEAR IMGUI", "LIBRARY", nullptr},
    {"XEMU, CXBX-RELOADED", "REFERENCES", nullptr},
    {"LINKS", nullptr, nullptr},
    {"YOUTUBE", "OPEN", "https://youtube.com/c/@RanggaCasper"},
    {"GITHUB", "OPEN", "https://github.com/RanggaCasper"},
    {"SUPPORT", "OPEN", "https://sociabuzz.com/racas/tribe"},
};

const std::vector<Row>& CreditRows() {
    static const std::vector<Row> list = [] {
        std::vector<Row> out;
        for (const Credit& c : kCredits) out.push_back({c.value ? Kind::Value : Kind::Heading, kCredit, c.label});
        return out;
    }();
    return list;
}

const Credit* CreditOf(const Row& row) {
    for (const Credit& c : kCredits)
        if (row.label == c.label) return &c;
    return nullptr;
}

const std::vector<Row>& InputRows() {
    static const std::vector<Row> list = [] {
        std::vector<Row> out = {{Kind::Heading, -1, "MOUSE"},
                                {Kind::Value, kSensitivity, "MOUSE SENSITIVITY"},
                                {Kind::Value, kInvertMouse, "INVERT LOOK"},
                                {Kind::Value, kHoldAim, "HOLD TO AIM"},
                                {Kind::Heading, -1, "KEYBOARD"}};
        size_t count = 0;
        const input::Action* actions = input::Actions(&count);
        for (size_t i = 0; i < count; i++) out.push_back({Kind::Bind, int(i), actions[i].label});
        out.push_back({Kind::Value, kWalkSpeed, "WALK SPEED"});
        return out;
    }();
    return list;
}

// the setting an ON / OFF row flips, nullptr for any other row
bool* ToggleFlag(Settings& s, int id) {
    game::Options& o = game::Opt();
    switch (id) {
        case kVsync: return &s.vsync;
        case kFpsCounter: return &o.fps_counter;
        case kReloadBlur: return &o.reload_blur;
        case kBloom: return &o.bloom;
        case kFog: return &o.fog;
        case kHdr: return &o.hdr;
        case kSkipIntros: return &o.skip_intros;
        case kInvertMouse: return &o.invert_mouse;
        case kHoldAim: return &o.hold_aim;
        case kInfiniteAmmo: return &o.infinite_ammo;
        case kNoRecoil: return &o.no_recoil;
        case kGodMode: return &o.god_mode;
        case kOneHitKill: return &o.one_hit_kill;
        case kInfiniteGrenades: return &o.infinite_grenades;
        case kUnlockLevels: return &o.unlock_levels;
        case kUnlockBlackOps: return &o.unlock_black_ops;
        case kEsp: return &o.esp;
        case kAimbot: return &o.aimbot;
        case kDynamicRes: return &o.dynamic_resolution;
        default: return nullptr;
    }
}

// both field of view rows: 45 to 140 in steps of 1, 70 the game's own
constexpr int kFovMin = 45;
constexpr int kFovMax = 140;
constexpr uint32_t kGameFov = 70;

void FovText(uint32_t fov, char (&text)[64]) {
    std::snprintf(text, sizeof(text), fov == kGameFov ? "%u (DEFAULT)" : "%u", fov);
}

// the dynamic resolution's frame rates to hold
constexpr uint32_t kDynamicTargets[] = {30, 45, 60, 90, 120};

// a resolution as percent of the screen's and its pixels
void PercentText(uint32_t percent, char (&text)[64]) {
    Mode out = Output();
    std::snprintf(text, sizeof(text), "%u%% (%ux%u)", percent, out.width * percent / 100, out.height * percent / 100);
}

// the display rows that are no ON / OFF, stepped first; false for another row
bool StepDisplayRow(int id, int step, Settings& s, char (&text)[64]) {
    game::Options& o = game::Opt();
    switch (id) {
        case kRenderResolution: {
            // 20% to 200% in steps of 5; greyed while the dynamic resolution has it, which it shows as it is now
            if (step) s.render_percent = uint32_t(StepRange(int(s.render_percent), 20, 200, 5, step));
            PercentText(o.dynamic_resolution ? dynres::Percent() : s.render_percent, text);
            return true;
        }
        case kDynamicTarget:
            std::snprintf(text, sizeof(text), "%u FPS", kDynamicTargets[StepTable(kDynamicTargets, o.dynamic_target_fps, step)]);
            return true;
        case kDynamicMin:
            // 20% up to the max, the max down to the min
            if (step) o.dynamic_min_percent = uint32_t(StepRange(int(o.dynamic_min_percent), 20, int(o.dynamic_max_percent), 5, step));
            PercentText(o.dynamic_min_percent, text);
            return true;
        case kDynamicMax:
            if (step) o.dynamic_max_percent = uint32_t(StepRange(int(o.dynamic_max_percent), int(o.dynamic_min_percent), 200, 5, step));
            PercentText(o.dynamic_max_percent, text);
            return true;
        case kDynamicHold:
            // 1 to 30 seconds
            if (step) o.dynamic_hold = uint32_t(StepRange(int(o.dynamic_hold), 1, 30, 1, step));
            std::snprintf(text, sizeof(text), "%u S", o.dynamic_hold);
            return true;
        case kFmvScale:
            std::snprintf(text, sizeof(text), "%s", kFmvScaleNames[StepTable(kFmvScales, o.fmv_scale, step)]);
            return true;
        case kAntiAliasing:
            std::snprintf(text, sizeof(text), "%s",
                          kAntiAliasingNames[StepTable(kAntiAliasings, o.anti_aliasing, step)]);
            return true;
        case kScreenFilter:
            std::snprintf(text, sizeof(text), "%s",
                          kScreenFilterNames[StepTable(kScreenFilters, o.screen_filter, step)]);
            return true;
        case kFov:
            if (step) o.fov = uint32_t(StepRange(int(o.fov), kFovMin, kFovMax, 1, step));
            FovText(o.fov, text);
            return true;
        case kViewmodelFov:
            if (step) o.viewmodel_fov = uint32_t(StepRange(int(o.viewmodel_fov), kFovMin, kFovMax, 1, step));
            FovText(o.viewmodel_fov, text);
            return true;
        case kMaxFps: {
            uint32_t cap = kFpsCaps[StepTable(kFpsCaps, s.fps_cap, step)];
            if (cap == kFpsCapRefresh) std::snprintf(text, sizeof(text), "REFRESH RATE");
            else if (cap) std::snprintf(text, sizeof(text), "%u", cap);
            else std::snprintf(text, sizeof(text), "UNLIMITED (DEFAULT)");
            return true;
        }
        default:
            return false;
    }
}

// the graphics rows that are no ON / OFF, stepped first; false for another row
bool StepGraphicsRow(int id, int step, Settings& s, char (&text)[64]) {
    game::Options& o = game::Opt();
    switch (id) {
        case kAniso:
            std::snprintf(text, sizeof(text), "%s", kAnisoNames[StepTable(kAnisoValues, s.aniso, step)]);
            return true;
        case kDrawDistance:
            std::snprintf(text, sizeof(text), "%s", kDistanceNames[StepTable(kDistances, o.draw_distance, step)]);
            return true;
        default:
            return false;
    }
}

// the cheat rows that are no ON / OFF, stepped first; false for another row
bool StepCheatRow(int id, int step, char (&text)[64]) {
    game::Options& o = game::Opt();
    switch (id) {
        case kRapidFire:
            // off, then 2 to 4 times the rate
            if (step) o.rapid_fire = uint32_t(StepRange(int(o.rapid_fire), 1, 4, 1, step));
            if (o.rapid_fire <= 1) std::snprintf(text, sizeof(text), "OFF");
            else std::snprintf(text, sizeof(text), "%uX", o.rapid_fire);
            return true;
        case kGameSpeed:
            // 25% to 200% in steps of 25
            if (step) o.game_speed = uint32_t(StepRange(int(o.game_speed), 25, 200, 25, step));
            std::snprintf(text, sizeof(text), o.game_speed == 100 ? "%u%% (DEFAULT)" : "%u%%", o.game_speed);
            return true;
        default:
            return false;
    }
}

// the general, mouse and keyboard rows that are no ON / OFF, stepped first
void StepInputRow(int id, int step, char (&text)[64]) {
    game::Options& o = game::Opt();
    switch (id) {
        case kAudioOutput:
            std::snprintf(text, sizeof(text), "%s", kAudioOutputNames[StepTable(kAudioOutputs, o.audio_output, step)]);
            break;
        case kSensitivity:
            // 0.1 to 5.0 in steps of 0.1
            if (step) {
                int tenths = StepRange(int(std::lround(o.mouse_sensitivity * 10.0f)), 1, 50, 1, step);
                o.mouse_sensitivity = float(tenths) / 10.0f;
            }
            std::snprintf(text, sizeof(text), "%.1f", o.mouse_sensitivity);
            break;
        case kTouchSensitivity:
            // as the mouse's: 0.1 to 5.0 in steps of 0.1
            if (step) {
                int tenths = StepRange(int(std::lround(o.touch_sensitivity * 10.0f)), 1, 50, 1, step);
                o.touch_sensitivity = float(tenths) / 10.0f;
            }
            std::snprintf(text, sizeof(text), "%.1f", o.touch_sensitivity);
            break;
        case kWalkSpeed:
            // 10% to 100% in steps of 5
            if (step) o.walk_speed = uint32_t(StepRange(int(o.walk_speed), 10, 100, 5, step));
            std::snprintf(text, sizeof(text), "%u%%", o.walk_speed);
            break;
        case kGyro:
            // off, always on, while firing
            if (step) gyro::SetMode(uint32_t(StepIndex(int(gyro::CurrentMode()), step, 3)));
            std::snprintf(text, sizeof(text), "%s", gyro::CurrentMode() == 0 ? "OFF"
                                                    : gyro::CurrentMode() == 1 ? "ALWAYS"
                                                                               : "WHILE FIRING");
            break;
        case kTouchLayout:
            // the presets, CUSTOM once a control was moved
            if (step) touch::StepLayout(step);
            std::snprintf(text, sizeof(text), "%s", touch::LayoutName());
            break;
        case kTouchOpacity:
            if (step) touch::StepOpacity(step);
            std::snprintf(text, sizeof(text), "%u%%", touch::ControlsOpacity());
            break;
        case kTouchSize:
            if (step) touch::StepSize(step);
            std::snprintf(text, sizeof(text), "%u%%", touch::ControlsSize());
            break;
        case kTouchSkin:
            if (step) touch::StepSkin(step);
            std::snprintf(text, sizeof(text), "%s", touch::SkinName());
            break;
        case kTouchStick:
            // under the thumb where it comes down, or fixed in its place
            if (step) touch::StepStickMode(step);
            std::snprintf(text, sizeof(text), "%s", touch::StickModeName());
            break;
        case kTouchEdit:
            // any step opens the editor over the screen
            if (step) touch::EditLayout();
            std::snprintf(text, sizeof(text), "OPEN");
            break;
        case kCheckUpdates:
            // any step asks; the answer comes in a dialog over the game
            if (step) host::CheckForUpdates();
            std::snprintf(text, sizeof(text), "CHECK");
            break;
        case kGyroSensitivity:
            // 0.1 to 5.0 in steps of 0.1
            if (step) gyro::SetSensitivity(float(StepRange(int(std::lround(gyro::Sensitivity() * 10.0f)), 1, 50, 1, step)) / 10.0f);
            std::snprintf(text, sizeof(text), "%.1f", gyro::Sensitivity());
            break;
        case kGyroInvertX:
        case kGyroInvertY: {
            // kept with the gyro's other settings in android.toml, so not one of ToggleFlag's
            bool x = gyro::InvertX(), y = gyro::InvertY();
            if (step) (id == kGyroInvertX ? x : y) ^= true;
            if (step) gyro::SetInvert(x, y);
            std::snprintf(text, sizeof(text), "%s", OnOff(id == kGyroInvertX ? x : y));
            break;
        }
        case kLeftDeadzone:
        case kRightDeadzone: {
            // 0% to 50% of the stick's travel in steps of 1, 0 leaves the game's own
            uint32_t& dz = id == kLeftDeadzone ? o.left_deadzone : o.right_deadzone;
            if (step) dz = uint32_t(StepRange(int(dz), 0, 50, 1, step));
            std::snprintf(text, sizeof(text), dz ? "%u%%" : "%u%% (DEFAULT)", dz);
            break;
        }
        case kLeftSensitivity:
        case kRightSensitivity: {
            // 0.1 to 3.0 in steps of 0.1
            float& s = id == kLeftSensitivity ? o.left_sensitivity : o.right_sensitivity;
            if (step) s = float(StepRange(int(std::lround(s * 10.0f)), 1, 30, 1, step)) / 10.0f;
            std::snprintf(text, sizeof(text), "%.1f", s);
            break;
        }
        case kLanguage: {
            int count = int(std::size(game::kLanguages));
            int i = 0;
            while (i < count - 1 && o.language != game::kLanguages[i]) i++;
            // past the languages this disc has no strings for; English and Indonesian are always there
            if (step) {
                do i = StepIndex(i, step, count);
                while (!game::LanguageAvailable(game::kLanguages[i]));
                o.language = game::kLanguages[i];
            }
            const char* format = o.language == game::BootLanguage() ? "%s" : "%s (RESTART)";
            std::snprintf(text, sizeof(text), format, kLanguageNames[i]);
            break;
        }
           case kAutoUpdate: {
    // Mendapatkan lingkungan JNI dari SDL / Native Activity
    JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
    jobject activity = (jobject)SDL_AndroidGetActivity();
    if (env && activity) {
        jclass clazz = env->GetObjectClass(activity);
        
        // Membaca status saat ini dari SharedPreferences via Java method
        jmethodID getMethod = env->GetStaticMethodID(clazz, "isStartupCheckEnabled", "(Landroid/content/Context;)Z");
        bool auto_check = true;
        if (getMethod) {
            auto_check = env->CallStaticBooleanMethod(clazz, getMethod, activity);
        }

        // Jika tombol ditekan (step != 0), ubah statusnya (toggle)
        if (step) {
            auto_check = !auto_check;
            jmethodID setMethod = env->GetStaticMethodID(clazz, "setStartupCheckEnabled", "(Landroid/content/Context;Z)V");
            if (setMethod) {
                env->CallStaticVoidMethod(clazz, setMethod, activity, (jboolean)auto_check);
            }
        }
        
        env->DeleteLocalRef(clazz);
        env->DeleteLocalRef(activity);
        
        std::snprintf(text, sizeof(text), "%s", OnOff(auto_check));
    } else {
        std::snprintf(text, sizeof(text), "OFF");
    }
    break;
      }
    }
}

// steps the value first when step is not zero
std::string RowText(int id, int step, Settings& s = Get()) {
    char text[64] = "";
    if (bool* on = ToggleFlag(s, id)) {
        Flip(*on, step);
        std::snprintf(text, sizeof(text), "%s", OnOff(*on));
    } else if (!StepDisplayRow(id, step, s, text) && !StepGraphicsRow(id, step, s, text) &&
               !StepCheatRow(id, step, text)) {
        StepInputRow(id, step, text);
    }
    if (step && &s == &Get()) Changed();
    return text;
}

constexpr const char* kUnbound = "UNBOUND";

std::string KeyText(std::string keys) {
    if (keys.empty()) return kUnbound;
    for (char& c : keys) c = c == ',' ? '/' : char(std::toupper(uint8_t(c)));
    return keys;
}

// as options_menu.cpp's pages: 0 graphics
constexpr int kTouchPage = 1;
constexpr int kGeneralPage = 2;
constexpr int kKeyboardPage = 3;
constexpr int kCheatsPage = 4;
constexpr int kCreditsPage = 5;

void RestoreDefaults(int page) {
    game::Options& o = game::Opt();
    if (page == kCheatsPage) {
        game::Options none;
        o.infinite_ammo = none.infinite_ammo;
        o.rapid_fire = none.rapid_fire;
        o.no_recoil = none.no_recoil;
        o.game_speed = none.game_speed;
        o.god_mode = none.god_mode;
        o.one_hit_kill = none.one_hit_kill;
        o.infinite_grenades = none.infinite_grenades;
        o.unlock_levels = none.unlock_levels;
        o.unlock_black_ops = none.unlock_black_ops;
        o.esp = none.esp;
        o.aimbot = none.aimbot;
        Changed();
        return;
    }
    if (page == kKeyboardPage) {
        o.mouse_sensitivity = game::Options{}.mouse_sensitivity;
        o.invert_mouse = false;
        o.hold_aim = true;
        o.walk_speed = game::Options{}.walk_speed;
        input::ResetBindings();
        return;
    }
    game::Options keep = o;
    Get() = DefaultSettings();
    o = game::Options{};
    o.mouse_sensitivity = keep.mouse_sensitivity;
    o.invert_mouse = keep.invert_mouse;
    o.hold_aim = keep.hold_aim;
    o.walk_speed = keep.walk_speed;
    o.keys = keep.keys;
    // the cheats page has its own defaults
    o.infinite_ammo = keep.infinite_ammo;
    o.rapid_fire = keep.rapid_fire;
    o.no_recoil = keep.no_recoil;
    o.game_speed = keep.game_speed;
    o.god_mode = keep.god_mode;
    o.one_hit_kill = keep.one_hit_kill;
    o.infinite_grenades = keep.infinite_grenades;
    o.unlock_levels = keep.unlock_levels;
    o.unlock_black_ops = keep.unlock_black_ops;
    o.esp = keep.esp;
    o.aimbot = keep.aimbot;
    Changed();
}

// the row being rebound, -1 when idle; its keys must be released before the next press binds
std::atomic<int> g_capture{-1};
int g_capture_slot = 0;  // its box: 0 the key, 1 the alternate
int g_hovered_box = -1;  // the box under the mouse or focused with a key in it, this frame
bool g_capture_armed = false;
int g_capture_done = 0;  // frame of the last bind, the key that bound must not start another
int g_page = kCheatsPage;
// the action's key and its alternate as their boxes show them, the alternate blank when there is none
std::vector<std::string> KeyBoxes(size_t action) {
    std::string keys = input::Binding(action);
    size_t comma = keys.find(',');
    std::string second;
    if (comma != std::string::npos) second = keys.substr(comma + 1);
    second = second.substr(0, second.find(','));
    if (!second.empty()) second = KeyText(second);
    return {KeyText(keys.substr(0, comma)), second};
}

constexpr float kKeyBoxEms = 10.0f;  // a key box's width when the row has two

// label left, a box per key right (the key and its alternate side by side), ending where the other rows' values do;
// Enter or click starts a box's capture, right click or Delete sets `cleared`; returns the box, -1 for none
int BindRow(const char* label, const std::vector<std::string>& keys, int capturing, int& cleared) {
    const ImGuiStyle& style = ImGui::GetStyle();
    RowBox row = MeasureRow();
    float value_end = row.row_x + row.row_w - row.block_w + row.row_h + style.ItemSpacing.x + row.value_w;
    int n = int(keys.size());
    int pressed = -1;
    float box_w = n > 1 ? row.em * kKeyBoxEms : row.value_w;
    ImGui::PushID(label);
    RowLabel(row, label);
    for (int i = 0; i < n; i++) {
        float x = value_end - float(n - i) * box_w - float(n - 1 - i) * style.ItemSpacing.x;
        ImGui::SetCursorPos(ImVec2(x, row.row_y));
        ImGui::PushID(i);
        if (ImGui::InvisibleButton("##key", ImVec2(box_w, row.row_h))) pressed = i;
        bool deleted = ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Delete, false);
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right) || deleted) cleared = i;
        bool pointed = ImGui::IsItemHovered() || ImGui::IsItemFocused();
        if (pointed && !keys[i].empty() && keys[i] != kUnbound) g_hovered_box = i;
        bool lit = capturing == i || ImGui::IsItemFocused() || ImGui::IsItemHovered();
        ImGui::GetWindowDrawList()->AddRectFilled(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                                                  ImGui::GetColorU32(lit ? ImGuiCol_Header : ImGuiCol_FrameBg));
        // key names as they are
        std::string value = keys[i];
        if (capturing == i) value = Ui("PRESS A KEY");
        else if (value == kUnbound) value = Ui(kUnbound);
        float pad = (box_w - ImGui::CalcTextSize(value.c_str()).x) * 0.5f;
        ImGui::SetCursorPos(ImVec2(x + std::max(pad, 0.0f), row.row_y));
        ImGui::AlignTextToFramePadding();
        if (capturing == i) ImGui::PushStyleColor(ImGuiCol_Text, kHeading);
        ImGui::TextUnformatted(value.c_str());
        if (capturing == i) ImGui::PopStyleColor();
        ImGui::PopID();
    }
    ImGui::PopID();
    EndRow(row);
    return pressed;
}

// Escape cancels, any other bindable key or mouse button binds
void PollCapture() {
    if (g_capture < 0) return;
    int vk = input::PressedKey();
    if (!g_capture_armed) {
        g_capture_armed = !vk;
        return;
    }
    if (!vk) return;
    if (vk != VK_ESCAPE) input::Bind(size_t(g_capture.load()), vk, g_capture_slot);
    g_capture = -1;
    g_capture_done = ImGui::GetFrameCount();
}

// not under a bind or on the button that just bound
bool CaptureFree() { return g_capture < 0 && ImGui::GetFrameCount() > g_capture_done + 1; }

void ValueRow(const Row& r) {
    int step = OptionRow(r.label, Ui(RowText(r.id, 0)).c_str());
    if (step) RowText(r.id, step);
}

void KeyRow(const Row& r) {
    int cleared = -1;
    int box = BindRow(r.label, KeyBoxes(size_t(r.id)), g_capture == r.id ? g_capture_slot : -1, cleared);
    // a Delete that answered a capture is no clear
    if (cleared >= 0 && CaptureFree()) input::Bind(size_t(r.id), 0, cleared);
    if (box < 0 || !CaptureFree()) return;
    g_capture_slot = box;
    g_capture = r.id;
    g_capture_armed = false;
}

void DrawList(const Row* list, size_t count) {
    for (size_t i = 0; i < count; i++) {
        const Row& r = list[i];
        if (r.kind == Kind::Heading) Section(r.label);
        else if (r.kind == Kind::Value) ValueRow(r);
        else KeyRow(r);
    }
}

// the touch controls page ends with the sticks', which the game's options show on its control setup page
void DrawRows(int page) {
    size_t count = 0;
    const Row* list = rows::Page(page, &count);
    DrawList(list, count);
    if (page != kTouchPage) return;
    Section("CONTROLLER");
    DrawList(kControllerRows, std::size(kControllerRows));
}

// under the cheats, the level's weapons as buttons (weapons.cpp: any weapon in any level); the one given goes in the
// slot not in hand and the game switches to it
void DrawWeapons() {
    weapons::View v = weapons::Current();
    Section("GIVE A WEAPON");
    if (!v.in_level) {
        ImGui::PushStyleColor(ImGuiCol_Text, kDim);
        ImGui::TextWrapped("%s", Ui("Start or continue a level to choose its weapons.", false).c_str());
        ImGui::PopStyleColor();
        return;
    }
    constexpr int kColumns = 3;
    float gap = ImGui::GetStyle().ItemSpacing.x;
    float w = (ImGui::GetContentRegionAvail().x - gap * (kColumns - 1)) / kColumns;
    for (size_t i = 0; i < v.choices.size(); i++) {
        const weapons::Choice& c = v.choices[i];
        if (i % kColumns) ImGui::SameLine();
        ImGui::PushID(c.type);
        if (c.held) ImGui::PushStyleColor(ImGuiCol_Text, kHeading);  // in one of the two slots now
        if (ImGui::Button(c.label.c_str(), ImVec2(w, 0))) weapons::Give(c.type);
        if (c.held) ImGui::PopStyleColor();
        ImGui::PopID();
    }
    Section("AMMUNITION");
    if (ImGui::Button(Ui("FILL AMMUNITION").c_str(), ImVec2(w, 0))) weapons::FillAmmo();
    if (!v.status.empty()) {
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, kDim);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(v.status.c_str());
        ImGui::PopStyleColor();
    }
}

void Defaults() {
    g_capture = -1;
    RestoreDefaults(g_page);
}

// the panel is the cheats alone, the touch button's way to them in play; every other setting is in the game's own
// options menu
void DrawTitle(const char* caption) {
    std::string title = Ui(caption);
    float title_pad = (ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(title.c_str()).x) * 0.5f;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(title_pad, 0.0f));
    ImGui::TextUnformatted(title.c_str());
}

// the hint line, then Back and Defaults on the right
void DrawFooter(float em) {
    ImGui::Separator();
    ImGui::PushStyleColor(ImGuiCol_Text, kDim);
    const char* hint = "";
    if (g_capture >= 0) hint = "Press a key or mouse button to bind, Escape cancels";
    else if (g_page == kKeyboardPage && g_hovered_box >= 0) hint = "Right click or Delete clears the key";
    else if (g_page == kCheatsPage) hint = "A weapon replaces the one not in hand, gold names are held";
    ImGui::TextUnformatted(Ui(hint, false).c_str());
    ImGui::PopStyleColor();
    float bw = em * 8;
    float buttons_pad = ImGui::GetContentRegionAvail().x - bw * 2 - ImGui::GetStyle().ItemSpacing.x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(buttons_pad, 0.0f));
    if (ImGui::Button(Ui("Back", false).c_str(), ImVec2(bw, 0))) ToggleMenu();
    ImGui::SameLine();
    if (ImGui::Button(Ui("Defaults", false).c_str(), ImVec2(bw, 0))) Defaults();
}

// in the rows' child: a finger dragged up or down scrolls them as a phone's lists do, then glides a little when it
// lifts. Past half an em the press is the scroll's, so the row or weapon it came down on is not clicked
void DragScroll(float em) {
    constexpr float kFriction = 6.0f;  // of the glide's speed lost a second
    static bool dragging = false;
    static float glide = 0;  // pixels a second
    ImGuiIO& io = ImGui::GetIO();
    // the rows, not the scrollbar beside them: that drags as it always did
    bool inside = ImGui::GetCurrentWindow()->InnerRect.Contains(io.MouseClickedPos[0]);
    if (ImGui::IsMouseClicked(0)) {
        dragging = false;
        glide = 0;
    }
    if (ImGui::IsMouseDown(0) && inside) {
        if (!dragging && std::fabs(ImGui::GetMouseDragDelta(0).y) > em * 0.5f) dragging = true;
        if (!dragging) return;
        ImGui::SetScrollY(ImGui::GetScrollY() - io.MouseDelta.y);
        if (io.DeltaTime > 0) glide = (glide - io.MouseDelta.y / io.DeltaTime) * 0.5f;  // smoothed over frames
        ImGui::ClearActiveID();
        return;
    }
    dragging = false;
    if (std::fabs(glide) < em) {
        glide = 0;
        return;
    }
    ImGui::SetScrollY(ImGui::GetScrollY() + glide * io.DeltaTime);
    glide *= std::max(0.0f, 1.0f - kFriction * io.DeltaTime);
}

void DrawPanel() {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    float em = ImGui::GetFontSize();
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(em * 50, em * 32), ImGuiCond_Always);
    for (const auto& [col, color] : kPanelColors) ImGui::PushStyleColor(col, color);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(em * 1.6f, em * 1.2f));
    constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                                        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar;
    if (ImGui::Begin("##settings", nullptr, kFlags)) {
        DrawTitle("CHEATS");
        ImGui::Separator();
        float footer = ImGui::GetTextLineHeightWithSpacing() + ImGui::GetFrameHeightWithSpacing() + em;
        // a scrollbar a thumb can take, not the desktop's thin one
        ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, em * 1.4f);
        ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, em * 2.5f);
        ImGui::BeginChild("##rows", ImVec2(0, -footer), ImGuiChildFlags_NavFlattened);
        DragScroll(em);
        g_hovered_box = -1;
        DrawRows(g_page);
        if (g_page == kCheatsPage) DrawWeapons();
        ImGui::EndChild();
        ImGui::PopStyleVar(3);
        DrawFooter(em);
    }
    ImGui::End();
    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor(int(std::size(kPanelColors)));
}

// the window's display's refresh rate, 0 when unreadable
uint32_t RefreshRate() {
    SDL_Window* w = static_cast<SDL_Window*>(Window());
    const SDL_DisplayMode* m = SDL_GetCurrentDisplayMode(w ? SDL_GetDisplayForWindow(w) : SDL_GetPrimaryDisplay());
    return m && m->refresh_rate > 1 ? uint32_t(std::lround(m->refresh_rate)) : 0;
}

// as app::Pace, at this cap
void PaceAt(uint32_t cap) {
    using clock = std::chrono::steady_clock;
    static clock::time_point next = clock::now();
    auto now = clock::now();
    if (!cap) {
        next = now;
        return;
    }
    auto period = std::chrono::nanoseconds(1000000000ull / cap);
    next += period;
    if (next < now - period) next = now;
    while (clock::now() + std::chrono::milliseconds(2) < next) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    while (clock::now() < next) std::this_thread::yield();
}

}  // namespace

void PaceFrame() {
    if (Get().fps_cap != kFpsCapRefresh) {
        Pace();
        return;
    }
    // a few under, so a variable refresh display stays in its range and vsync never waits a whole refresh
    constexpr uint32_t kRefreshMargin = 3;
    uint32_t rate = RefreshRate();
    PaceAt(rate > kRefreshMargin ? rate - kRefreshMargin : 0);
}

const rows::Row* rows::Page(int page, size_t* count) {
    if (page == kKeyboardPage) {
        *count = InputRows().size();
        return InputRows().data();
    }
    if (page == kCheatsPage) {
        *count = std::size(kCheatRows);
        return kCheatRows;
    }
    if (page == kCreditsPage) {
        *count = CreditRows().size();
        return CreditRows().data();
    }
    if (page == kTouchPage) {
        *count = std::size(kTouchRows);
        return kTouchRows;
    }
    if (page == kGeneralPage) {
        *count = std::size(kGeneralRows);
        return kGeneralRows;
    }
    *count = std::size(kGraphicsRows);
    return kGraphicsRows;
}

const rows::Row* rows::Controller(size_t* count) {
    *count = std::size(kControllerRows);
    return kControllerRows;
}

std::string rows::Value(const Row& row, int step) {
    if (row.kind == Kind::Bind) return KeyText(input::Binding(size_t(row.id)));
    if (row.id == kCredit) {
        // a step on a link opens it
        const Credit* c = CreditOf(row);
        if (!c || !c->value) return " ";
        if (step && c->url) SDL_OpenURL(c->url);
        return c->value;
    }
    return RowText(row.id, step);
}

void rows::Defaults(int page) { RestoreDefaults(page); }

// the recomp's own pages share one set of defaults, too much to undo from one of them
bool rows::Restorable(int page) { return page == kKeyboardPage || page == kCheatsPage; }

// the dynamic resolution's rows while it is on, RENDER RESOLUTION while it is off
bool rows::Enabled(const Row& row) {
    if (row.kind != Kind::Value) return true;
    bool dynamic = game::Opt().dynamic_resolution;
    switch (row.id) {
        case kRenderResolution: return !dynamic;
        case kDynamicTarget:
        case kDynamicMin:
        case kDynamicMax:
        case kDynamicHold: return dynamic;
        default: return true;
    }
}

bool rows::Opens(const Row& row) {
    // the credits' rows have no arrows: the links open, the rest do nothing
    return row.kind == Kind::Value && (row.id == kTouchEdit || row.id == kCheckUpdates || row.id == kCredit);
}

bool MenuOpen() { return g_open.load(std::memory_order_relaxed); }

bool Capturing() { return g_capture >= 0; }

void ToggleMenu() {
    bool open = !MenuOpen();
    g_open.store(open, std::memory_order_relaxed);
    g_capture = -1;
    if (open) input::ReleaseCursor();
}

void DrawUi(double fps) {
    game::g_fps = float(fps);
    ImGuiIO& io = ImGui::GetIO();
    if (MenuOpen()) io.ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
    else io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    io.KeyRepeatRate = 0.1f;  // held arrows step ten times a second, ImGui's twenty is too fast for settings
    // scale the font with the output height
    io.FontGlobalScale = std::max(1.0f, io.DisplaySize.y / 720.0f);
    if (MenuOpen()) {
        PollCapture();
        DrawPanel();
    }
}

}  // namespace app
