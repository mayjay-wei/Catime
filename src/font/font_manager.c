/**
 * @file font_manager.c
 * @brief Font loading and management implementation
 */

#include "font/font_manager.h"
#include "font/font_ttf_parser.h"
#include "font/font_path_manager.h"
#include "font/font_config.h"
#include "utils/string_convert.h"
#include "utils/path_utils.h"
#include "config.h"
#include "log.h"
#include "../../resource/resource.h"
#include <stdio.h>
#include <string.h>

/* ============================================================================
 * Global State
 * ============================================================================
 */

char FONT_FILE_NAME[MAX_PATH] = FONT_FOLDER_PREFIX "Wallpoet Essence.ttf";
char FONT_INTERNAL_NAME[MAX_PATH];
char PREVIEW_FONT_NAME[MAX_PATH] = "";
char PREVIEW_INTERNAL_NAME[MAX_PATH] = "";
bool IS_PREVIEWING = false;

static wchar_t CURRENT_LOADED_FONT_PATH[MAX_PATH] = {0};
static bool FONT_RESOURCE_LOADED = false;

/* ============================================================================
 * Embedded Font Resources
 * ============================================================================
 */

FontResource fontResources[] = {
    {IDR_FONT_RECMONO, "RecMonoCasual Nerd Font Mono Essence.ttf"},
    {IDR_FONT_DEPARTURE, "DepartureMono Nerd Font Propo Essence.ttf"},
    {IDR_FONT_TERMINESS, "Terminess Nerd Font Propo Essence.ttf"},
    {IDR_FONT_JACQUARD, "Jacquard 12 Essence.ttf"},
    {IDR_FONT_JACQUARDA, "Jacquarda Bastarda 9 Essence.ttf"},
    {IDR_FONT_PIXELIFY, "Pixelify Sans Medium Essence.ttf"},
    {IDR_FONT_RUBIK_BURNED, "Rubik Burned Essence.ttf"},
    {IDR_FONT_RUBIK_GLITCH, "Rubik Glitch Essence.ttf"},
    {IDR_FONT_RUBIK_MARKER_HATCH, "Rubik Marker Hatch Essence.ttf"},
    {IDR_FONT_RUBIK_PUDDLES, "Rubik Puddles Essence.ttf"},
    {IDR_FONT_WALLPOET, "Wallpoet Essence.ttf"},
    {IDR_FONT_PROFONT, "ProFont IIx Nerd Font Essence.ttf"},
    {IDR_FONT_DADDYTIME, "DaddyTimeMono Nerd Font Propo Essence.ttf"},
};

const int32_t FONT_RESOURCES_COUNT =
    sizeof(fontResources) / sizeof(FontResource);

/* ============================================================================
 * Font Resource Management
 * ============================================================================
 */

bool UnloadCurrentFontResource(void) {
    if (!FONT_RESOURCE_LOADED || CURRENT_LOADED_FONT_PATH[0] == 0) {
        return true;
    }

    bool result =
        RemoveFontResourceExW(CURRENT_LOADED_FONT_PATH, FR_PRIVATE, nullptr);
    CURRENT_LOADED_FONT_PATH[0] = 0;
    FONT_RESOURCE_LOADED = false;
    return result;
}

