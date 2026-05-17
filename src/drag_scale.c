/**
 * @file drag_scale.c
 * @brief Interactive window dragging and scaling with debounced saves
 *
 * Debounced config saves reduce disk I/O during continuous operations.
 * Centered scaling maintains visual stability during resize.
 */

#include <windows.h>
#include "config.h"
#include "drag_scale.h"
#include "plugin/plugin_data.h"

#include "color/color_state.h"

BOOL PREVIOUS_TOPMOST_STATE = false;
static bool g_editModeForcedTopmost = false;
static bool g_editModeTopmostOverride = false;

static UINT_PTR g_configSaveTimer = 0;

static inline void RefreshWindow(HWND hwnd, bool eraseBackground) {
    InvalidateRect(hwnd, nullptr, eraseBackground);
    UpdateWindow(hwnd);
}

static inline float ClampScaleFactor(float scale) {
    if (scale < MIN_SCALE_FACTOR)
        return MIN_SCALE_FACTOR;
    if (scale > MAX_SCALE_FACTOR)
        return MAX_SCALE_FACTOR;
    return scale;
}

static inline int CalculateCenteredPosition(int originalPos, int originalSize,
                                            int newSize) {
    return originalPos + (originalSize - newSize) / 2;
}

static VOID CALLBACK ConfigSaveTimerProc(HWND hwnd, [[maybe_unused]] UINT msg,
                                         UINT_PTR idEvent,
                                         [[maybe_unused]] DWORD dwTime) {
    if (idEvent != TIMER_ID_CONFIG_SAVE) {
        return;
    }

    SaveWindowSettings(hwnd);
    KillTimer(hwnd, TIMER_ID_CONFIG_SAVE);
    g_configSaveTimer = 0;
}

/* Debouncing: Only save after operations stop for CONFIG_SAVE_DELAY_MS */
void ScheduleConfigSave(HWND hwnd) {
    if (g_configSaveTimer != 0) {
        KillTimer(hwnd, TIMER_ID_CONFIG_SAVE);
    }

    g_configSaveTimer =
        SetTimer(hwnd, TIMER_ID_CONFIG_SAVE, CONFIG_SAVE_DELAY_MS,
                 (TIMERPROC)ConfigSaveTimerProc);
}

void StartDragWindow(HWND hwnd) {
    if (!CLOCK_EDIT_MODE)
        return;

    CLOCK_IS_DRAGGING = true;
    SetCapture(hwnd);
    GetCursorPos(&CLOCK_LAST_MOUSE_POS);
}

void StartEditMode(HWND hwnd) {
    PREVIOUS_TOPMOST_STATE = CLOCK_WINDOW_TOPMOST;
    g_editModeForcedTopmost = false;
    g_editModeTopmostOverride = false;

    if (!CLOCK_WINDOW_TOPMOST) {
        g_editModeForcedTopmost = true;
        SetWindowTopmostTransient(hwnd, true);
    }

    CLOCK_EDIT_MODE = true;

    RefreshWindow(hwnd, true);
    SetBlurBehind(hwnd, true);
    SetClickThrough(hwnd, false);
    RefreshWindow(hwnd, true);

    SetCursor(LoadCursorW(nullptr, IDC_ARROW));
}

void EndEditMode(HWND hwnd) {
    if (!CLOCK_EDIT_MODE)
        return;

    CLOCK_EDIT_MODE = false;

    SetBlurBehind(hwnd, false);
    SetClickThrough(hwnd, true);
    SaveWindowSettings(hwnd);

    WriteConfigColor(CLOCK_TEXT_COLOR);

    if (g_editModeForcedTopmost && !g_editModeTopmostOverride &&
        !PREVIOUS_TOPMOST_STATE) {
        SetWindowTopmostTransient(hwnd, false);

        InvalidateRect(hwnd, nullptr, true);
        RedrawWindow(hwnd, nullptr, nullptr,
                     RDW_ERASE | RDW_INVALIDATE | RDW_UPDATENOW);

        KillTimer(hwnd, TIMER_ID_EDIT_MODE_REFRESH);
    } else {
        RefreshWindow(hwnd, true);
    }

    g_editModeForcedTopmost = false;
    g_editModeTopmostOverride = false;
}

void MarkEditModeTopmostOverride(void) {
    if (CLOCK_EDIT_MODE) {
        g_editModeTopmostOverride = true;
    }
}

void EndDragWindow(HWND hwnd) {
    if (!CLOCK_EDIT_MODE || !CLOCK_IS_DRAGGING)
        return;

    CLOCK_IS_DRAGGING = false;
    ReleaseCapture();

    RefreshWindow(hwnd, true);
}

/* SWP_NOREDRAW + UpdateWindow maintains smooth dragging */
BOOL HandleDragWindow(HWND hwnd) {
    if (!CLOCK_EDIT_MODE || !CLOCK_IS_DRAGGING)
        return false;

    POINT currentPos;
    GetCursorPos(&currentPos);
    int deltaX = currentPos.x - CLOCK_LAST_MOUSE_POS.x;
    int deltaY = currentPos.y - CLOCK_LAST_MOUSE_POS.y;

    RECT windowRect;
    GetWindowRect(hwnd, &windowRect);
    int width = windowRect.right - windowRect.left;
    int height = windowRect.bottom - windowRect.top;

    int newX = windowRect.left + deltaX;
    int newY = windowRect.top + deltaY;

    SetWindowPos(hwnd, nullptr, newX, newY, width, height,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW);

    CLOCK_LAST_MOUSE_POS = currentPos;
    CLOCK_WINDOW_POS_X = newX;
    CLOCK_WINDOW_POS_Y = newY;

    InvalidateRect(hwnd, nullptr, false);
    return true;
}

/* Mouse wheel scaling: configurable step per notch, centered to maintain
 * stability */
BOOL HandleScaleWindow(HWND hwnd, int delta) {
    if (!CLOCK_EDIT_MODE)
        return false;

    BOOL isPluginMode = PluginData_IsActive();
    float oldScale =
        isPluginMode ? PLUGIN_FONT_SCALE_FACTOR : CLOCK_FONT_SCALE_FACTOR;

    RECT windowRect;
    GetWindowRect(hwnd, &windowRect);
    int oldWidth = windowRect.right - windowRect.left;
    int oldHeight = windowRect.bottom - windowRect.top;

    BOOL isCtrlDown = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    int stepPercent = isCtrlDown ? g_AppConfig.display.scale_step_fast
                                 : g_AppConfig.display.scale_step_normal;

    float scaleFactor = 1.0f + (stepPercent / 100.0f);

    float newScale =
        (delta > 0) ? oldScale * scaleFactor : oldScale / scaleFactor;

    newScale = ClampScaleFactor(newScale);

    if (newScale == oldScale)
        return false;

    if (isPluginMode) {
        PLUGIN_FONT_SCALE_FACTOR = newScale;
    } else {
        CLOCK_FONT_SCALE_FACTOR = newScale;
        CLOCK_WINDOW_SCALE = newScale;
    }

    float scalingRatio = newScale / oldScale;
    int newWidth = (int)(oldWidth * scalingRatio);
    int newHeight = (int)(oldHeight * scalingRatio);

    /* Center scaling to keep window visually stable */
    int newX = CalculateCenteredPosition(windowRect.left, oldWidth, newWidth);
    int newY = CalculateCenteredPosition(windowRect.top, oldHeight, newHeight);

    SetWindowPos(hwnd, nullptr, newX, newY, newWidth, newHeight,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW);

    RefreshWindow(hwnd, false);
    return true;
}
