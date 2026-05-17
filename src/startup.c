/**
 * @file startup.c
 * @brief Windows startup integration with table-driven mode configs
 */
#include "startup.h"
#include "config.h"
#include "timer/timer.h"
#include "timer/main_timer.h"
#include "timer/timer_events.h"
#include "log.h"
#include <stddef.h>
#include <windows.h>
#include <shlobj.h>
#include <objbase.h>
#include <shobjidl.h>
#include <shlguid.h>
#include <shlwapi.h>
#include <strsafe.h>
#include <stdio.h>
#include <string.h>

#define STARTUP_LINK_FILENAME L"Catime.lnk"
#define STARTUP_CMD_ARG L"--startup"
#define CONFIG_KEY_STARTUP_MODE "STARTUP_MODE="

#define MODE_NAME_COUNT_UP "COUNT_UP"
#define MODE_NAME_SHOW_TIME "SHOW_TIME"
#define MODE_NAME_NO_DISPLAY "NO_DISPLAY"
#define MODE_NAME_DEFAULT "DEFAULT"

typedef struct {
    IShellLinkW* shellLink;
    IPersistFile* persistFile;
    bool initialized;
} ComShellLink;

/** Table-driven design eliminates if-else chains */
typedef struct {
    const char* modeName;
    bool showCurrentTime;
    bool countUp;
    bool enableTimer;
    int32_t totalTime;
    const char* description;
} StartupModeConfig;

static const StartupModeConfig STARTUP_MODE_CONFIGS[] = {
    {MODE_NAME_COUNT_UP, false, true, true, 0, "Count-up timer from zero"},
    {MODE_NAME_SHOW_TIME, true, false, true, 0, "Display current system time"},
    {MODE_NAME_NO_DISPLAY, false, false, false, 0,
     "Hidden mode, no timer display"},
    {MODE_NAME_DEFAULT, false, false, true, -1,
     "Standard countdown timer with default time"},
};

static constexpr size_t STARTUP_MODE_COUNT =
    sizeof(STARTUP_MODE_CONFIGS) / sizeof(STARTUP_MODE_CONFIGS[0]);

/** PathCombineW prevents buffer overflows vs sprintf */
static bool GetStartupShortcutPath(wchar_t* output, size_t outputSize) {
    wchar_t startupFolder[MAX_PATH];

    UNREFERENCED_PARAMETER(outputSize);

    HRESULT hr = SHGetFolderPathW(NULL, CSIDL_STARTUP, NULL, 0, startupFolder);
    if (FAILED(hr)) {
        LOG_ERROR("Failed to get startup folder path, hr=0x%08X",
                  (unsigned int)hr);
        return false;
    }

    if (!PathCombineW(output, startupFolder, STARTUP_LINK_FILENAME)) {
        LOG_ERROR("Failed to combine startup path");
        return false;
    }

    return true;
}

static bool GetExecutablePath(wchar_t* output, size_t outputSize) {
    DWORD result = GetModuleFileNameW(NULL, output, (DWORD)outputSize);
    if (result == 0 || result >= outputSize) {
        LOG_ERROR("Failed to get executable path");
        return false;
    }
    return true;
}

static bool InitComShellLink(ComShellLink* link) {
    *link = (ComShellLink){
        .shellLink = nullptr,
        .persistFile = nullptr,
        .initialized = false,
    };

    HRESULT hr = CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER,
                                  &IID_IShellLinkW, (void**)&link->shellLink);
    if (FAILED(hr)) {
        LOG_ERROR("Failed to create IShellLink interface, hr=0x%08X",
                  (unsigned int)hr);
        return false;
    }

    hr = link->shellLink->lpVtbl->QueryInterface(
        link->shellLink, &IID_IPersistFile, (void**)&link->persistFile);
    if (FAILED(hr)) {
        LOG_ERROR("Failed to get IPersistFile interface, hr=0x%08X",
                  (unsigned int)hr);
        link->shellLink->lpVtbl->Release(link->shellLink);
        link->shellLink = nullptr;
        return false;
    }

    link->initialized = true;
    return true;
}

