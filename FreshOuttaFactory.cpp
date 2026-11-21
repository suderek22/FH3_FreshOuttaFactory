#include <windows.h>
#include <atomic>
#include <cstring>
#include <cwchar>

#include "fh3mod.h"

namespace {
constexpr wchar_t kSection[] = L"settings";
constexpr char kPlatformVersion[] = "1.4.2";
constexpr unsigned kRenderSnapshotCall = 0x1E7D0E9;
constexpr unsigned kRenderSnapshotOffset = 0x1800;

const FH3ModApi* api;
FH3Mod* mod;
wchar_t iniPath[MAX_PATH];

struct Option {
    const char* key;
    const wchar_t* iniKey;
    const char* label;
    const char* description;
    int defaultValue;
    std::atomic<bool> enabled{false};
};

Option noDamage{
    "NoDamage", L"NoDamage", "No crash damage (all cars)",
    "Suppress new visual crash damage and detached body parts for the player and Drivatars.", 1
};
Option noDirt{
    "NoDirt", L"NoDirt", "Always clean (no dirt)",
    "Hide dirt and mud patches on the player car and Drivatars.", 0
};

using ApplyDamage = void (*)(void*, const void*, const void*, float, float, float, unsigned);
using DetachPart = void (*)(void*, unsigned);
using UpdateRenderSnapshot = void (*)(void*);

void* originalApplyDamage;
void* originalDetachPart;
void* originalUpdateRenderSnapshot;

void applyDamage(void* car, const void* damage, const void* position,
                 float impact, float a5, float a6, unsigned flags) {
    if (!noDamage.enabled.load())
        reinterpret_cast<ApplyDamage>(originalApplyDamage)(car, damage, position, impact, a5, a6, flags);
}

void detachPart(void* car, unsigned part) {
    if (!noDamage.enabled.load())
        reinterpret_cast<DetachPart>(originalDetachPart)(car, part);
}

void updateRenderSnapshot(void* car) {
    reinterpret_cast<UpdateRenderSnapshot>(originalUpdateRenderSnapshot)(car);
    if (!noDirt.enabled.load())
        return;

    // Filter the render snapshot before the next game call submits it.
    auto* snapshot = static_cast<unsigned char*>(car) + kRenderSnapshotOffset;
    std::memset(snapshot + 0x1A8, 0, 8);      // Dirt and mud amounts
    std::memset(snapshot + 0x780, 0, 16);     // Random dirtiness vector
    std::memset(snapshot + 0x7B0, 0, 4);      // Mud dampness
}

struct HookSite {
    const char* name;
    unsigned rva;
    const char* expected;
    size_t length;
    void* detour;
    void** original;
};

bool matches(const unsigned char* game, const HookSite& site) {
    return std::memcmp(game + site.rva, site.expected, site.length) == 0;
}

bool installPair(unsigned char* game, const HookSite& first, const HookSite& second) {
    if (!matches(game, first) || !matches(game, second))
        return false;

    FH3Hook* firstHook = api->hook_function(mod, first.name, game + first.rva, first.detour, first.original);
    FH3Hook* secondHook = api->hook_function(mod, second.name, game + second.rva, second.detour, second.original);
    if (!firstHook || !secondHook)
        return false;

    if (api->enable_hooks(mod, firstHook) && api->enable_hooks(mod, secondHook))
        return true;

    api->disable_hooks(mod, firstHook);
    api->disable_hooks(mod, secondHook);
    return false;
}

DWORD WINAPI installHooks(void*) {
    auto* game = static_cast<unsigned char*>(api->wait_for_code(60000));
    if (!game) {
        api->log(mod, "Timed out waiting for the FH3 game code.");
        return 0;
    }

    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(game);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(game + dos->e_lfanew);
    if (nt->FileHeader.TimeDateStamp != 0x5B16ECCA ||
        nt->OptionalHeader.SizeOfImage < kRenderSnapshotCall + 5) {
        api->log(mod, "Unsupported FH3 executable. Expected the June 5, 2018 build.");
        return 0;
    }

    const HookSite damage{"CarPresentation::ApplyDamage", 0x1E33770,
        "\x48\x8B\xC4\x48\x89\x58\x10\x48\x89\x70\x18\x55", 12,
        reinterpret_cast<void*>(applyDamage), &originalApplyDamage};
    const HookSite detach{"CarPresentation::DetachPart", 0x1E3CF60,
        "\x48\x8B\x89\xA8\x07\x00\x00\xE9\xD4\x3C\xDB\xFF", 12,
        reinterpret_cast<void*>(detachPart), &originalDetachPart};

    api->log(mod, installPair(game, damage, detach)
        ? "Visual damage hooks installed." : "Visual damage hook validation or installation failed.");

    const HookSite render{"CarPresentation render snapshot", kRenderSnapshotCall,
        "\xE8\xE2\xB1\xFF\xFF", 5,
        reinterpret_cast<void*>(updateRenderSnapshot), &originalUpdateRenderSnapshot};
    auto* cleanHook = matches(game, render)
        ? api->hook_call(mod, render.name, game + render.rva, render.detour, render.original)
        : nullptr;
    api->log(mod, cleanHook && api->enable_hooks(mod, cleanHook)
        ? "Dirt and mud render hook installed." : "Dirt and mud render hook validation or installation failed.");
    return 0;
}

void changeOption(void* user, const char*, int value) {
    auto& option = *static_cast<Option*>(user);
    option.enabled.store(value != 0);
    if (!WritePrivateProfileStringW(kSection, option.iniKey, value ? L"1" : L"0", iniPath))
        api->log(mod, "Could not save the setting to FH3_FreshOuttaFactory.ini.");
}

void addOption(Option& option) {
    const bool enabled = GetPrivateProfileIntW(kSection, option.iniKey, option.defaultValue, iniPath) != 0;
    option.enabled.store(enabled);
    FH3Setting setting{};
    setting.size = sizeof setting;
    setting.key = option.key;
    setting.label = option.label;
    setting.description = option.description;
    setting.value = enabled;
    setting.on_change = changeOption;
    setting.user = &option;
    if (!api->add_setting(mod, &setting))
        api->log(mod, "Could not register a FreshOuttaFactory setting.");
}
}

FH3MOD_EXPORT int fh3mod_init(const FH3ModApi* platform) {
    if (!platform || platform->api_version < FH3MOD_API_VERSION)
        return 0;
    api = platform;

    FH3ModInfo info{};
    info.size = sizeof info;
    info.name = "FH3_FreshOuttaFactory";
    info.version = MOD_VERSION;
    info.github_repo = "suderek22/FH3_FreshOuttaFactory";
    info.platform_version = kPlatformVersion;
    info.check_updates = 1;
    info.description = "Suppresses visual crash damage on all cars and can keep them free of dirt and mud.";
    mod = api->register_mod(&info);
    if (!mod)
        return 0;

    const int length = swprintf_s(iniPath, L"%s\\fh3-mods\\FH3_FreshOuttaFactory.ini", api->azimuth_dir);
    if (length < 0) {
        api->log(mod, "Could not construct the path to FH3_FreshOuttaFactory.ini.");
        return 0;
    }

    addOption(noDamage);
    addOption(noDirt);
    if (HANDLE worker = CreateThread(nullptr, 0, installHooks, nullptr, 0, nullptr))
        CloseHandle(worker);
    else
        api->log(mod, "Could not start the game hook worker.");
    return 0;
}

