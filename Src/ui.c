/**
  ******************************************************************************
  * @file    ui.c
  * @brief   手写数字识别界面：左侧手写板 + 右侧置信度/结果面板
  ******************************************************************************
  * 屏幕 480x272，布局：
  *
  *   0                                                                  480
  *   +--------------------------------------------------------------------+
  *   | +-- WRITE A DIGIT --+  +-- CONFIDENCE --+  +-- RESULT --+          |
  *   | |                   |  | 0 ####      4% |  | +--------+ |          |
  *   | |                   |  | 1 #         1% |  | |   7    | |          |
  *   | |     手写区        |  | ...             |  | +--------+ |          |
  *   | |    224 x 224      |  | 7 ########  97%|  |    97%     |          |
  *   | |                   |  | ...             |  |  [ SCAN ]  |          |
  *   | +-------------------+  | 9             0%|  | [ CLEAR ]  |          |
  *   |                        +-----------------+  +------------+          |
  *   +--------------------------------------------------------------------+
  *
  * 左右两侧垂直范围一致：标题顶 y=6，底边 y=261，标题条厚度均为 27。
  * 所有坐标常量集中在本文件顶部，改布局只需改常量。
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "ui.h"
#include <string.h>

/* ============================ 左侧手写板 ================================== */
#define PAD_X       10
#define PAD_Y       35
#define PAD_W       224
#define PAD_H       224

#define PAD_IX      UI_PAD_IX            /* 书写区左上角 x = 11 */
#define PAD_IY      UI_PAD_IY            /* 书写区左上角 y = 36 */
#define PAD_IW      UI_INK_W             /* 223 */
#define PAD_IH      UI_INK_H             /* 223 */

/* 实际落笔范围：内缩 2px，保证 3px 粗的笔画不会压到内边框 */
#define DRAW_X0     (PAD_X + 2)
#define DRAW_Y0     (PAD_Y + 2)
#define DRAW_X1     (PAD_X + PAD_W - 3)
#define DRAW_Y1     (PAD_Y + PAD_H - 3)

/* 标题标签：宽度与外框严格等长（外框 DrawRect 覆盖 8..236，共 229px） */
#define LABEL_X     (PAD_X - 2)          /* 8   */
#define LABEL_Y     6
#define LABEL_W     (PAD_W + 5)          /* 229 */
#define LABEL_H     27

#define TITLE_STR   "WRITE A DIGIT"
#define TITLE_W     (13 * 11)                              /* Font16 字宽 11 */
#define TITLE_X     (LABEL_X + (LABEL_W - TITLE_W) / 2)    /* 51 */
#define TITLE_Y     (LABEL_Y + (LABEL_H - 16) / 2)         /* 11 */

/* ============================ 右侧面板 ==================================== */
/* 高度与左侧对齐：左侧整体从 y=6（标题顶）到 y=261（外框底边），
   面板 DrawRect 的底边在 y+h，故 h = 261 - 6 = 255。
   面板标题条厚度 27，与左侧标题底板 (6..32) 同高。 */
#define PA_X        242                  /* 置信度面板 */
#define PA_Y        6
#define PA_W        144
#define PA_H        255

#define PB_X        390                  /* 结果面板 */
#define PB_Y        6
#define PB_W        86
#define PB_H        255

#define HDR_H       27                   /* 面板标题条高度，与左侧标题底板一致 */

#define ROW_Y0      42                   /* 第 0 行置信度的 y */
#define ROW_PITCH   22                   /* 行间距 */
#define DIGIT_X     249                  /* 数字标签 x */
#define BAR_X       259                  /* 条形轨道 x */
#define BAR_W       86                   /* 条形轨道宽 */
#define BAR_H       12                   /* 与 Font12 行高一致，行内顶对齐 */
#define PCT_X       348                  /* 百分比 x（4 字符 = 28px） */

#define CARD_X      394                  /* 结果卡片 */
#define CARD_Y      38
#define CARD_W      78
#define CARD_H      104

#define BTN_X       394                  /* 按钮 */
#define BTN_W       78
#define BTN_H       32
#define BTN_SCAN_Y  172
#define BTN_CLEAR_Y 211

#define STATUS_Y    248                  /* 面板底部状态文字 */

