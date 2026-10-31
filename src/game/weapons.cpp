// The weapons page of the F10 panel: any of the game's player weapons in any level.
//
// Every level's weapon table holds all seventeen player weapons, but a level ships only the first person banks
// (Levels\Level_NN\fpguns\<weapon>.db and .wdd: the gun, arms, animations and sounds) and the world models
// (Guns.bin, the "<weapon>_LOD" models a dropped gun shows) of the guns it uses. So a weapon is given the game's way:
// created from the table, put in the slot not in hand, and switched to as the d-pad does; a bank the level lacks is
// read from another level's folder, and a world model it lacks from another level's Guns.bin.
#include "game/weapons.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <deque>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "core/log.h"
#include "game/layout.h"
#include "kernel/disc.h"
#include "kernel/memory.h"
#include "xdk/hle.h"

using namespace kernel;
using namespace layout;
namespace fs = std::filesystem;

namespace {

// the weapon manager: +0 the weapon table, +4 the weapon objects (0x110 each), +8 their in use flags
constexpr uint32_t kWeaponManager = 0x2D12D0;
constexpr uint32_t kTableCount = 1;  // byte
constexpr uint32_t kTableEntries = 4;
constexpr uint32_t kEntrySize = 0x20;  // name key (8 bytes), weapon data, ...
// resource manager: the TEXTURE and MODELS dictionaries looked up by name, 11 of each
constexpr uint32_t kResources = 0x2D45D8;
constexpr uint32_t kTextureDicts = 0xC64;
constexpr uint32_t kModelDicts = 0xC90;
constexpr int kDictSlots = 11;
constexpr int kDictSlotsKept = 3;  // left free for the level's own later loads
// guest functions
constexpr uint32_t kCreateWeapon = 0x1CEE10;  // edi the manager; (owner, type, 0) -> the weapon
constexpr uint32_t kFreeWeapon = 0x1C9B90;    // edi the manager, edx the weapon
constexpr uint32_t kFindModel = 0x1F83E0;     // (resources, key low, key high) -> the model or 0
constexpr uint32_t kFixGuns = 0xB5F20;        // eax a Guns.bin in memory: relocated and its dictionaries registered
// world
constexpr uint32_t kWorldState = 0x5AE0;  // the level loader's step
constexpr uint32_t kStatePlaying = 0x1C;
constexpr uint32_t kWorldSilver = 0x5CB0;  // byte: the level loaded Guns_s.bin
// player
constexpr uint32_t kPlayerKind = 0xC4;
constexpr uint32_t kKindPlayer = 2;
constexpr uint32_t kPlayerInventory = 0x278;
// inventory: the ammunition of each type (words) first
constexpr uint32_t kInvOwner = 0x1C;
constexpr uint32_t kInvSlots = 0x20;     // the weapons, one a slot
constexpr uint32_t kInvHeld = 0x24;      // the weapon in hand
constexpr uint32_t kInvSwitchTo = 0x28;  // the weapon a switch brings out
constexpr uint32_t kInvRequest = 0x38;   // 2 asks to switch to the other slot, as the d-pad does
constexpr uint32_t kInvBusy = 0xB4;       // 0x13 while the suppressor animation runs
constexpr uint32_t kInvSlotCount = 0x42;  // byte
constexpr uint32_t kInvSlotHeld = 0x43;   // byte
constexpr bool kProbe = false;  // true: the diagnostic probes write weapons_diag.log
constexpr int kAmmoTypes = 10;
constexpr uint32_t kAmmoMax = 999;
// weapon and its data
constexpr uint32_t kWeaponType = 0;  // byte: index in the table
constexpr uint32_t kWeaponAnim = 0xD8;        // the first person animation id, 0 when idle (0x19 attach, 0x1A detach)
constexpr uint32_t kWeaponSuppressed = 0x108;  // 1 while the suppressor is on (found by diffing snapshots)
constexpr uint32_t kWeaponData = 0xEC;
constexpr uint32_t kWeaponMagazine = 0xF4;  // the main fire's magazine, +0xF8 the alternate fire's
constexpr uint32_t kMagazineData = 8;
constexpr uint32_t kDataClip = 0x60;
constexpr uint32_t kDataAmmoType = 0x64;

// the game's files under D:, the disc image when it runs from one, else the game's folder
const std::string kDiscRoot = "D:\\";

// names as the game keeps them: twelve characters of " -/0-9A-Z_" in base 40
std::string Decode(uint64_t key) {
    char s[13] = {};
    for (int i = 11; i >= 0; i--) {
        int c = int(key % 40);
        key /= 40;
        s[i] = c == 0 ? ' ' : c == 1 ? '-' : c == 2 ? '/' : c < 13 ? char('0' + c - 3) : c < 39 ? char('A' + c - 13) : '_';
    }
    std::string out = s;
    out.erase(out.find_last_not_of(' ') + 1);
    return out;
}

uint64_t Encode(const std::string& name) {
    uint64_t key = 0;
    for (size_t i = 0; i < 12; i++) {
        char ch = i < name.size() ? char(std::toupper(uint8_t(name[i]))) : ' ';
        int c = ch == '-' ? 1 : ch == '/' ? 2 : ch >= '0' && ch <= '9' ? 3 + ch - '0' : ch >= 'A' && ch <= 'Z' ? 13 + ch - 'A' : ch == '_' ? 39 : 0;
        key = key * 40 + uint64_t(c);
    }
    return key;
}

// the names the game's own text gives them, told apart by clip and ammunition; the silver ones end in 5
const std::map<std::string, std::string> kNames = {
    {"PST", "Pistol"},         {"PS5", "Pistol (Silenced)"},  {"GK1", "Glock"},        {"GK5", "Glock (Silenced)"},
    {"MGN", "Magnum"},         {"MG5", "Magnum"},             {"SMG", "Uzi"},          {"SM5", "Uzi"},
    {"SM3", "MAC-10"},         {"MC5", "MAC-10"},             {"MP1", "MP5"},          {"MP5", "MP5 (Suppressed)"},
    {"P90", "P90"},            {"P95", "P90"},                {"AK1", "AK-47"},        {"AK5", "AK-47"},
    {"ASR", "G36C"},           {"AS5", "G36C"},               {"M16", "M16"},          {"M45", "M16"},
    {"BNS", "M16A2 + M203"},   {"SHG", "SPAS 12"},            {"SH5", "SPAS 12"},      {"RM1", "Remington 870"},
    {"RM5", "Remington 870"},  {"SNR", "Walther 2000"},       {"SN5", "Walther 2000 (Silenced)"}, {"HVY", "M249"},
    {"HV5", "M249"},           {"RPG", "RPG"},                {"GRL", "Grenade launcher"},
};

std::string Label(const std::string& key_name) {
    std::string code = key_name.size() > 4 && key_name.compare(0, 4, "BG1_") == 0 ? key_name.substr(4) : key_name;
    auto it = kNames.find(code);
    return it == kNames.end() ? code : it->second;
}

// ---- automatic suppressor: weapon+0x108 is 1 while the suppressor is on ----------------------------------------
// Weapons whose table code is listed get it set once when they are in hand and idle. The default list is what the
// tests proved (Glock, AK-47, Pistol, MP5, Walther 2000, Uzi, G36C, MAC-10, P90, M16); suppressor_codes.txt beside the log (codes separated by spaces, e.g. "GK1 AK1 MP1")
// replaces it, to try other weapons.
std::atomic<bool> g_auto_suppressor{false};  // the panel offers both versions of each weapon; true also forces it on pickups
std::set<std::string> g_supp_codes;
std::map<uint32_t, int> g_applied;  // weapon object -> type it was handled as
uint32_t g_swap_back = 0;           // a weapon to bring back out once the other one is in hand

std::string CodeOf(const std::string& key_name) {
    return key_name.size() > 4 && key_name.compare(0, 4, "BG1_") == 0 ? key_name.substr(4) : key_name;
}

// black.log takes none of the LOGI lines, so the weapons' own diagnostics also go to a file beside it
void DiagLog(const char* fmt, ...) {
    char line[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    LOGI("%s", line);
    static std::mutex lock;
    std::lock_guard<std::mutex> guard(lock);
    static const char* path = "/storage/emulated/0/Black/SaveData/weapons_diag.log";
    static bool first = true;
    static int lines = 0;
    if (first) {  // every run starts a fresh file; the run before is kept as weapons_diag_prev.log
        first = false;
        std::rename(path, "/storage/emulated/0/Black/SaveData/weapons_diag_prev.log");
    }
    if (lines >= 3000) return;  // a runaway probe cannot fill the storage
    lines++;
    if (FILE* f = std::fopen(path, "a")) {
        std::fputs(line, f);
        std::fputc('\n', f);
        std::fclose(f);
    }
}

void LoadSuppressorCodes() {
    static bool done;
    if (done) return;
    done = true;
    g_supp_codes = {"GK1", "AK1", "PST", "MP1", "SNR", "SMG", "ASR", "SM3", "P90", "M16"};  // all checked in game
    std::ifstream f("/storage/emulated/0/Black/SaveData/suppressor_codes.txt");
    std::set<std::string> codes;
    std::string tok;
    while (f >> tok) {
        for (char& c : tok) c = char(std::toupper(uint8_t(c)));
        codes.insert(CodeOf(tok));
    }
    if (!codes.empty()) g_supp_codes = codes;
    std::string all;
    for (const std::string& c : g_supp_codes) all += c + " ";
    DiagLog("weapons: auto suppressor for %s", all.c_str());
}

bool SuppSupportedCode(const std::string& code) {
    LoadSuppressorCodes();
    return g_supp_codes.count(code) != 0;
}

// does `type` of this manager's table take a suppressor?
bool SuppSupportedType(uint32_t manager, int type) {
    uint32_t table = manager ? R32(manager) : 0;
    if (!table || type < 0) return false;
    int count = G<uint8_t>(table + kTableCount);
    uint32_t entries = R32(table + kTableEntries);
    if (!entries || type >= count || count > 64) return false;
    uint32_t e = entries + uint32_t(type) * kEntrySize;
    return SuppSupportedCode(CodeOf(Decode(R32(e) | uint64_t(R32(e + 4)) << 32)));
}

// what the game thread found and the panel shows
std::mutex g_lock;
weapons::View g_view;
std::atomic<int> g_give{-1};
std::atomic<bool> g_fill{false};

void SetStatus(const std::string& text) {
    std::lock_guard<std::mutex> lock(g_lock);
    g_view.status = text;
    DiagLog("weapons: %s", text.c_str());
}

std::string Lower(std::string s) {
    for (char& c : s) c = char(std::tolower(uint8_t(c)));
    return s;
}

// a file of the game, by its path under D:
bool GameFileExists(const std::string& path) {
    if (const Disc* disc = MountedDisc()) {
        const Disc::Entry* e = disc->Find(path);
        return e && !e->dir;
    }
    std::string host = ResolvePath(kDiscRoot + path, 0);
    std::error_code ec;
    return !host.empty() && fs::is_regular_file(host, ec);
}

// a file of the game, read whole: from the disc image when it runs from one, else from the game's folder
bool LoadGameFile(const std::string& path, std::vector<uint8_t>& out) { return ReadGameFile(kDiscRoot + path, out); }

int LevelNumber(std::string name) {
    name = Lower(name);
    bool ok = name.size() == 8 && name.compare(0, 6, "level_") == 0 && std::isdigit(uint8_t(name[6])) &&
              std::isdigit(uint8_t(name[7]));
    return ok ? (name[6] - '0') * 10 + (name[7] - '0') : -1;
}

// the level folders the game has, by number
const std::vector<int>& Levels() {
    static const std::vector<int> levels = [] {
        std::vector<int> out;
        if (const Disc* disc = MountedDisc()) {
            if (const Disc::Entry* dir = disc->Find("Levels"))
                for (const Disc::Entry& e : dir->children)
                    if (e.dir && LevelNumber(e.name) >= 0) out.push_back(LevelNumber(e.name));
        } else {
            std::error_code ec;
            for (const auto& e : fs::directory_iterator(ResolvePath(kDiscRoot + "Levels", 0), ec))
                if (e.is_directory(ec) && LevelNumber(e.path().filename().string()) >= 0)
                    out.push_back(LevelNumber(e.path().filename().string()));
        }
        std::sort(out.begin(), out.end());
        return out;
    }();
    return levels;
}

// ---- first person banks: Level_NN\fpguns\ a level lacks is read from another level's -----------------------------

// in place, the same length: the level number of a bank path to one that has the file
void RedirectBank(uint32_t path_va) {
    std::string path = ReadCString(path_va, 260);
    std::string low = Lower(path);
    size_t at = low.find("level_");
    if (at == std::string::npos || low.find("\\fpguns\\", at) != at + 8 || !std::isdigit(uint8_t(low[at + 6])) ||
        !std::isdigit(uint8_t(low[at + 7])))
        return;
    static std::mutex lock;
    static std::map<std::string, int> chosen;  // lowercase path -> the level to read it from, -1 as it is
    std::lock_guard<std::mutex> guard(lock);
    auto it = chosen.find(low);
    if (it == chosen.end()) {
        size_t root = path.find(":\\");
        size_t skip = root == std::string::npos ? 0 : root + 2;  // a D:\ in front
        std::string rel = path.substr(skip);
        int level = -1;
        if (!GameFileExists(rel)) {
            for (int n : Levels()) {
                char digits[3];
                std::snprintf(digits, sizeof(digits), "%02d", n);
                std::string other = rel;
                other.replace(at - skip + 6, 2, digits);
                if (GameFileExists(other)) {
                    level = n;
                    break;
                }
            }
            if (level < 0) LOGW("weapons: no level has %s", path.c_str());
            else LOGI("weapons: %s from Level_%02d", path.c_str(), level);
        }
        it = chosen.emplace(low, level).first;
    }
    if (it->second < 0) return;
    char* s = static_cast<char*>(HostPtr(path_va));
    s[at + 6] = char('0' + it->second / 10);
    s[at + 7] = char('0' + it->second % 10);
}

// ---- world models: another level's Guns.bin for the "_LOD" models this one lacks ----------------------------------

struct GunsFile {
    std::string path;  // Levels\Level_NN\Stg_0001\Guns.bin
    bool silver;       // Guns_s.bin
    std::vector<uint64_t> models;
    uint32_t buffer = 0;     // guest copy, made the first time it is needed
    uint32_t dicts[2] = {};  // its TEXTURE and MODELS dictionaries while registered, this level
};

std::vector<GunsFile>& GunsFiles() {
    static std::vector<GunsFile> files = [] {
        std::vector<GunsFile> out;
        for (int n : Levels()) {
            for (const char* name : {"Guns.bin", "Guns_s.bin"}) {
                char path[64];
                std::snprintf(path, sizeof(path), "Levels\\Level_%02d\\Stg_0001\\%s", n, name);
                std::vector<uint8_t> d;
                if (!GameFileExists(path) || !LoadGameFile(path, d)) continue;
                auto u32 = [&](size_t at) {
                    uint32_t v = 0;
                    if (at + 4 <= d.size()) std::memcpy(&v, &d[at], 4);
                    return v;
                };
                // +4 the MODELS dictionary: +8 its count, +0xC its entries, 16 bytes each from the name key
                uint32_t dict = u32(4);
                uint32_t count = u32(dict + 8), entries = dict + u32(dict + 0xC);
                GunsFile g{path, name[4] == '_', {}};
                for (uint32_t i = 0; i < count && i < 64; i++) {
                    uint64_t key = u32(entries + i * 16) | uint64_t(u32(entries + i * 16 + 4)) << 32;
                    if (key) g.models.push_back(key);
                }
                out.push_back(std::move(g));
            }
        }
        return out;
    }();
    return files;
}

int FreeSlots(uint32_t list) {
    int free = 0;
    for (int i = 0; i < kDictSlots; i++) free += R32(list + i * 4) == 0;
    return free;
}

void Unregister(uint32_t list, uint32_t dict) {
    for (int i = 0; i < kDictSlots; i++)
        if (dict && R32(list + i * 4) == dict) W32(list + i * 4, 0);
}

// a level is loading: what this one registered goes, the game may have cleared it already
void ForgetExtraGuns() {
    uint32_t res = R32(kResources);
    for (GunsFile& g : GunsFiles()) {
        if (res) {
            Unregister(res + kTextureDicts, g.dicts[0]);
            Unregister(res + kModelDicts, g.dicts[1]);
        }
        g.dicts[0] = g.dicts[1] = 0;
    }
}

// a world model, from another level's Guns.bin when this level lacks it; false when there is no room
bool EnsureModel(X86Context& ctx, uint64_t key, bool silver) {
    uint32_t res = R32(kResources);
    if (!res) return false;
    if (CallGuest(ctx, kFindModel, {res, uint32_t(key), uint32_t(key >> 32)})) return true;
    GunsFile* pick = nullptr;
    for (GunsFile& g : GunsFiles()) {
        if (g.dicts[1] || std::find(g.models.begin(), g.models.end(), key) == g.models.end()) continue;
        if (!pick || (g.silver == silver && pick->silver != silver)) pick = &g;
    }
    if (!pick) return false;
    if (FreeSlots(res + kTextureDicts) <= kDictSlotsKept || FreeSlots(res + kModelDicts) <= kDictSlotsKept) return false;
    std::vector<uint8_t> d;
    if (!LoadGameFile(pick->path, d) || d.size() < 16) return false;
    if (!pick->buffer) pick->buffer = mem::AllocContiguous(uint32_t(d.size()), 0, 0xFFFFFFFF, 0x1000);
    if (!pick->buffer) return false;
    std::memcpy(HostPtr(pick->buffer), d.data(), d.size());  // as on disc again: the game relocates it in place
    // known before the fix up: its models look their textures up while it runs (sub_001FBCE0 below)
    pick->dicts[0] = pick->buffer + R32(pick->buffer);
    pick->dicts[1] = pick->buffer + R32(pick->buffer + 4);
    uint32_t eax = ctx.eax;
    ctx.eax = pick->buffer;
    CallGuest(ctx, kFixGuns, {});
    ctx.eax = eax;
    LOGI("weapons: world models from %s", pick->path.c_str());
    return CallGuest(ctx, kFindModel, {res, uint32_t(key), uint32_t(key >> 32)}) != 0;
}

bool EnsureWorldModel(X86Context& ctx, const std::string& weapon_name, bool silver) {
    return EnsureModel(ctx, Encode(weapon_name + "_LOD"), silver);
}

// A dropped weapon becomes a pickup with its world model, a null one crashes: it is loaded now if it can be, else
// the drop makes none, as when the game's pickups are all in use. pm the pickup manager: +0x5844 the pickup table
// (count byte, +8 entries of 0x18: weapon key, model key, weapon type byte at +0x10), +0x5800 its models found
constexpr uint32_t kPickupTable = 0x5844;
constexpr uint32_t kPickupModels = 0x5800;
constexpr uint32_t kPickupSize = 0x18;
bool DropHasModel(X86Context& ctx, uint32_t pm, uint8_t type) {
    uint32_t table = pm ? R32(pm + kPickupTable) : 0;
    if (!table) return true;
    uint32_t entries = R32(table + 8);
    int count = G<uint8_t>(table);
    int found = -1;
    for (int i = 0; i < count && found < 0; i++)
        if (G<uint8_t>(entries + uint32_t(i) * kPickupSize + 0x10) == type) found = i;
    if (found < 0) return false;
    uint32_t models = R32(pm + kPickupModels);
    if (models && R32(models + uint32_t(found) * 4)) return true;
    uint32_t e = entries + uint32_t(found) * kPickupSize;
    uint64_t key = R32(e + 8) | uint64_t(R32(e + 12)) << 32;
    uint32_t world = R32(kWorld);
    return EnsureModel(ctx, key, world && G<uint8_t>(world + kWorldSilver) != 0);
}

// ---- the player and the level's weapon table ---------------------------------------------------------------------

struct Level {
    uint32_t world, player, inventory, manager, entries;
    int count;
};

bool ReadLevel(Level& l) {
    l.world = R32(kWorld);
    if (!l.world || R32(l.world + kWorldState) != kStatePlaying) return false;
    l.player = l.world + kWorldPlayer;
    l.inventory = l.player + kPlayerInventory;
    if (R32(l.player + kPlayerKind) != kKindPlayer || R32(l.inventory + kInvOwner) != l.player ||
        !R32(l.inventory + kInvSlots))
        return false;
    l.manager = R32(kWeaponManager);
    uint32_t table = l.manager ? R32(l.manager) : 0;
    if (!table || !R32(l.manager + 4)) return false;
    l.count = G<uint8_t>(table + kTableCount);
    l.entries = R32(table + kTableEntries);
    return l.entries && l.count > 0 && l.count <= 64;
}

uint64_t EntryKey(const Level& l, int type) {
    uint32_t e = l.entries + uint32_t(type) * kEntrySize;
    return R32(e) | uint64_t(R32(e + 4)) << 32;
}

uint32_t Slot(const Level& l, int i) { return R32(R32(l.inventory + kInvSlots) + uint32_t(i) * 4); }

void Refresh() {
    Level l;
    weapons::View v;
    v.in_level = ReadLevel(l);
    if (v.in_level) {
        static uint32_t dumped_manager;  // once a level: every type of the table, to see which ones are suppressed
        if (l.manager != dumped_manager) {
            dumped_manager = l.manager;
            for (int t = 0; t < l.count; t++)
                DiagLog("weapons: table %d = %s", t, Decode(EntryKey(l, t)).c_str());
        }
        int slots = std::min<int>(G<uint8_t>(l.inventory + kInvSlotCount), 2);
        for (int t = 0; t < l.count; t++) {
            std::string name = Decode(EntryKey(l, t));
            if (name.empty()) continue;
            bool plain = false, supp = false;
            for (int i = 0; i < slots; i++)
                if (uint32_t w = Slot(l, i))
                    if (G<uint8_t>(w + kWeaponType) == t) (R32(w + kWeaponSuppressed) ? supp : plain) = true;
            bool can = SuppSupportedCode(CodeOf(name));
            v.choices.push_back({Label(name), t, can ? plain : plain || supp});
            if (can) v.choices.push_back({Label(name) + " + Suppressor", t | 0x100, supp});  // 0x100: with the suppressor
        }
        std::sort(v.choices.begin(), v.choices.end(), [](const auto& a, const auto& b) { return a.label < b.label; });
    }
    std::lock_guard<std::mutex> lock(g_lock);
    v.status = g_view.status;
    g_view = std::move(v);
}

// the type's ammunition to at least five clips
void FillType(const Level& l, uint32_t data, bool top_up) {
    if (!data) return;
    uint32_t type = R32(data + kDataAmmoType), clip = G<uint16_t>(data + kDataClip);
    if (type >= kAmmoTypes) return;
    uint16_t& ammo = G<uint16_t>(l.inventory + type * 2);
    uint32_t want = std::min(kAmmoMax, top_up ? kAmmoMax : std::max(clip * 5, 1u));
    if (ammo < want) ammo = uint16_t(want);
}

void FillWeapon(const Level& l, uint32_t weapon, bool top_up) {
    for (uint32_t m = 0; m < 2; m++)
        if (uint32_t mag = R32(weapon + kWeaponMagazine + m * 4)) FillType(l, R32(mag + kMagazineData), top_up);
}

void DoGive(X86Context& ctx, int code) {
    Level l;
    if (!ReadLevel(l)) return SetStatus("Only during a level");
    int type = code & 0xFF;
    bool suppressed = (code & 0x100) != 0;
    if (code < 0 || type >= l.count) return;
    std::string name = Decode(EntryKey(l, type));
    if (G<uint8_t>(l.inventory + kInvSlotCount) < 2) return SetStatus("The player has a single weapon slot here");
    if (R32(l.inventory + kInvRequest) || R32(l.inventory + kInvHeld) != R32(l.inventory + kInvSwitchTo))
        return SetStatus("Wait for the weapon switch to end");
    int held = G<int8_t>(l.inventory + kInvSlotHeld);
    int other = held == 0 ? 1 : 0;
    uint32_t slots = R32(l.inventory + kInvSlots);
    uint32_t old = R32(slots + uint32_t(other) * 4);
    if (old && old == R32(l.inventory + kInvHeld)) return SetStatus("Wait for the weapon switch to end");
    // the game never keeps two of one gun: the one in hand only changes its suppressor (the sound follows at the next
    // weapon switch), and one in the other slot with the same suppressor state is left alone
    std::string shown = Label(name) + (suppressed ? " + Suppressor" : "");
    if (uint32_t in_hand = R32(l.inventory + kInvHeld); in_hand && G<uint8_t>(in_hand + kWeaponType) == type) {
        if ((R32(in_hand + kWeaponSuppressed) != 0) == suppressed) return SetStatus(shown + " is already in hand");
        W32(in_hand + kWeaponSuppressed, suppressed ? 1 : 0);
        g_applied[in_hand] = type;
        if (!old) return SetStatus(shown + " set in hand (sound changes after a weapon switch)");
        W32(l.inventory + kInvRequest, 2);  // out and back in: the sound is chosen when a weapon comes out
        g_swap_back = in_hand;
        return SetStatus(shown + " set");
    }
    if (old && G<uint8_t>(old + kWeaponType) == type && (R32(old + kWeaponSuppressed) != 0) == suppressed)
        return SetStatus(shown + " is already in the other slot");
    // the gun made first: a full pool keeps the old one
    uint32_t edi = ctx.edi;
    ctx.edi = l.manager;
    uint32_t weapon = CallGuest(ctx, kCreateWeapon, {l.player, uint32_t(type), 0});
    ctx.edi = edi;
    if (!weapon) return SetStatus("The game has no room for another weapon");
    if (old) {
        uint32_t edx = ctx.edx;
        ctx.edi = l.manager;
        ctx.edx = old;
        CallGuest(ctx, kFreeWeapon, {});
        ctx.edi = edi;
        ctx.edx = edx;
    }
    W32(slots + uint32_t(other) * 4, weapon);
    W32(weapon + kWeaponSuppressed, suppressed ? 1 : 0);  // before it comes out: the sound is chosen then
    g_applied[weapon] = type;
    FillWeapon(l, weapon, false);
    W32(l.inventory + kInvRequest, 2);  // out it comes, as the d-pad brings it
    bool model = EnsureWorldModel(ctx, name, G<uint8_t>(l.world + kWorldSilver) != 0);
    SetStatus(Label(name) + (suppressed ? " + Suppressor" : "") + (model ? " given" : " given (dropped, it will vanish)"));
    Refresh();
}

void DoFill() {
    Level l;
    if (!ReadLevel(l)) return SetStatus("Only during a level");
    for (int i = 0; i < 2; i++)
        if (uint32_t w = Slot(l, i)) FillWeapon(l, w, true);
        SetStatus("Ammunition full");
}

}  // namespace

namespace {

// ---- temporary: finds where the game keeps "suppressor on" ----------------------------------------------------
// Logs the words of the held weapon, its data, its magazine and the inventory that settle on a new value and stay
// there. A word that keeps changing (positions, animation clocks) is muted after a few changes: a flag is not one.
// Lines carry the seconds since the level began and a "mark" every five seconds, to tell what was done when.
struct Probe {
    uint32_t cur, stable;
    int changes;
    std::chrono::steady_clock::time_point since;
};

double Seconds() {
    static const auto start = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

struct Region {
    std::vector<Probe> words;
    void Scan(const char* name, uint32_t at, uint32_t size) {
        auto now = std::chrono::steady_clock::now();
        if (words.empty()) {
            words.resize(size / 4);
            for (uint32_t i = 0; i < words.size(); i++) words[i] = {R32(at + i * 4), R32(at + i * 4), 0, now};
            return;
        }
        for (uint32_t i = 0; i < words.size(); i++) {
            Probe& p = words[i];
            uint32_t v = R32(at + i * 4);
            if (v != p.cur) {
                p.cur = v;
                p.since = now;
                p.changes++;
            } else if (p.cur != p.stable && now - p.since > std::chrono::milliseconds(1200)) {
                if (p.changes <= 6)
                    DiagLog("t=%.1f probe %s+0x%X: %08X -> %08X", Seconds(), name, unsigned(i * 4), unsigned(p.stable),
                            unsigned(p.cur));
                p.stable = p.cur;
            }
        }
    }
};

void SuppressorProbe() {
    static uint32_t last_weapon, last_data;
    static int last_type = -1, last_mark = -1;
    static Region object, data, magazine, inventory;
    Level l;
    if (!ReadLevel(l)) {
        last_weapon = 0;
        return;
    }
    uint32_t w = R32(l.inventory + kInvHeld);
    if (!w) return;
    int mark = int(Seconds() / 5);
    if (mark != last_mark) {
        last_mark = mark;
        DiagLog("t=%.1f mark %d", Seconds(), mark * 5);
    }
    int type = G<uint8_t>(w + kWeaponType);
    if (w != last_weapon || type != last_type) {
        last_weapon = w;
        last_type = type;
        object.words.clear();
        data.words.clear();
        magazine.words.clear();
        inventory.words.clear();
        std::string name = type < l.count ? Label(Decode(EntryKey(l, type))) : "?";
        DiagLog("t=%.1f probe: weapon %08X type %d %s", Seconds(), unsigned(w), type, name.c_str());
    }
    uint32_t d = R32(w + kWeaponData);
    if (d != last_data) {
        last_data = d;
        data.words.clear();
        DiagLog("t=%.1f probe: data pointer %08X", Seconds(), unsigned(d));
    }
    object.Scan("weapon", w, 0x110);
    if (d) data.Scan("data", d, 0x100);
    if (uint32_t mag = R32(w + kWeaponMagazine)) magazine.Scan("magazine", mag, 0x80);
    inventory.Scan("inventory", l.inventory, 0x100);
}

// ---- temporary: what the pickup of a suppressor changes ---------------------------------------------------------
// The game's pickup message (sub_0012B510: ecx the kind, 4 the "silent" ones, a suppressor; al which of the nine) marks
// the moment. The words of the weapon pool, the inventory and the player that held still for two seconds before it and
// differ a second after are logged, so no timing is needed.
struct Watch {
    std::vector<uint32_t> addr, now, before;
    std::deque<std::vector<uint32_t>> hist;  // the last ticks, the oldest is the state before the pickup
    std::vector<double> changed;             // seconds of each word's last change
    std::vector<char> quiet;
    double event = -1;
};
Watch g_watch;

void WatchGather(const Level& l, std::vector<uint32_t>& addr) {
    addr.clear();
    uint32_t pool = R32(l.manager + 4);
    for (uint32_t i = 0; pool && i < 8 * 0x110 / 4; i++) addr.push_back(pool + i * 4);
    for (uint32_t i = 0; i < 0x80; i++) addr.push_back(l.inventory + i * 4);
    for (uint32_t i = 0; i < 0xC0; i++) addr.push_back(l.player + i * 4);
}

std::string WatchName(const Level& l, uint32_t a) {
    char s[64];
    uint32_t pool = R32(l.manager + 4);
    if (pool && a >= pool && a < pool + 8 * 0x110) std::snprintf(s, sizeof(s), "weapon[%u]+0x%X", (a - pool) / 0x110, (a - pool) % 0x110);
    else if (a >= l.inventory && a < l.inventory + 0x200) std::snprintf(s, sizeof(s), "inventory+0x%X", a - l.inventory);
    else std::snprintf(s, sizeof(s), "player+0x%X", a - l.player);
    return s;
}

void PickupWatch() {
    Watch& w = g_watch;
    Level l;
    if (!ReadLevel(l)) {
        w = Watch();
        return;
    }
    double t = Seconds();
    std::vector<uint32_t> addr;
    WatchGather(l, addr);
    std::vector<uint32_t> now(addr.size());
    for (size_t i = 0; i < addr.size(); i++) now[i] = R32(addr[i]);
    if (addr != w.addr) {
        w = Watch();
        w.addr = addr;
        w.now = now;
        w.changed.assign(addr.size(), -100.0);
        w.quiet.assign(addr.size(), 0);
        return;
    }
    for (size_t i = 0; i < addr.size(); i++)
        if (now[i] != w.now[i]) w.changed[i] = t;
    w.now = now;
    w.hist.push_back(now);
    if (w.hist.size() > 8) w.hist.pop_front();
    if (w.event >= 0 && t - w.event > 1.0) {
        int shown = 0;
        for (size_t i = 0; i < addr.size() && shown < 80; i++) {
            if (!w.quiet[i] || now[i] == w.before[i] || t - w.changed[i] < 0.4) continue;
            DiagLog("t=%.1f SILENT-PICKUP %s: %08X -> %08X", t, WatchName(l, addr[i]).c_str(), unsigned(w.before[i]),
                    unsigned(now[i]));
            shown++;
        }
        DiagLog("t=%.1f SILENT-PICKUP end, %d words", t, shown);
        w.event = -1;
    }
}

void PickupEvent(uint32_t category, uint32_t type, uint32_t ret) {
    Level l;
    if (!ReadLevel(l)) return;
    if (kProbe && category != 0)
        DiagLog("t=%.1f pickup message: kind %u, type %u, caller %08X", Seconds(), category, type, unsigned(ret));
    Watch& w = g_watch;
    if (category != 4 || w.addr.empty() || w.hist.empty()) return;
    w.event = Seconds();
    w.before = w.hist.front();
    for (size_t i = 0; i < w.addr.size(); i++) w.quiet[i] = w.event - w.changed[i] > 2.0;
}

// the weapon in hand, once it is idle: its suppressor goes on, if its type is on the list and it is not on already
void AutoSuppressor(const Level& l) {
    if (!g_auto_suppressor) return;
    LoadSuppressorCodes();
    uint32_t w = R32(l.inventory + kInvHeld);
    if (!w || R32(l.inventory + kInvRequest) || w != R32(l.inventory + kInvSwitchTo)) return;  // a switch is on
    if (R32(w + kWeaponAnim) || R32(l.inventory + kInvBusy)) return;                            // an animation runs
    int type = G<uint8_t>(w + kWeaponType);
    auto it = g_applied.find(w);
    if (it != g_applied.end() && it->second == type) return;
    g_applied[w] = type;
    if (type >= l.count) return;
    std::string code = CodeOf(Decode(EntryKey(l, type)));
    if (!g_supp_codes.count(code) || R32(w + kWeaponSuppressed)) return;
    W32(w + kWeaponSuppressed, 1);
    DiagLog("weapons: suppressor on for %s (weapon %08X)", code.c_str(), unsigned(w));
}

// the weapon whose suppressor was changed in hand went out; once the other one is in hand it is brought back
void SwapBack(const Level& l) {
    if (!g_swap_back) return;
    uint32_t held = R32(l.inventory + kInvHeld);
    if (R32(l.inventory + kInvRequest) || held != R32(l.inventory + kInvSwitchTo)) return;  // still switching
    if (held != g_swap_back && (Slot(l, 0) == g_swap_back || Slot(l, 1) == g_swap_back))
        W32(l.inventory + kInvRequest, 2);
    g_swap_back = 0;
}

// logs the level loader's step whenever it changes: shows that the tick runs and why a level is not read
void Heartbeat() {
    static uint32_t last_state = 0xFFFFFFFF;
    static int last_ok = -1;
    uint32_t world = R32(kWorld);
    uint32_t state = world ? R32(world + kWorldState) : 0xFFFFFFFE;
    Level l;
    int ok = ReadLevel(l) ? 1 : 0;
    if (state == last_state && ok == last_ok) return;
    last_state = state;
    last_ok = ok;
    DiagLog("weapons: tick world=%08X state=%X level=%d", unsigned(world), unsigned(state), ok);
    if (world && !ok && state == kStatePlaying) {
        uint32_t player = world + kWorldPlayer, inv = player + kPlayerInventory;
        DiagLog("weapons: kind=%u owner=%08X (player %08X) slots=%08X manager=%08X", unsigned(R32(player + kPlayerKind)),
             unsigned(R32(inv + kInvOwner)), unsigned(player), unsigned(R32(inv + kInvSlots)),
             unsigned(R32(kWeaponManager)));
    }
}

}  // namespace

namespace weapons {

View Current() {
    std::lock_guard<std::mutex> lock(g_lock);
    return g_view;
}

void Give(int type) { g_give = type; }

void FillAmmo() { g_fill = true; }

void SetAutoSuppressor(bool on) { g_auto_suppressor = on; }  // add its declaration to game/weapons.h

}  // namespace weapons

// game thread, each menu and HUD tick (hooks.cpp MenuTick)
void WeaponsTick(X86Context& ctx) {
    static int tick;
    if (int type = g_give.exchange(-1); type >= 0) DoGive(ctx, type);
    if (g_fill.exchange(false)) DoFill();
    if (++tick % 10 == 0) Refresh();
    Heartbeat();
    Level l;
    if (ReadLevel(l)) {
        AutoSuppressor(l);
        SwapBack(l);
    } else {
        g_applied.clear();
        g_swap_back = 0;
    }
    if (kProbe) {
        PickupWatch();
        SuppressorProbe();
    }
}

// the game's file opening: a first person bank the level lacks comes from another level's folder
X86_EXTERN_FUNC(__imp__sub_000C2C50);
X86_FUNC(sub_000C2C50) {
    RedirectBank(R32(ctx.esp + 4));
    __imp__sub_000C2C50(ctx, base);
}

// A texture by name, (resources, name) stdcall: the game takes only dictionaries in its own memory heaps, so the
// ones added from other levels' Guns.bin are searched after it (their world models showed white without)
constexpr uint32_t kFindInTextures = 0x14B00;  // esi a TEXTURE dictionary; (name) -> the texture or 0
X86_EXTERN_FUNC(__imp__sub_001FBCE0);
X86_FUNC(sub_001FBCE0) {
    uint32_t name = R32(ctx.esp + 8);
    __imp__sub_001FBCE0(ctx, base);
    if (ctx.eax || !name) return;
    for (const GunsFile& g : GunsFiles()) {
        if (!g.dicts[0]) continue;
        uint32_t esi = ctx.esi;
        ctx.esi = g.dicts[0];
        uint32_t texture = CallGuest(ctx, kFindInTextures, {name});
        ctx.esi = esi;
        if (texture) {
            ctx.eax = texture;
            return;
        }
    }
}

// a weapon dropped as a pickup: eax the pickup manager, (type, ...) stdcall, five arguments
X86_EXTERN_FUNC(__imp__sub_001E0EB0);
X86_FUNC(sub_001E0EB0) {
    uint8_t type = G<uint8_t>(ctx.esp + 4);
    if (!DropHasModel(ctx, ctx.eax, type)) {
        LOGI("weapons: no world model for weapon %u, the drop makes no pickup", type);
        Return(ctx, 0, 5);
        return;
    }
    __imp__sub_001E0EB0(ctx, base);
}

// a level's Guns.bin is in: the world models added for the last level go first
X86_EXTERN_FUNC(__imp__sub_001D3790);
X86_FUNC(sub_001D3790) {
    ForgetExtraGuns();
    __imp__sub_001D3790(ctx, base);
}

// A weapon created (a pickup, the level's loadout, the panel): with auto suppressor on, a player's weapon of a listed
// type gets it at once, before it is first brought out (the sound is chosen then).
// edi the weapon manager; (owner, type, 0) on the stack; eax the weapon after.
X86_EXTERN_FUNC(__imp__sub_001CEE10);
X86_FUNC(sub_001CEE10) {
    uint32_t world = R32(kWorld);
    int type = int(R32(ctx.esp + 8) & 0xFF);
    bool want = g_auto_suppressor && world && R32(ctx.esp + 4) == world + kWorldPlayer && SuppSupportedType(ctx.edi, type);
    __imp__sub_001CEE10(ctx, base);
    if (!ctx.eax) return;
    g_applied.erase(ctx.eax);
    if (want) {
        W32(ctx.eax + kWeaponSuppressed, 1);
        g_applied[ctx.eax] = type;
    }
}

// the game's pickup message: (ecx kind, al type, dl, ...): a suppressor picked up is kind 4
X86_EXTERN_FUNC(__imp__sub_0012B510);
X86_FUNC(sub_0012B510) {
    PickupEvent(ctx.ecx, ctx.eax & 0xFF, R32(ctx.esp));
    __imp__sub_0012B510(ctx, base);
}