static void CleanupComShellLink(ComShellLink* link) {
    if (link->persistFile) {
        link->persistFile->lpVtbl->Release(link->persistFile);
        link->persistFile = nullptr;
    }
    if (link->shellLink) {
        link->shellLink->lpVtbl->Release(link->shellLink);
        link->shellLink = nullptr;
    }
    link->initialized = false;
}

/** Centralizes timer reset to avoid repetition - uses high-precision timer */
static void RestartTimer(HWND hwnd, UINT interval) {
    MainTimer_Start(hwnd, interval);
}

static void StopTimer([[maybe_unused]] HWND hwnd) { MainTimer_Stop(); }

static bool ReadStartupModeConfig(char* modeName, size_t modeNameSize) {
    char configPath[MAX_PATH];
    wchar_t wconfigPath[MAX_PATH];
    constexpr size_t CONFIG_LINE_BUFFER_SIZE = 256;
    char line[CONFIG_LINE_BUFFER_SIZE];
    UNREFERENCED_PARAMETER(modeNameSize);

    GetConfigPath(configPath, MAX_PATH);

    MultiByteToWideChar(CP_UTF8, 0, configPath, -1, wconfigPath, MAX_PATH);

    FILE* configFile = _wfopen(wconfigPath, L"r");
    if (!configFile) {
        LOG_WARNING("Failed to open config file for reading startup mode");
        return false;
    }

    bool found = false;
    while (fgets(line, sizeof(line), configFile)) {
        if (strncmp(line, CONFIG_KEY_STARTUP_MODE,
                    strlen(CONFIG_KEY_STARTUP_MODE)) != 0) {
            continue;
        }

        if (sscanf(line, "STARTUP_MODE=%19s", modeName) == 1) {
            found = true;
            LOG_INFO("Read startup mode from config: %s", modeName);
            break;
        }
    }

    fclose(configFile);
    return found;
}

/** Unified mode application via data-driven config */
static void ApplyModeConfig(HWND hwnd, const StartupModeConfig* config) {
    LOG_INFO("Applying startup mode: %s - %s", config->modeName,
             config->description);

    CLOCK_SHOW_CURRENT_TIME = config->showCurrentTime;
    CLOCK_COUNT_UP = config->countUp;
    CLOCK_TOTAL_TIME = (config->totalTime == -1)
                           ? g_AppConfig.timer.default_start_time
                           : config->totalTime;
    countdown_elapsed_time = 0;
    countup_elapsed_time = 0;

    /* Initialize absolute time references for timer calculation */
    int64_t now = GetAbsoluteTimeMs();

    if (config->countUp) {
        g_start_time = now;
    } else if (CLOCK_TOTAL_TIME > 0) {
        g_target_end_time = now + ((int64_t)CLOCK_TOTAL_TIME * 1000);
    }

    ResetMillisecondAccumulator();

    if (config->enableTimer) {
        RestartTimer(hwnd, GetTimerInterval());
    } else {
        StopTimer(hwnd);
    }
}

static const StartupModeConfig* FindModeConfig(const char* modeName) {
    for (size_t i = 0; i < STARTUP_MODE_COUNT; i++) {
        if (strcmp(modeName, STARTUP_MODE_CONFIGS[i].modeName) == 0) {
            return &STARTUP_MODE_CONFIGS[i];
        }
    }
    return NULL;
}

static const StartupModeConfig* GetDefaultModeConfig(void) {
    for (size_t i = 0; i < STARTUP_MODE_COUNT; i++) {
        if (strcmp(STARTUP_MODE_CONFIGS[i].modeName, MODE_NAME_DEFAULT) == 0) {
            return &STARTUP_MODE_CONFIGS[i];
        }
    }
    return &STARTUP_MODE_CONFIGS[0];
}