/* ============================ 配色 ======================================== */
#define C_BORDER    LCD_COLOR_GRAY
#define C_HEADER    LCD_COLOR_DARKBLUE
#define C_TRACK     LCD_COLOR_LIGHTGRAY
#define C_BAR       LCD_COLOR_BLUE
#define C_TOP       LCD_COLOR_ORANGE
#define C_TEXT      LCD_COLOR_DARKGRAY

/* ============================ 私有变量 ==================================== */
static uint8_t s_ink[UI_INK_H][UI_INK_W];
static uint8_t s_lastPct[10];
static uint8_t s_lastBest = 0xFF;

/* ============================ 私有函数 ==================================== */

/**
  * @brief  用 Font24 的点阵按 scale 倍放大绘制一个 ASCII 字符，并按墨迹包围盒居中。
  * @param  cx : 目标中心的 x 坐标（按字模实际墨迹居中，避免 "1" 之类偏左）
  * @param  y  : 字符标称框的顶部（24*scale 高的区域）
  * @note   用于结果卡片里的大号数字（17x24 放大 4 倍 = 68x96）。
  *         字模格式与 BSP 的 DrawChar() 一致：行优先，每行 (Width+7)/8 字节，
  *         高位在前，行尾有 (字节数*8 - Width) 个空位。
  */
static void UI_DrawCharScaled(uint16_t cx, uint16_t y, uint8_t ch,
                              uint8_t scale, uint32_t color)
{
  sFONT  *f   = &Font24;
  uint8_t bpr = (f->Width + 7) / 8;         /* bytes per row */
  uint8_t pad = bpr * 8 - f->Width;
  const uint8_t *p = f->table + (uint32_t)(ch - ' ') * f->Height * bpr;
  uint16_t row, col;
  uint16_t minc = f->Width, maxc = 0;
  uint16_t minr = f->Height, maxr = 0;
  int32_t  x0, y0;
  uint16_t w, h;

  /* 第一遍：求墨迹包围盒 */
  for (row = 0; row < f->Height; row++)
  {
    uint32_t line = 0;

    for (uint8_t b = 0; b < bpr; b++)
    {
      line = (line << 8) | p[row * bpr + b];
    }

    for (col = 0; col < f->Width; col++)
    {
      if (line & (1UL << (f->Width - col + pad - 1)))
      {
        if (col < minc) minc = col;
        if (col > maxc) maxc = col;
        if (row < minr) minr = row;
        if (row > maxr) maxr = row;
      }
    }
  }

  if (maxr < minr)      /* 空白字符 */
  {
    return;
  }

  w  = (uint16_t)((maxc - minc + 1) * scale);
  h  = (uint16_t)((maxr - minr + 1) * scale);
  x0 = (int32_t)cx - (int32_t)w / 2 - (int32_t)minc * scale;
  y0 = (int32_t)y + ((int32_t)f->Height * scale - (int32_t)h) / 2
       - (int32_t)minr * scale;

  BSP_LCD_SetTextColor(color);

  /* 第二遍：只绘制包围盒内的点 */
  for (row = minr; row <= maxr; row++)
  {
    uint32_t line = 0;

    for (uint8_t b = 0; b < bpr; b++)
    {
      line = (line << 8) | p[row * bpr + b];
    }

    for (col = minc; col <= maxc; col++)
    {
      if (line & (1UL << (f->Width - col + pad - 1)))
      {
        BSP_LCD_FillRect((uint16_t)(x0 + (int32_t)col * scale),
                         (uint16_t)(y0 + (int32_t)row * scale),
                         scale, scale);
      }
    }
  }
}

/* ---------------------------- 墨迹缓存 ------------------------------------ */

/* 在缓存里点一个半径 1 的圆点（与屏幕上的笔宽对应） */
static void UI_InkDot(int16_t x, int16_t y)
{
  int16_t dx, dy;

  for (dy = -1; dy <= 1; dy++)
  {
    for (dx = -1; dx <= 1; dx++)
    {
      int16_t ix = (int16_t)(x - PAD_IX + dx);
      int16_t iy = (int16_t)(y - PAD_IY + dy);

      if ((ix >= 0) && (ix < UI_INK_W) && (iy >= 0) && (iy < UI_INK_H))
      {
        s_ink[iy][ix] = 1;
      }
    }
  }
}