bool LoadFontFromFile(const char* fontFilePath) {
    if (!fontFilePath)
        return false;

    /* Convert to wide */
    wchar_t wFontPath[MAX_PATH];
    if (!Utf8ToWide(fontFilePath, wFontPath, MAX_PATH))
        return false;

    /* Check if file exists */
    if (GetFileAttributesW(wFontPath) == INVALID_FILE_ATTRIBUTES) {
        return false;
    }

    /* Skip if already loaded */
    if (FONT_RESOURCE_LOADED &&
        wcscmp(CURRENT_LOADED_FONT_PATH, wFontPath) == 0) {
        return true;
    }

    /* Load new font */
    int addResult = AddFontResourceExW(wFontPath, FR_PRIVATE, nullptr);
    if (addResult <= 0) {
        return false;
    }

    /* Unload previous font if different */
    if (FONT_RESOURCE_LOADED && CURRENT_LOADED_FONT_PATH[0] != 0 &&
        wcscmp(CURRENT_LOADED_FONT_PATH, wFontPath) != 0) {
        RemoveFontResourceExW(CURRENT_LOADED_FONT_PATH, FR_PRIVATE, nullptr);
    }

    /* Save current loaded font */
    wcscpy_s(CURRENT_LOADED_FONT_PATH, MAX_PATH, wFontPath);
    FONT_RESOURCE_LOADED = true;
    return true;
}

/* ============================================================================
 * High-Level Font Loading (with auto-recovery)
 * ============================================================================
 */

/**
 * @brief Internal font loading with optional config update
 * @param fontFileName Font filename
 * @param shouldUpdateConfig true to update config if auto-fixed
 * @return true on success
 */
static bool LoadFontInternal(const char* fontFileName,
                             bool shouldUpdateConfig) {
    if (!fontFileName)
        return false;

    /* Try direct load */
    char fontPath[MAX_PATH];
    if (!BuildFullFontPath(fontFileName, fontPath, MAX_PATH))
        return false;

    if (LoadFontFromFile(fontPath)) {
        return true;
    }

    /* Direct load failed: try auto-fix */
    FontPathInfo pathInfo;
    if (!AutoFixFontPath(fontFileName, &pathInfo)) {
        return false;
    }

    /* Update config if requested and this is the active font */
    if (!shouldUpdateConfig || !IsFontsFolderPath(FONT_FILE_NAME)) {
        return LoadFontFromFile(pathInfo.absolutePath);
    }
    const char* currentRelative = ExtractRelativePath(FONT_FILE_NAME);
    if (currentRelative && strcmp(currentRelative, fontFileName) == 0) {
        /* Update global FONT_FILE_NAME */
        strncpy(FONT_FILE_NAME, pathInfo.configPath,
                sizeof(FONT_FILE_NAME) - 1);
        FONT_FILE_NAME[sizeof(FONT_FILE_NAME) - 1] = '\0';

        /* Write to config */
        WriteConfigFont(pathInfo.relativePath, false);
        FlushConfigToDisk();
    }
    return LoadFontFromFile(pathInfo.absolutePath);
}

bool LoadFontByName([[maybe_unused]] HINSTANCE hInstance,
                    const char* fontName) {
    return LoadFontInternal(fontName, true);
}

bool LoadFontByNameAndGetRealName([[maybe_unused]] HINSTANCE hInstance,
                                  const char* fontFileName, char* realFontName,
                                  size_t realFontNameSize) {
    if (!fontFileName || !realFontName || realFontNameSize == 0)
        return false;

    /* Build full path */
    char fontPath[MAX_PATH];
    if (!BuildFullFontPath(fontFileName, fontPath, MAX_PATH))
        return false;

    /* Check if file exists */
    wchar_t wFontPath[MAX_PATH];
    if (Utf8ToWide(fontPath, wFontPath, MAX_PATH)) {
        /* If not exists, try auto-fix */
        if (GetFileAttributesW(wFontPath) == INVALID_FILE_ATTRIBUTES) {
            FontPathInfo pathInfo;
            if (!AutoFixFontPath(fontFileName, &pathInfo)) {
                return false;
            }
            strncpy(fontPath, pathInfo.absolutePath, MAX_PATH - 1);
            fontPath[MAX_PATH - 1] = '\0';

            /* Update config if this is the active font */
            if (IsFontsFolderPath(FONT_FILE_NAME)) {
                const char* currentRelative =
                    ExtractRelativePath(FONT_FILE_NAME);
                if (currentRelative &&
                    strcmp(currentRelative, fontFileName) == 0) {
                    strncpy(FONT_FILE_NAME, pathInfo.configPath,
                            sizeof(FONT_FILE_NAME) - 1);
                    FONT_FILE_NAME[sizeof(FONT_FILE_NAME) - 1] = '\0';

                    WriteConfigFont(pathInfo.relativePath, false);
                    FlushConfigToDisk();
                }
            }
        }
    }

    /* Extract font name from TTF */
    if (!GetFontNameFromFile(fontPath, realFontName, realFontNameSize)) {
        /* Fallback: use filename without extension */
        const char* filename = GetFileNameU8(fontFileName);
        strncpy(realFontName, filename, realFontNameSize - 1);
        realFontName[realFontNameSize - 1] = '\0';

        /* Remove extension */
        char* dot = strrchr(realFontName, '.');
        if (dot)
            *dot = '\0';
    }

    /* Load font */
    return LoadFontFromFile(fontPath);
}

