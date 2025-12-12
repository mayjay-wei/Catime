# 番茄鐘核心流程分析

## 🍅 番茄鐘狀態機

```
[IDLE] ──start──> [WORK] ──完成──> [BREAK] ──完成──> [WORK]
   ↑                                   │
   │                                   │ 
   └────── 4次循環後 ←────[LONG_BREAK]──┘
```

## 🎯 關鍵變量追蹤

1. **current_pomodoro_phase**: 當前階段
2. **current_pomodoro_time_index**: 當前時間索引 (0=工作, 1=短休息, 2=長休息...)  
3. **complete_pomodoro_cycles**: 完成的循環次數

## 📋 核心函數（學習順序）

### 第1層：初始化
- `InitializePomodoro()` - 啟動番茄鐘

### 第2層：狀態管理  
- `AdvancePomodoroState()` - 推進到下一階段
- `HandlePomodoroCompletion()` - 處理完成邏輯
- `IsActivePomodoroTimer()` - 檢查是否活躍

### 第3層：計時器邏輯
- `HandleMainTimer()` - 主計時器事件
- `HandleCountdownTimeout()` - 倒計時結束處理

## 🔗 文件間關係

```
timer.h (接口) 
    ↓
timer_events.c (實現)
    ↓  
pomodoro.h (狀態)
    ↓
config.h (配置)
```