/* Bresenham：把两个采样点之间的笔画全部写入缓存，避免快速书写漏点 */
static void UI_InkLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1)
{
  int16_t dx = (int16_t)((x1 > x0) ? (x1 - x0) : (x0 - x1));
  int16_t dy = (int16_t)((y1 > y0) ? (y0 - y1) : (y1 - y0));  /* 负数 */
  int16_t sx = (x0 < x1) ? 1 : -1;
  int16_t sy = (y0 < y1) ? 1 : -1;
  int16_t err = (int16_t)(dx + dy);

  for (;;)
  {
    int16_t e2;

    UI_InkDot(x0, y0);

    if ((x0 == x1) && (y0 == y1))
    {
      break;
    }

    e2 = (int16_t)(2 * err);
    if (e2 >= dy) { err = (int16_t)(err + dy); x0 = (int16_t)(x0 + sx); }
    if (e2 <= dx) { err = (int16_t)(err + dx); y0 = (int16_t)(y0 + sy); }
  }
}

/* ---------------------------- 左侧手写板 ---------------------------------- */

static void UI_DrawPad(void)
{
  /* 标题底板：蓝底，宽度与方框外框等长 */
  BSP_LCD_SetTextColor(LCD_COLOR_BLUE);
  BSP_LCD_FillRect(LABEL_X, LABEL_Y, LABEL_W, LABEL_H);

  /* 标题：白字，相对底板居中 */
  BSP_LCD_SetBackColor(LCD_COLOR_BLUE);
  BSP_LCD_SetTextColor(LCD_COLOR_WHITE);
  BSP_LCD_SetFont(&Font16);
  BSP_LCD_DisplayStringAt(TITLE_X, TITLE_Y, (uint8_t *)TITLE_STR, LEFT_MODE);

  /* 恢复默认背景色，避免后续文字带蓝底 */
  BSP_LCD_SetBackColor(LCD_COLOR_WHITE);

  /* 方框：三圈叠出 3px 粗边框 */
  BSP_LCD_SetTextColor(LCD_COLOR_BLUE);
  BSP_LCD_DrawRect(PAD_X - 2, PAD_Y - 2, PAD_W + 4, PAD_H + 4);
  BSP_LCD_DrawRect(PAD_X - 1, PAD_Y - 1, PAD_W + 2, PAD_H + 2);
  BSP_LCD_DrawRect(PAD_X,     PAD_Y,     PAD_W,     PAD_H);
}

/* ---------------------------- 右侧面板 ------------------------------------ */

static void UI_DrawPanel(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                         const char *title)
{
  uint16_t tw = (uint16_t)strlen(title) * Font12.Width;

  /* 面板白底 + 灰色细边框 */
  BSP_LCD_SetTextColor(LCD_COLOR_WHITE);
  BSP_LCD_FillRect(x, y, w, h);

  BSP_LCD_SetTextColor(C_BORDER);
  BSP_LCD_DrawRect(x, y, w, h);

  /* 标题条：深蓝底白字 */
  BSP_LCD_SetTextColor(C_HEADER);
  BSP_LCD_FillRect(x, y, w, HDR_H);

  BSP_LCD_SetFont(&Font12);
  BSP_LCD_SetBackColor(C_HEADER);
  BSP_LCD_SetTextColor(LCD_COLOR_WHITE);
  BSP_LCD_DisplayStringAt((uint16_t)(x + (w - tw) / 2),
                          (uint16_t)(y + (HDR_H - Font12.Height) / 2),
                          (uint8_t *)title, LEFT_MODE);

  BSP_LCD_SetBackColor(LCD_COLOR_WHITE);
}