bool SwitchFont(HINSTANCE hInstance, const char* fontName) {
    if (!fontName)
        return false;

    /* Update active font filename */
    strncpy(FONT_FILE_NAME, fontName, sizeof(FONT_FILE_NAME) - 1);
    FONT_FILE_NAME[sizeof(FONT_FILE_NAME) - 1] = '\0';

    /* Load and extract internal name */
    if (!LoadFontByNameAndGetRealName(hInstance, fontName, FONT_INTERNAL_NAME,
                                      sizeof(FONT_INTERNAL_NAME))) {
        return false;
    }

    /* Write to config (without reload) */
    WriteConfigFont(FONT_FILE_NAME, false);

    return true;
}

/* ============================================================================
 * Preview System
 * ============================================================================
 */

bool PreviewFont(HINSTANCE hInstance, const char* fontName) {
    if (!fontName)
        return false;

    /* Save preview font name */
    strncpy(PREVIEW_FONT_NAME, fontName, sizeof(PREVIEW_FONT_NAME) - 1);
    PREVIEW_FONT_NAME[sizeof(PREVIEW_FONT_NAME) - 1] = '\0';

    /* Load and extract internal name */
    if (!LoadFontByNameAndGetRealName(hInstance, fontName,
                                      PREVIEW_INTERNAL_NAME,
                                      sizeof(PREVIEW_INTERNAL_NAME))) {
        return false;
    }

    /* Set preview mode */
    IS_PREVIEWING = true;
    return true;
}

void CancelFontPreview(void) {
    /* Clear preview mode */
    IS_PREVIEWING = false;
    PREVIEW_FONT_NAME[0] = '\0';
    PREVIEW_INTERNAL_NAME[0] = '\0';

    /* Reload original font */
    HINSTANCE hInstance = GetModuleHandle(nullptr);

    if (IsFontsFolderPath(FONT_FILE_NAME)) {
        const char* relativePath = ExtractRelativePath(FONT_FILE_NAME);
        if (relativePath) {
            LoadFontByNameAndGetRealName(hInstance, relativePath,
                                         FONT_INTERNAL_NAME,
                                         sizeof(FONT_INTERNAL_NAME));
        }
    } else if (FONT_FILE_NAME[0] != '\0') {
        LoadFontByNameAndGetRealName(hInstance, FONT_FILE_NAME,
                                     FONT_INTERNAL_NAME,
                                     sizeof(FONT_INTERNAL_NAME));
    }
}

void ApplyFontPreview(void) {
    if (!IS_PREVIEWING || strlen(PREVIEW_FONT_NAME) == 0)
        return;

    /* Commit preview to active font */
    strncpy(FONT_FILE_NAME, PREVIEW_FONT_NAME, sizeof(FONT_FILE_NAME) - 1);
    FONT_FILE_NAME[sizeof(FONT_FILE_NAME) - 1] = '\0';

    strncpy(FONT_INTERNAL_NAME, PREVIEW_INTERNAL_NAME,
            sizeof(FONT_INTERNAL_NAME) - 1);
    FONT_INTERNAL_NAME[sizeof(FONT_INTERNAL_NAME) - 1] = '\0';

    /* Write to config */
    WriteConfigFont(FONT_FILE_NAME, false);

    /* Clear preview state */
    CancelFontPreview();
}

