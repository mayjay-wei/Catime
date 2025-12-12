/**
 * @file tray_menu_pomodoro.h
 * @brief Pomodoro menu building and configuration
 * 
 * Handles Pomodoro timer menu items and configuration loading.
 */

#ifndef TRAY_MENU_POMODORO_H
#define TRAY_MENU_POMODORO_H

#include <windows.h>

/**
 * @brief Build Pomodoro submenu
 * @param hMenu Parent menu handle to append Pomodoro submenu to
 * 
 * @details
 * Creates Pomodoro submenu with:
 * - Start button
 * - Time interval options (work, short break, long break, etc.)
 * - Loop count display
 * - Combination settings
 * 
 * Checkmarks indicate currently active Pomodoro phase.
 */
void BuildPomodoroMenu(HMENU hMenu);

#endif /* TRAY_MENU_POMODORO_H */