/* 一行置信度：数字标签 + 条形 + 百分比 */
static void UI_DrawBarRow(uint8_t i, uint8_t pct, uint8_t best, uint8_t force)
{
  uint16_t y = (uint16_t)(ROW_Y0 + i * ROW_PITCH);
  uint16_t w = (uint16_t)((uint16_t)pct * BAR_W / 100);
  char     buf[8];

  if (!force && (pct == s_lastPct[i]))
  {
    return;
  }

  BSP_LCD_SetFont(&Font12);
  BSP_LCD_SetBackColor(LCD_COLOR_WHITE);

  /* 数字标签：最大值加粗并改成橙色 */
  BSP_LCD_SetTextColor(LCD_COLOR_WHITE);
  BSP_LCD_FillRect(DIGIT_X, y, 9, 12);

  BSP_LCD_SetTextColor((i == best) ? C_TOP : C_TEXT);
  BSP_LCD_DisplayChar(DIGIT_X, y, (uint8_t)('0' + i));
  if (i == best)
  {
    BSP_LCD_DisplayChar((uint16_t)(DIGIT_X + 1), y, (uint8_t)('0' + i));
  }

  /* 轨道 + 填充 */
  BSP_LCD_SetTextColor(C_TRACK);
  BSP_LCD_FillRect(BAR_X, y, BAR_W, BAR_H);

  if (w)
  {
    BSP_LCD_SetTextColor((i == best) ? C_TOP : C_BAR);
    BSP_LCD_FillRect(BAR_X, y, w, BAR_H);
  }

  /* 百分比：固定 4 字符，新旧内容等宽，不留残影 */
  sprintf(buf, "%3d%%", pct);
  BSP_LCD_SetTextColor((i == best) ? C_TOP : C_TEXT);
  BSP_LCD_DisplayStringAt(PCT_X, y, (uint8_t *)buf, LEFT_MODE);

  s_lastPct[i] = pct;
}

/* 结果卡片：大号数字 + 该数字的置信度 */
static void UI_DrawResultCard(uint8_t digit, uint8_t pct)
{
  char buf[8];

  /* 清卡片内部（保留 1px 边框） */
  BSP_LCD_SetTextColor(LCD_COLOR_WHITE);
  BSP_LCD_FillRect((uint16_t)(CARD_X + 1), (uint16_t)(CARD_Y + 1),
                   (uint16_t)(CARD_W - 1), (uint16_t)(CARD_H - 1));

  if (digit <= 9)
  {
    UI_DrawCharScaled((uint16_t)(CARD_X + CARD_W / 2), (uint16_t)(CARD_Y + 4),
                      (uint8_t)('0' + digit), 4, LCD_COLOR_BLACK);
    sprintf(buf, "%3d%%", pct);
  }
  else
  {
    UI_DrawCharScaled((uint16_t)(CARD_X + CARD_W / 2), (uint16_t)(CARD_Y + 4),
                      '?', 4, LCD_COLOR_LIGHTGRAY);
    sprintf(buf, " --%%");
  }

  BSP_LCD_SetTextColor(C_BORDER);
  BSP_LCD_DrawRect(CARD_X, CARD_Y, CARD_W, CARD_H);

  BSP_LCD_SetFont(&Font12);
  BSP_LCD_SetBackColor(LCD_COLOR_WHITE);
  BSP_LCD_SetTextColor((digit <= 9) ? C_BAR : LCD_COLOR_LIGHTGRAY);
  BSP_LCD_DisplayStringAt((uint16_t)(CARD_X + (CARD_W - 28) / 2),
                          (uint16_t)(CARD_Y + CARD_H + 10),
                          (uint8_t *)buf, LEFT_MODE);
}

/* ---------------------------- 按钮 ---------------------------------------- */

static void UI_PressScan(uint8_t pressed)
{
  BSP_LCD_SetTextColor(pressed ? LCD_COLOR_DARKBLUE : C_BAR);
  BSP_LCD_FillRect(BTN_X, BTN_SCAN_Y, BTN_W, BTN_H);

  BSP_LCD_SetFont(&Font12);
  BSP_LCD_SetBackColor(pressed ? LCD_COLOR_DARKBLUE : C_BAR);
  BSP_LCD_SetTextColor(LCD_COLOR_WHITE);
  BSP_LCD_DisplayStringAt((uint16_t)(BTN_X + (BTN_W - 28) / 2),
                          (uint16_t)(BTN_SCAN_Y + 10),
                          (uint8_t *)"SCAN", LEFT_MODE);
}

