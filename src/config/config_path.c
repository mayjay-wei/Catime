/**
 * @file config_path.c
 * @brief Configuration path and resource folder management
 *
 * Manages configuration file paths, resource folder creation, and first-run
 * detection.
 */
#include "config.h"
#include "utils/string_convert.h"
#include "utils/path_utils.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <shlobj.h>
#include <objbase.h>

#define UTF8_TO_WIDE(utf8, wide)  \
    wchar_t wide[MAX_PATH] = {0}; \
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide, MAX_PATH)

/**
 * @brief Get configuration file path with automatic directory creation
 */
void GetConfigPath(char* path, size_t size) {
    if (!path || size == 0)
        return;

    /* Prefer modern Known Folder API to obtain a wide-character LocalAppData
     * path */
    PWSTR wLocalAppData = nullptr;
    HRESULT hr = S_OK;

    /* SHGetKnownFolderPath is available on Vista+, fallback to SHGetFolderPathW
     * if needed */
    HMODULE hShell = LoadLibraryW(L"shell32.dll");
    if (hShell) {
        typedef HRESULT(WINAPI * PFN_SHGetKnownFolderPath)(const GUID*, DWORD,
                                                           HANDLE, PWSTR*);
        PFN_SHGetKnownFolderPath pfn = (PFN_SHGetKnownFolderPath)GetProcAddress(
            hShell, "SHGetKnownFolderPath");
        if (pfn) {
            /* FOLDERID_LocalAppData */
            static constexpr GUID folderIdLocalAppDataGuid = {
                0xF1B3'2785,
                0x6FBA,
                0x4FCF,
                {0x9D, 0x55, 0x7B, 0x8E, 0x7F, 0x15, 0x70, 0x91}};
            hr = pfn(&folderIdLocalAppDataGuid, 0, nullptr, &wLocalAppData);
        } else {
            hr = E_NOTIMPL;
        }
        FreeLibrary(hShell);
    } else {
        hr = E_FAIL;
    }

    wchar_t wConfigPath[MAX_PATH] = {};
    if (SUCCEEDED(hr) && wLocalAppData && wcslen(wLocalAppData) > 0) {
        /* Build %LOCALAPPDATA%\Catime and ensure directory exists */
        wchar_t wDir[MAX_PATH] = {};
        _snwprintf_s(wDir, MAX_PATH, _TRUNCATE, L"%s\\Catime", wLocalAppData);
        CreateDirectoryW(wDir, nullptr); /* Ignore errors if directory exists */

        _snwprintf_s(wConfigPath, MAX_PATH, _TRUNCATE,
                     L"%s\\Catime\\config.ini", wLocalAppData);
        CoTaskMemFree(wLocalAppData);

        /* Convert wide path to UTF-8 for the rest of the app */
        WideCharToMultiByte(CP_UTF8, 0, wConfigPath, -1, path, (int)size,
                            nullptr, NULL);
        return;
    }

    /* Fallback to legacy SHGetFolderPathW(CSIDL_LOCAL_APPDATA) */
    wchar_t wLegacy[MAX_PATH] = {};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0,
                                   wLegacy))) {
        wchar_t wDir[MAX_PATH] = {};
        _snwprintf_s(wDir, MAX_PATH, _TRUNCATE, L"%s\\Catime", wLegacy);
        CreateDirectoryW(wDir, nullptr); /* Ignore errors if directory exists */

        _snwprintf_s(wConfigPath, MAX_PATH, _TRUNCATE,
                     L"%s\\Catime\\config.ini", wLegacy);
        WideCharToMultiByte(CP_UTF8, 0, wConfigPath, -1, path, (int)size,
                            nullptr, NULL);
        return;
    }

    /* Final fallback: manual path construction using %USERPROFILE% */
    wchar_t wUserProfile[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"USERPROFILE", wUserProfile, MAX_PATH) > 0) {
        wchar_t wDir[MAX_PATH] = {};
        _snwprintf_s(wDir, MAX_PATH, _TRUNCATE, L"%s\\AppData\\Local\\Catime",
                     wUserProfile);
        CreateDirectoryW(wDir, nullptr); /* Ignore errors if directory exists */

        _snwprintf_s(wConfigPath, MAX_PATH, _TRUNCATE,
                     L"%s\\AppData\\Local\\Catime\\config.ini", wUserProfile);
        WideCharToMultiByte(CP_UTF8, 0, wConfigPath, -1, path, (int)size,
                            nullptr, NULL);
        return;
    }

    /* Critical failure: cannot determine config path */
    LOG_ERROR(
        "Failed to determine configuration path - all methods failed "
        "(SHGetKnownFolderPath, SHGetFolderPathW, USERPROFILE)");
    strncpy(path, "", size - 1);
    path[size - 1] = '\0';
}

/**
 * @brief Build full path to a resources subfolder and ensure it exists
 */
