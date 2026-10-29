/**
  ******************************************************************************
  * @file    fault_dump.c
  * @brief   把 HardFault / MemManage / BusFault / UsageFault 的现场打到 LCD 上
  ******************************************************************************
  * 用法：异常发生时屏幕会被清空，显示故障类型、PC、LR、R0-R3、CFSR/HFSR
  *       以及 MMFAR/BFAR，并把 CFSR 的各个错误位翻译成英文缩写。
  *
  * 常见位含义：
  *   IACCVIOL  取指违反 MPU 权限（跳到不可执行区域）
  *   DACCVIOL  数据访问违反 MPU 权限
  *   MMARVALID MMFAR 里是有效地址
  *   IBUSERR   取指出错（总线错误）
  *   PRECISERR 精确总线错误，BFAR 有效
  *   IMPRECISERR 非精确总线错误（写缓冲），BFAR 通常无效
  *   UNSTKERR/STKERR 异常进出栈失败（栈指针坏了）
  *   BFARVALID BFAR 里是有效地址
  *   UNDEFINSTR 未定义指令
  *   INVSTATE  非法的 Thumb 状态（跳到了奇地址/坏函数指针）
  *   INVPC     非法 PC 装载
  *   NOCP      协处理器不可用（FPU 未使能）
  *   UNALIGNED 非对齐访问
  *   DIVBYZERO 除零
  ******************************************************************************
  */

#include "main.h"
#include <stdio.h>
#include <string.h>

static void append_flag(char *out, size_t n, const char *name)
{
  size_t len = strlen(out);

  if ((len + strlen(name) + 2) >= n)
  {
    return;
  }
  strcat(out, name);
  strcat(out, " ");
}

/* MemManage：CFSR[7:0] */
static void decode_mm(uint32_t cfsr, char *out, size_t n)
{
  strncpy(out, "MM : ", n - 1);
  out[n - 1] = '\0';

  if (cfsr & (1UL << 0)) append_flag(out, n, "IACCVIOL");
  if (cfsr & (1UL << 1)) append_flag(out, n, "DACCVIOL");
  if (cfsr & (1UL << 3)) append_flag(out, n, "MUNSTKERR");
  if (cfsr & (1UL << 4)) append_flag(out, n, "MSTKERR");
  if (cfsr & (1UL << 5)) append_flag(out, n, "MLSPERR");
  if (cfsr & (1UL << 7)) append_flag(out, n, "MMARVALID");
}

/* BusFault：CFSR[15:8] */
static void decode_bus(uint32_t cfsr, char *out, size_t n)
{
  strncpy(out, "BUS: ", n - 1);
  out[n - 1] = '\0';

  if (cfsr & (1UL << 8))  append_flag(out, n, "IBUSERR");
  if (cfsr & (1UL << 9))  append_flag(out, n, "PRECISERR");
  if (cfsr & (1UL << 10)) append_flag(out, n, "IMPRECISERR");
  if (cfsr & (1UL << 11)) append_flag(out, n, "UNSTKERR");
  if (cfsr & (1UL << 12)) append_flag(out, n, "STKERR");
  if (cfsr & (1UL << 13)) append_flag(out, n, "LSPERR");
  if (cfsr & (1UL << 15)) append_flag(out, n, "BFARVALID");
}

/* UsageFault：CFSR[25:16] */
static void decode_usg(uint32_t cfsr, char *out, size_t n)
{
  strncpy(out, "USG: ", n - 1);
  out[n - 1] = '\0';

  if (cfsr & (1UL << 16)) append_flag(out, n, "UNDEFINSTR");
  if (cfsr & (1UL << 17)) append_flag(out, n, "INVSTATE");
  if (cfsr & (1UL << 18)) append_flag(out, n, "INVPC");
  if (cfsr & (1UL << 19)) append_flag(out, n, "NOCP");
  if (cfsr & (1UL << 24)) append_flag(out, n, "UNALIGNED");
  if (cfsr & (1UL << 25)) append_flag(out, n, "DIVBYZERO");
}