static void UI_PressClear(uint8_t pressed)
{
  BSP_LCD_SetTextColor(pressed ? C_BAR : LCD_COLOR_WHITE);
  BSP_LCD_FillRect(BTN_X, BTN_CLEAR_Y, BTN_W, BTN_H);

  BSP_LCD_SetTextColor(C_BAR);
  BSP_LCD_DrawRect(BTN_X, BTN_CLEAR_Y, BTN_W, BTN_H);

  BSP_LCD_SetFont(&Font12);
  BSP_LCD_SetBackColor(pressed ? C_BAR : LCD_COLOR_WHITE);
  BSP_LCD_SetTextColor(pressed ? LCD_COLOR_WHITE : C_BAR);
  BSP_LCD_DisplayStringAt((uint16_t)(BTN_X + (BTN_W - 35) / 2),
                          (uint16_t)(BTN_CLEAR_Y + 10),
                          (uint8_t *)"CLEAR", LEFT_MODE);
}

static uint8_t UI_HitScan(uint16_t x, uint16_t y)
{
  return (uint8_t)((x >= BTN_X) && (x < BTN_X + BTN_W) &&
                   (y >= BTN_SCAN_Y) && (y < BTN_SCAN_Y + BTN_H));
}

static uint8_t UI_HitClear(uint16_t x, uint16_t y)
{
  return (uint8_t)((x >= BTN_X) && (x < BTN_X + BTN_W) &&
                   (y >= BTN_CLEAR_Y) && (y < BTN_CLEAR_Y + BTN_H));
}

/* ============================ 对外接口 ==================================== */

void UI_Init(void)
{
  uint8_t i;

  BSP_LCD_SetBackColor(LCD_COLOR_WHITE);
  BSP_LCD_Clear(LCD_COLOR_WHITE);

  /* 左：手写板；右：两栏面板 */
  UI_DrawPad();
  UI_DrawPanel(PA_X, PA_Y, PA_W, PA_H, "CONFIDENCE");
  UI_DrawPanel(PB_X, PB_Y, PB_W, PB_H, "RESULT");

  /* 初始状态：置信度全 0，结果未知 */
  for (i = 0; i < 10; i++)
  {
    s_lastPct[i] = 0;
  }
  s_lastBest = 0xFF;

  UI_ResetResult();
  UI_PressScan(0);
  UI_PressClear(0);
  UI_SetStatus("READY");
}

uint8_t UI_TouchUpdate(uint16_t x, uint16_t y, uint8_t down)
{
  static uint16_t lastx = 0;
  static uint16_t lasty = 0;
  static uint8_t  pen = 0;          /* 当前是否处于落笔状态 */
  static uint8_t  pressScan = 0;
  static uint8_t  pressClear = 0;
  static uint8_t  scanArmed = 1;    /* SCAN 是否已武装，防止触摸抖动连续触发 */
  static uint8_t  relCnt = 0;       /* 连续“无触摸”帧计数，做抬手防抖 */

  uint8_t evt = UI_EVT_NONE;

  /* ---------------- 抬手 ---------------- */
  if (!down)
  {
    if (pen)
    {
      evt = UI_EVT_PEN_UP;
    }
    pen = 0;

    if (pressScan)
    {
      pressScan = 0;
      UI_PressScan(0);
    }
    if (pressClear)
    {
      pressClear = 0;
      UI_PressClear(0);
    }

    /* 连续 3 帧（约 15ms）没有触摸才重新武装，单帧毛刺不会触发第二次推理 */
    if (relCnt < 3)
    {
      relCnt++;
    }
    if (relCnt >= 3)
    {
      scanArmed = 1;
    }

    return evt;
  }

  relCnt = 0;

  /* ---------------- SCAN 按钮：按下沿触发一次 ---------------- */
  if (UI_HitScan(x, y))
  {
    pen = 0;
    if (pressClear)
    {
      pressClear = 0;
      UI_PressClear(0);
    }
    if (scanArmed && !pressScan)
    {
      // scanArmed = 0;
      pressScan = 1;
      UI_PressScan(1);
      evt = UI_EVT_SCAN;
    }
    return evt;
  }

  /* ---------------- CLEAR 按钮：按下沿触发，界面自行清空 ---------------- */
  if (UI_HitClear(x, y))
  {
    pen = 0;
    if (pressScan)
    {
      pressScan = 0;
      UI_PressScan(0);
    }
    if (!pressClear)
    {
      pressClear = 1;
      UI_PressClear(1);

      UI_ClearPad();
      UI_ResetResult();
      UI_SetStatus("READY");

      evt = UI_EVT_CLEAR;
    }
    return evt;
  }

  /* 手指移到按钮外：只恢复按钮外观，重新武装仍要等真正抬手 */
  if (pressScan)
  {
    pressScan = 0;
    UI_PressScan(0);
  }
  if (pressClear)
  {
    pressClear = 0;
    UI_PressClear(0);
  }

  /* ---------------- 手写区 ---------------- */
  if ((x >= DRAW_X0) && (x <= DRAW_X1) && (y >= DRAW_Y0) && (y <= DRAW_Y1))
  {
    BSP_LCD_SetTextColor(LCD_COLOR_BLACK);

    if (pen)
    {
      /* 三根线叠出 3px 粗笔画，并保证快速书写不断线 */
      BSP_LCD_DrawLine(lastx, lasty, x, y);
      BSP_LCD_DrawLine((uint16_t)(lastx + 1), lasty, (uint16_t)(x + 1), y);
      BSP_LCD_DrawLine(lastx, (uint16_t)(lasty + 1), x, (uint16_t)(y + 1));

      UI_InkLine((int16_t)lastx, (int16_t)lasty, (int16_t)x, (int16_t)y);
    }
    else
    {
      BSP_LCD_FillCircle(x, y, 1);
      UI_InkDot((int16_t)x, (int16_t)y);
      UI_SetStatus("WRITING");
      evt = UI_EVT_PEN_DOWN;
    }

    lastx = x;
    lasty = y;
    pen = 1;
  }
  else
  {
    pen = 0;   /* 移出书写区即断笔 */
  }

  return evt;
}

