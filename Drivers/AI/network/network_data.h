/**
  ******************************************************************************
  * @file    network_data.h
  * @author  AST Embedded Analytics Research Platform
  * @date    2026-10-28T17:11:51+0800
  * @brief   AI Tool Automatic Code Generator for Embedded NN computing
  ******************************************************************************
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  ******************************************************************************
  */

/*==============================================================================
 * 权重数组声明（定义在 network_data.c）。
 *
 * 为什么是 uint64_t 而不是 float？
 *   - 保证 8 字节对齐，运行时可以按任意 4 字节偏移安全地重解释成 float；
 *   - 它是一个“字节块”，float 的含义完全由 network.c 里的偏移量决定
 *     （例如 conv1 权重在 +0、bias 在 +288、fc1 权重在 +4992 ……）。
 *
 * 总大小 = 26069 * 8 = 208552 字节，正好等于 STAI_NETWORK_WEIGHTS_SIZE_BYTES。
 * 它是 const，链接后位于 Flash（.rodata），不占 RAM。
 *============================================================================*/
#ifndef STAI_NETWORK_DATA_H
#define STAI_NETWORK_DATA_H
#include "stai.h"
STAI_ALIGNED(8)
extern const uint64_t g_network_weights_array[26069];
#endif  /* STAI_NETWORK_DATA_H */