/* ============================================================================
 * Embedded Font Resources
 * ============================================================================
 */

bool ExtractFontResourceToFile(HINSTANCE hInstance, int resourceId,
                               const char* outputPath) {
    if (!outputPath)
        return false;

    /* Find resource */
    HRSRC hResource =
        FindResourceW(hInstance, MAKEINTRESOURCE(resourceId), RT_FONT);
    if (hResource == nullptr)
        return false;

    /* Load resource */
    HGLOBAL hMemory = LoadResource(hInstance, hResource);
    if (hMemory == nullptr)
        return false;

    /* Lock resource */
    const void* fontData = LockResource(hMemory);
    if (fontData == nullptr)
        return false;

    /* Get size */
    DWORD fontLength = SizeofResource(hInstance, hResource);
    if (fontLength == 0)
        return false;

    /* Convert path to wide */
    wchar_t wOutputPath[MAX_PATH];
    if (!Utf8ToWide(outputPath, wOutputPath, MAX_PATH))
        return false;

    /* Write to file */
    HANDLE hFile = CreateFileW(wOutputPath, GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
        return false;

    DWORD bytesWritten;
    bool result =
        WriteFile(hFile, fontData, fontLength, &bytesWritten, nullptr);
    CloseHandle(hFile);

    return (result && bytesWritten == fontLength);
}

bool ExtractEmbeddedFontsToFolder(HINSTANCE hInstance) {
    /* Get fonts folder path */
    wchar_t wFontsFolderPath[MAX_PATH] = {0};
    if (!GetFontsFolderW(wFontsFolderPath, MAX_PATH, true))
        return false;

    char fontsFolderPath[MAX_PATH];
    if (!WideToUtf8(wFontsFolderPath, fontsFolderPath, MAX_PATH))
        return false;

    /* Extract each font */
    for (int i = 0; i < FONT_RESOURCES_COUNT; i++) {
        char outputPath[MAX_PATH];
        int outputPathLen =
            snprintf(outputPath, MAX_PATH, "%s\\%s", fontsFolderPath,
                     fontResources[i].fontName);
        if (outputPathLen < 0 || outputPathLen >= MAX_PATH) {
            LOG_WARNING("Font output path too long: %s",
                        fontResources[i].fontName);
            continue;
        }
        ExtractFontResourceToFile(hInstance, fontResources[i].resourceId,
                                  outputPath);
    }

    return true;
}

/* ============================================================================
 * Utility Functions
 * ============================================================================
 */

void ListAvailableFonts(void) {
    HDC hdc = GetDC(nullptr);
    if (!hdc)
        return;
    LOGFONT lf;
    memset(&lf, 0, sizeof(LOGFONT));
    lf.lfCharSet = DEFAULT_CHARSET;

    HFONT hFont =
        CreateFontW(12, 0, 0, 0, FW_NORMAL, false, false, false, lf.lfCharSet,
                    OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                    ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, nullptr);
    SelectObject(hdc, hFont);

    EnumFontFamiliesExW(hdc, &lf, (FONTENUMPROCW)EnumFontFamExProc, 0, 0);

    DeleteObject(hFont);
    ReleaseDC(nullptr, hdc);
}

int CALLBACK EnumFontFamExProc([[maybe_unused]] ENUMLOGFONTEXW* lpelfe,
                               [[maybe_unused]] NEWTEXTMETRICEX* lpntme,
                               [[maybe_unused]] DWORD FontType,
                               [[maybe_unused]] LPARAM lParam) {
    return 1;
}