/**
  * @brief  异常现场打印
  * @param  frame      指向异常压栈帧：R0,R1,R2,R3,R12,LR,PC,xPSR
  * @param  exc_return EXC_RETURN（判断用的是 MSP 还是 PSP）
  * @param  kind       0=Hard 1=MemManage 2=Bus 3=Usage
  */
void Fault_Dump(uint32_t *frame, uint32_t exc_return, uint32_t kind)
{
  static const char *names[4] = { "HARD FAULT", "MEM MANAGE", "BUS FAULT", "USAGE FAULT" };
  char line[96];
  uint32_t cfsr  = SCB->CFSR;
  uint32_t hfsr  = SCB->HFSR;
  uint32_t mmfar = SCB->MMFAR;
  uint32_t bfar  = SCB->BFAR;

  __disable_irq();

  /* 屏幕可能已经被初始化过；这里只做最基本的写操作 */
  BSP_LCD_SelectLayer(LTDC_ACTIVE_LAYER);
  BSP_LCD_SetBackColor(LCD_COLOR_WHITE);
  BSP_LCD_Clear(LCD_COLOR_WHITE);
  BSP_LCD_SetFont(&Font12);

  BSP_LCD_SetTextColor(LCD_COLOR_RED);
  sprintf(line, "%s  EXC_RETURN=%08lx", names[kind & 3], (unsigned long)exc_return);
  BSP_LCD_DisplayStringAt(0, 0, (uint8_t *)line, LEFT_MODE);

  BSP_LCD_SetTextColor(LCD_COLOR_BLACK);

  sprintf(line, "PC=%08lx  LR=%08lx", (unsigned long)frame[6], (unsigned long)frame[5]);
  BSP_LCD_DisplayStringAt(0, 16, (uint8_t *)line, LEFT_MODE);

  sprintf(line, "R0=%08lx R1=%08lx", (unsigned long)frame[0], (unsigned long)frame[1]);
  BSP_LCD_DisplayStringAt(0, 32, (uint8_t *)line, LEFT_MODE);

  sprintf(line, "R2=%08lx R3=%08lx", (unsigned long)frame[2], (unsigned long)frame[3]);
  BSP_LCD_DisplayStringAt(0, 48, (uint8_t *)line, LEFT_MODE);

  sprintf(line, "CFSR=%08lx HFSR=%08lx", (unsigned long)cfsr, (unsigned long)hfsr);
  BSP_LCD_DisplayStringAt(0, 68, (uint8_t *)line, LEFT_MODE);

  sprintf(line, "MMFAR=%08lx BFAR=%08lx", (unsigned long)mmfar, (unsigned long)bfar);
  BSP_LCD_DisplayStringAt(0, 84, (uint8_t *)line, LEFT_MODE);

  decode_mm(cfsr, line, sizeof(line));
  BSP_LCD_DisplayStringAt(0, 108, (uint8_t *)line, LEFT_MODE);

  decode_bus(cfsr, line, sizeof(line));
  BSP_LCD_DisplayStringAt(0, 124, (uint8_t *)line, LEFT_MODE);

  decode_usg(cfsr, line, sizeof(line));
  BSP_LCD_DisplayStringAt(0, 140, (uint8_t *)line, LEFT_MODE);

  BSP_LCD_SetTextColor(LCD_COLOR_DARKGRAY);
  sprintf(line, "SP=%08lx  xPSR=%08lx", (unsigned long)frame, (unsigned long)frame[7]);
  BSP_LCD_DisplayStringAt(0, 164, (uint8_t *)line, LEFT_MODE);

  sprintf(line, "Reset to continue");
  BSP_LCD_DisplayStringAt(0, 188, (uint8_t *)line, LEFT_MODE);

  while (1)
  {
    __NOP();
  }
}