static void GetResourceSubfolderPathUtf8(const wchar_t* wSubFolder,
                                         char* outPathUtf8, size_t outSize) {
    char configPathUtf8[MAX_PATH] = {};
    wchar_t wConfigPath[MAX_PATH] = {};
    GetConfigPath(configPathUtf8, MAX_PATH);
    MultiByteToWideChar(CP_UTF8, 0, configPathUtf8, -1, wConfigPath, MAX_PATH);

    /** Trim trailing file name */
    wchar_t* lastSep = wcsrchr(wConfigPath, L'\\');
    if (!lastSep) {
        if (outPathUtf8 && outSize > 0) {
            strncpy(outPathUtf8, ".\\", outSize - 1);
            outPathUtf8[outSize - 1] = '\0';
        }
        return;
    }
    *lastSep = L'\0';

    wchar_t wFolder[MAX_PATH] = {};
    _snwprintf_s(wFolder, MAX_PATH, _TRUNCATE, L"%s\\%s", wConfigPath,
                 wSubFolder);

    /** Ensure directory exists (creates intermediate directories) */
    SHCreateDirectoryExW(nullptr, wFolder, nullptr);

    if (outPathUtf8 && outSize > 0) {
        WideCharToMultiByte(CP_UTF8, 0, wFolder, -1, outPathUtf8, (int)outSize,
                            nullptr, nullptr);
    }
}

/**
 * @brief Ensure default resources subfolder structure exists
 */
static void EnsureDefaultResourceSubfolders(void) {
    const wchar_t* subfolders[] = {
        L"resources", L"resources\\audio", L"resources\\fonts",
        L"resources\\animations", L"resources\\plugins"};
    for (size_t i = 0; i < sizeof(subfolders) / sizeof(subfolders[0]); ++i) {
        GetResourceSubfolderPathUtf8(subfolders[i], nullptr, 0);
    }
}

/**
 * @brief Extract filename from full path with UTF-8 support
 */
void ExtractFileName(const char* path, char* name, size_t nameSize) {
    ExtractFileNameU8(path, name, nameSize);
}

/**
 * @brief Create resource folder structure in config directory
 */
void CheckAndCreateResourceFolders() {
    char config_path[MAX_PATH];
    char base_path[MAX_PATH];
    char* last_slash;

    /** Get base directory from config path */
    GetConfigPath(config_path, MAX_PATH);

    /** Extract directory portion */
    strncpy(base_path, config_path, MAX_PATH - 1);
    base_path[MAX_PATH - 1] = '\0';

    last_slash = strrchr(base_path, '\\');
    if (!last_slash) {
        last_slash = strrchr(base_path, '/');
    }

    if (last_slash) {
        *(last_slash + 1) = '\0';

        /** Unified creation via helper */
        EnsureDefaultResourceSubfolders();
    }
}

/**
 * @brief Check if this is the first run of the application
 */
bool IsFirstRun(void) {
    char config_path[MAX_PATH];
    GetConfigPath(config_path, MAX_PATH);

    if (!FileExists(config_path)) {
        return true;
    }

    char firstRun[32] = {};
    ReadIniString(INI_SECTION_GENERAL, "FIRST_RUN", "TRUE", firstRun,
                  sizeof(firstRun), config_path);

    return (strcmp(firstRun, "TRUE") == 0);
}

/**
 * @brief Set first run flag to FALSE
 */
void SetFirstRunCompleted(void) {
    char config_path[MAX_PATH];
    GetConfigPath(config_path, MAX_PATH);

    WriteIniString(INI_SECTION_GENERAL, "FIRST_RUN", "FALSE", config_path);
}

/**
 * @brief Get audio resources folder path with automatic directory creation
 */
void GetAudioFolderPath(char* path, size_t size) {
    if (!path || size == 0)
        return;
    GetResourceSubfolderPathUtf8(L"resources\\audio", path, size);
}

/**
 * @brief Get animations resources folder path and ensure it exists
 */
void GetAnimationsFolderPath(char* path, size_t size) {
    if (!path || size == 0)
        return;
    GetResourceSubfolderPathUtf8(L"resources\\animations", path, size);
}

/**
 * @brief Get plugins resources folder path and ensure it exists
 */
void GetPluginsFolderPath(char* path, size_t size) {
    if (!path || size == 0)
        return;
    GetResourceSubfolderPathUtf8(L"resources\\plugins", path, size);
}

/**
 * @brief Check if desktop shortcut verification has been completed
 */
bool IsShortcutCheckDone(void) {
    char config_path[MAX_PATH];
    GetConfigPath(config_path, MAX_PATH);

    /** Read shortcut check status from general section */
    return ReadIniBool(INI_SECTION_GENERAL, "SHORTCUT_CHECK_DONE", false,
                       config_path);
}

/**
 * @brief Mark desktop shortcut verification as completed
 */
void SetShortcutCheckDone(bool done) {
    char config_path[MAX_PATH];
    GetConfigPath(config_path, MAX_PATH);

    /** Write shortcut check status to general section */
    WriteIniString(INI_SECTION_GENERAL, "SHORTCUT_CHECK_DONE",
                   done ? "TRUE" : "FALSE", config_path);
}
