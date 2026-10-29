#ifndef __AI_DIGIT_H
#define __AI_DIGIT_H

#include <stdint.h>

int     AI_Init(void);
int     AI_SelfTest(void);                                  /* 返回单次推理 ms，负数为错误 */
uint8_t AI_Predict(const uint8_t *ink, uint8_t pct[10]);   /* 返回 0..9，失败返回 0xFF */

#endif