const uint8_t *UI_GetInk(void)
{
  return &s_ink[0][0];
}

void UI_ClearInk(void)
{
  memset(s_ink, 0, sizeof(s_ink));
}

void UI_ClearPad(void)
{
  UI_ClearInk();

  /* 只清框内区域，标题和边框保留 */
  BSP_LCD_SetTextColor(LCD_COLOR_WHITE);
  BSP_LCD_FillRect(PAD_IX, PAD_IY, PAD_IW, PAD_IH);

  /* 内边框可能被覆盖，重画一次 */
  BSP_LCD_SetTextColor(LCD_COLOR_BLUE);
  BSP_LCD_DrawRect(PAD_X, PAD_Y, PAD_W, PAD_H);
}

void UI_ShowResult(uint8_t digit, const uint8_t pct[10])
{
  uint8_t i;
  uint8_t best = (digit <= 9) ? digit : 0xFF;
  uint8_t force = (uint8_t)(best != s_lastBest);
  uint8_t pv[10];

  /* 归一化：防止上层传入 >100 的值把条形画到框外 */
  for (i = 0; i < 10; i++)
  {
    pv[i] = (pct[i] > 100) ? 100 : pct[i];
  }

  for (i = 0; i < 10; i++)
  {
    UI_DrawBarRow(i, pv[i], best, force);
  }

  s_lastBest = best;

  /* 结果卡片本身就是 top-1，无需再在置信度面板里重复汇总 */
  UI_DrawResultCard(best, (best <= 9) ? pv[best] : 0);
}

void UI_ResetResult(void)
{
  uint8_t i;

  s_lastBest = 0xFF;

  for (i = 0; i < 10; i++)
  {
    UI_DrawBarRow(i, 0, 0xFF, 1);
  }

  UI_DrawResultCard(0xFF, 0);
}

void UI_SetStatus(const char *text)
{
  uint16_t tw = (uint16_t)strlen(text) * Font12.Width;

  /* 先擦掉旧状态，长度变化时不会留残影 */
  BSP_LCD_SetTextColor(LCD_COLOR_WHITE);
  BSP_LCD_FillRect((uint16_t)(PB_X + 1), (uint16_t)(STATUS_Y - 2),
                   (uint16_t)(PB_W - 2), 14);

  BSP_LCD_SetFont(&Font12);
  BSP_LCD_SetBackColor(LCD_COLOR_WHITE);
  BSP_LCD_SetTextColor(C_TEXT);
  BSP_LCD_DisplayStringAt((uint16_t)(PB_X + (PB_W - tw) / 2), STATUS_Y,
                          (uint8_t *)text, LEFT_MODE);
}

/**
  * @}
  */
