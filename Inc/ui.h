/**
  ******************************************************************************
  * @file    ui.h
  * @brief   手写数字识别界面（左侧手写板 + 右侧置信度/结果面板）
  ******************************************************************************
  * 本模块把界面和触摸交互全部封装在一起：
  *   - 左侧：标题 + 大方框 + 手写笔迹 + 墨迹位图缓存
  *   - 右侧：0-9 置信度条 + 识别结果卡片 + SCAN / CLEAR 按钮
  *
  * 应用层只需要：
  *   1. 初始化时调用 UI_Init()
  *   2. 主循环里把触摸状态交给 UI_TouchUpdate()
  *   3. 收到 UI_EVT_SCAN 时做一次推理，然后调用 UI_ShowResult()
  ******************************************************************************
  */

#ifndef __UI_H
#define __UI_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* 触摸事件（UI_TouchUpdate 的返回值） */
#define UI_EVT_NONE     0u   /* 无事件 */
#define UI_EVT_SCAN     1u   /* SCAN 按钮按下（按下沿触发一次） */
#define UI_EVT_CLEAR    2u   /* CLEAR 按钮按下（按下沿触发一次，界面已自行清空） */
#define UI_EVT_PEN_DOWN 4u   /* 在手写板内起笔 */
#define UI_EVT_PEN_UP   8u   /* 在手写板内抬笔（可用于“抬手自动识别”） */

/* 手写墨迹位图尺寸：行优先，1 = 有笔迹，0 = 空白。
   注意：坐标原点对应屏幕 (UI_PAD_IX, UI_PAD_IY)。 */
#define UI_INK_W  223
#define UI_INK_H  223

/* 手写板书写区在屏幕上的位置（供应用层做坐标换算） */
#define UI_PAD_IX  11
#define UI_PAD_IY  36

void           UI_Init(void);
uint8_t        UI_TouchUpdate(uint16_t x, uint16_t y, uint8_t down);

const uint8_t *UI_GetInk(void);
void           UI_ClearInk(void);
void           UI_ClearPad(void);

void           UI_ShowResult(uint8_t digit, const uint8_t pct[10]);
void           UI_ResetResult(void);
void           UI_SetStatus(const char *text);

#ifdef __cplusplus
}
#endif

#endif /* __UI_H */
