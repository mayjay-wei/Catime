/**
 * @file tray_menu_pomodoro.c
 * @brief Pomodoro menu implementation
 */

#include "tray/tray_menu_pomodoro.h"
#include "tray/tray_menu.h"
#include "config.h"
#include "language.h"
#include "timer/timer.h"
#include "pomodoro.h"
#include "utils/string_format.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* External variables */
extern int current_pomodoro_time_index;
extern POMODORO_PHASE current_pomodoro_phase;
extern BOOL CLOCK_SHOW_CURRENT_TIME;
extern BOOL CLOCK_COUNT_UP;
extern int CLOCK_TOTAL_TIME;

#define MAX_POMODORO_TIMES 10

/**
 * @brief Build Pomodoro submenu
 * @param hMenu Parent menu handle to append to
 */
void BuildPomodoroMenu(HMENU hMenu) {
    if (!hMenu) return;

    HMENU hPomodoroMenu = CreatePopupMenu();
    
    wchar_t timeBuffer[64];
    
    AppendMenuW(hPomodoroMenu, MF_STRING, CLOCK_IDM_POMODORO_START,
                GetLocalizedString(NULL, L"Start"));
    AppendMenuW(hPomodoroMenu, MF_SEPARATOR, 0, NULL);

    for (int i = 0; i < g_AppConfig.pomodoro.times_count; i++) {
        FormatPomodoroTime(g_AppConfig.pomodoro.times[i], timeBuffer, sizeof(timeBuffer)/sizeof(wchar_t));
        
        UINT menuId;
        if (i == 0) menuId = CLOCK_IDM_POMODORO_WORK;
        else if (i == 1) menuId = CLOCK_IDM_POMODORO_BREAK;
        else if (i == 2) menuId = CLOCK_IDM_POMODORO_LBREAK;
        else menuId = CLOCK_IDM_POMODORO_TIME_BASE + i;
        
        BOOL isCurrentPhase = (current_pomodoro_phase != POMODORO_PHASE_IDLE &&
                              current_pomodoro_time_index == i &&
                              !CLOCK_SHOW_CURRENT_TIME &&
                              !CLOCK_COUNT_UP &&
                              CLOCK_TOTAL_TIME == g_AppConfig.pomodoro.times[i]);
        
        AppendMenuW(hPomodoroMenu, MF_STRING | (isCurrentPhase ? MF_CHECKED : MF_UNCHECKED), 
                    menuId, timeBuffer);
    }

    wchar_t menuText[64];
    _snwprintf_s(menuText, _countof(menuText), _TRUNCATE,
                GetLocalizedString(NULL, L"Loop Count: %d"),
                g_AppConfig.pomodoro.loop_count);
    AppendMenuW(hPomodoroMenu, MF_STRING, CLOCK_IDM_POMODORO_LOOP_COUNT, menuText);

    AppendMenuW(hPomodoroMenu, MF_SEPARATOR, 0, NULL);

    AppendMenuW(hPomodoroMenu, MF_STRING, CLOCK_IDM_POMODORO_COMBINATION,
              GetLocalizedString(NULL, L"Combination"));
    
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hPomodoroMenu,
                GetLocalizedString(NULL, L"Pomodoro"));
}