BOOL IsAutoStartEnabled(void) {
    wchar_t startupPath[MAX_PATH];

    if (!GetStartupShortcutPath(startupPath, MAX_PATH)) {
        return FALSE;
    }

    BOOL exists = (GetFileAttributesW(startupPath) != INVALID_FILE_ATTRIBUTES);
    LOG_INFO("Startup shortcut %s", exists ? "exists" : "does not exist");

    return exists;
}

/** --startup argument enables startup behavior detection */
bool CreateShortcut(void) {
    ComShellLink link;
    wchar_t startupPath[MAX_PATH];
    wchar_t exePath[MAX_PATH];
    bool success = false;

    LOG_INFO("Creating startup shortcut");

    if (!GetExecutablePath(exePath, MAX_PATH)) {
        return false;
    }

    if (!GetStartupShortcutPath(startupPath, MAX_PATH)) {
        return false;
    }

    if (!InitComShellLink(&link)) {
        return false;
    }

    HRESULT hr = link.shellLink->lpVtbl->SetPath(link.shellLink, exePath);
    if (FAILED(hr)) {
        LOG_ERROR("Failed to set shortcut path, hr=0x%08X", (unsigned int)hr);
        CleanupComShellLink(&link);
        return false;
    }

    hr = link.shellLink->lpVtbl->SetArguments(link.shellLink, STARTUP_CMD_ARG);
    if (FAILED(hr)) {
        LOG_WARNING("Failed to set shortcut arguments, hr=0x%08X",
                    (unsigned int)hr);
    }

    hr = link.persistFile->lpVtbl->Save(link.persistFile, startupPath, TRUE);
    if (SUCCEEDED(hr)) {
        LOG_INFO("Startup shortcut created successfully: %ls", startupPath);
        success = true;
    } else {
        LOG_ERROR("Failed to save shortcut, hr=0x%08X", (unsigned int)hr);
    }

    CleanupComShellLink(&link);
    return success;
}

bool RemoveShortcut(void) {
    wchar_t startupPath[MAX_PATH];

    LOG_INFO("Removing startup shortcut");

    if (!GetStartupShortcutPath(startupPath, MAX_PATH)) {
        return false;
    }

    if (DeleteFileW(startupPath)) {
        LOG_INFO("Startup shortcut removed successfully");
        return true;
    }

    DWORD error = GetLastError();
    if (error == ERROR_FILE_NOT_FOUND) {
        LOG_INFO("Startup shortcut does not exist, nothing to remove");
        return true;
    }
    LOG_ERROR("Failed to delete startup shortcut, error=%lu", error);
    return false;
}

/** Recreates shortcut to handle app relocations */
bool UpdateStartupShortcut(void) {
    LOG_INFO("Updating startup shortcut if exists");

    if (!IsAutoStartEnabled()) {
        LOG_INFO("No startup shortcut to update");
        return true;
    }
    if (!RemoveShortcut()) {
        LOG_ERROR("Failed to remove old startup shortcut");
        return false;
    }

    if (!CreateShortcut()) {
        LOG_ERROR("Failed to recreate startup shortcut");
        return false;
    }

    LOG_INFO("Startup shortcut updated successfully");
    return true;
}

/** Reads STARTUP_MODE from config, falls back to DEFAULT if not found */
void ApplyStartupMode(HWND hwnd) {
    constexpr size_t STARTUP_MODE_MAX_LEN = 20;
    char modeName[STARTUP_MODE_MAX_LEN] = {};
    const StartupModeConfig* config = nullptr;

    LOG_INFO("Applying startup mode configuration");

    if (ReadStartupModeConfig(modeName, sizeof(modeName))) {
        config = FindModeConfig(modeName);
        if (!config) {
            LOG_WARNING("Unknown startup mode '%s', using default", modeName);
            config = GetDefaultModeConfig();
        }
    } else {
        LOG_INFO("No startup mode configured, using default");
        config = GetDefaultModeConfig();
    }

    ApplyModeConfig(hwnd, config);
    InvalidateRect(hwnd, nullptr, true);
}
