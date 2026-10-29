/**
  ******************************************************************************
  * @file    network.h
  * @date    2026-10-28T17:11:51+0800
  * @brief   ST.AI Tool Automatic Code Generator for Embedded NN computing
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */

/*==============================================================================
 * 本文件是“模型结构描述表”，由 ST Edge AI Core 自动生成，重新生成会覆盖。
 *
 * 它不参与推理计算，只描述网络长什么样：
 *   - g_network_details.tensors[11] : 11 个张量（从输入到输出按顺序编号 0..10）；
 *   - g_network_details.nodes[10]   : 10 个计算节点，说明“用哪些张量算出哪个张量”；
 *   - n_nodes                       : 节点数量。
 *
 * 每个张量给出：
 *   size_bytes : 该张量占多少字节（float32 每数 4 字节，3136 = 784 个 float）；
 *   flags      : HAS_BATCH 表示带 batch 维；CHANNEL_LAST 表示通道在最后；
 *   format     : 数据类型，本模型全是 STAI_FORMAT_FLOAT32；
 *   shape      : {维数, {各维大小}}，如 {4, {1,28,28,1}} 表示 N,H,W,C；
 *   scale/zeropoint : 量化参数，float 模型为空；
 *   name       : 对应 ONNX 图里的张量名，调试时非常有用。
 *
 * 节点类型与 PyTorch 模型的对应关系（按执行顺序）：
 *   1 Conv2D    _conv1            -> 28x28x8
 *   2 NL(Relu)  _relu             -> 28x28x8
 *   3 Pool      _pool (MaxPool)   -> 14x14x8
 *   4 Conv2D    _conv2            -> 14x14x16
 *   5 NL(Relu)  _relu_1           -> 14x14x16
 *   6 Pool      _pool_1           -> 7x7x16
 *   7 Transpose _Reshape...chlast -> 仍然是 784 个 float，只是调整了内存顺序
 *   8 Dense     _fc1              -> 1x64
 *   9 NL(Relu)  _relu_2           -> 1x64
 *  10 Dense     output (fc2)      -> 1x10（logits，没有 Softmax）
 *
 * 节点的 id 不是连续的（缺 7），这是生成器内部编号方式，属正常现象；
 * 真正决定顺序的是数组里的先后次序。
 *
 * 注意：这个头文件里定义的是“非 static 的全局变量”，
 * 只能被一个 .c 包含（目前是 network.c），否则会重复定义导致链接失败。
 *============================================================================*/
#ifndef STAI_NETWORK_DETAILS_H
#define STAI_NETWORK_DETAILS_H

#include "stai.h"
#include "layers.h"

const stai_network_details g_network_details = {
  /* tensors[11]：按张量编号排列，0 是输入，10 是输出 */
  .tensors = (const stai_tensor[11]) {
   /* [0] 网络输入：1x28x28x1，float32，3136 字节。
    *     这里用 NHWC 顺序，而 network.h 的 IN_1_SHAPE 写的是 NCHW；
    *     因为输入只有 1 个通道，两种写法在内存上完全等价。 */
   { .size_bytes = 3136, .flags = (STAI_FLAG_HAS_BATCH|STAI_FLAG_CHANNEL_LAST), .format = STAI_FORMAT_FLOAT32, .shape = {4, (const int32_t[4]){1, 28, 28, 1}}, .scale = {0, NULL}, .zeropoint = {0, NULL}, .name = "input_output" },
   /* [1] conv1 输出：8 通道 28x28 = 6272 个 float = 25088 字节 */
   { .size_bytes = 25088, .flags = (STAI_FLAG_HAS_BATCH|STAI_FLAG_CHANNEL_LAST), .format = STAI_FORMAT_FLOAT32, .shape = {4, (const int32_t[4]){1, 28, 28, 8}}, .scale = {0, NULL}, .zeropoint = {0, NULL}, .name = "_conv1_Conv_output_0_output" },
   /* [2] ReLU1 输出：形状同 [1]；实际是原地运算，和 [1] 用同一块内存 */
   { .size_bytes = 25088, .flags = (STAI_FLAG_HAS_BATCH|STAI_FLAG_CHANNEL_LAST), .format = STAI_FORMAT_FLOAT32, .shape = {4, (const int32_t[4]){1, 28, 28, 8}}, .scale = {0, NULL}, .zeropoint = {0, NULL}, .name = "_relu_Relu_output_0_output" },
   /* [3] MaxPool1 输出：8 通道 14x14 = 1568 个 float = 6272 字节 */
   { .size_bytes = 6272, .flags = (STAI_FLAG_HAS_BATCH|STAI_FLAG_CHANNEL_LAST), .format = STAI_FORMAT_FLOAT32, .shape = {4, (const int32_t[4]){1, 14, 14, 8}}, .scale = {0, NULL}, .zeropoint = {0, NULL}, .name = "_pool_MaxPool_output_0_output" },
   /* [4] conv2 输出：16 通道 14x14 = 3136 个 float = 12544 字节 */
   { .size_bytes = 12544, .flags = (STAI_FLAG_HAS_BATCH|STAI_FLAG_CHANNEL_LAST), .format = STAI_FORMAT_FLOAT32, .shape = {4, (const int32_t[4]){1, 14, 14, 16}}, .scale = {0, NULL}, .zeropoint = {0, NULL}, .name = "_conv2_Conv_output_0_output" },
   /* [5] ReLU2 输出：形状同 [4]（原地运算） */
   { .size_bytes = 12544, .flags = (STAI_FLAG_HAS_BATCH|STAI_FLAG_CHANNEL_LAST), .format = STAI_FORMAT_FLOAT32, .shape = {4, (const int32_t[4]){1, 14, 14, 16}}, .scale = {0, NULL}, .zeropoint = {0, NULL}, .name = "_relu_1_Relu_output_0_output" },
   /* [6] MaxPool2 输出：16 通道 7x7 = 784 个 float = 3136 字节 */
   { .size_bytes = 3136, .flags = (STAI_FLAG_HAS_BATCH|STAI_FLAG_CHANNEL_LAST), .format = STAI_FORMAT_FLOAT32, .shape = {4, (const int32_t[4]){1, 7, 7, 16}}, .scale = {0, NULL}, .zeropoint = {0, NULL}, .name = "_pool_1_MaxPool_output_0_output" },
   /* [7] Transpose 之后的张量：内容仍是 16*7*7 = 784 个 float，
    *     只是改变了通道与空间维度的排列顺序，方便 fc1 按 784 维展开 */
   { .size_bytes = 3136, .flags = (STAI_FLAG_HAS_BATCH|STAI_FLAG_CHANNEL_LAST), .format = STAI_FORMAT_FLOAT32, .shape = {4, (const int32_t[4]){1, 16, 7, 7}}, .scale = {0, NULL}, .zeropoint = {0, NULL}, .name = "_Reshape_output_0_to_chlast_output" },
   /* [8] fc1 输出：64 个 float = 256 字节 */
   { .size_bytes = 256, .flags = (STAI_FLAG_HAS_BATCH|STAI_FLAG_CHANNEL_LAST), .format = STAI_FORMAT_FLOAT32, .shape = {2, (const int32_t[2]){1, 64}}, .scale = {0, NULL}, .zeropoint = {0, NULL}, .name = "_fc1_Gemm_output_0_output" },
   /* [9] ReLU3 输出：64 个 float（非原地，单独占一块，地址在激活区 +580） */
   { .size_bytes = 256, .flags = (STAI_FLAG_HAS_BATCH|STAI_FLAG_CHANNEL_LAST), .format = STAI_FORMAT_FLOAT32, .shape = {2, (const int32_t[2]){1, 64}}, .scale = {0, NULL}, .zeropoint = {0, NULL}, .name = "_relu_2_Relu_output_0_output" },
   /* [10] 网络输出：10 个 float = 40 字节，是 logits 而不是概率（图里没有 Softmax 层） */
   { .size_bytes = 40, .flags = (STAI_FLAG_HAS_BATCH|STAI_FLAG_CHANNEL_LAST), .format = STAI_FORMAT_FLOAT32, .shape = {2, (const int32_t[2]){1, 10}}, .scale = {0, NULL}, .zeropoint = {0, NULL}, .name = "output_output" }
  },
  /* nodes[10]：每个节点 = 层类型 + 输入张量编号列表 + 输出张量编号列表 */
  .nodes = (const stai_node_details[10]){
    /* 节点 1：Conv2D，张量 0 -> 1 */
    {.id = 1, .type = AI_LAYER_CONV2D_TYPE, .input_tensors = {1, (const int32_t[1]){0}}, .output_tensors = {1, (const int32_t[1]){1}} }, /* _conv1_Conv_output_0 */
    /* 节点 2：非线性(ReLU)，张量 1 -> 2 */
    {.id = 2, .type = AI_LAYER_NL_TYPE, .input_tensors = {1, (const int32_t[1]){1}}, .output_tensors = {1, (const int32_t[1]){2}} }, /* _relu_Relu_output_0 */
    /* 节点 3：池化(MaxPool)，张量 2 -> 3 */
    {.id = 3, .type = AI_LAYER_POOL_TYPE, .input_tensors = {1, (const int32_t[1]){2}}, .output_tensors = {1, (const int32_t[1]){3}} }, /* _pool_MaxPool_output_0 */
    /* 节点 4：Conv2D，张量 3 -> 4 */
    {.id = 4, .type = AI_LAYER_CONV2D_TYPE, .input_tensors = {1, (const int32_t[1]){3}}, .output_tensors = {1, (const int32_t[1]){4}} }, /* _conv2_Conv_output_0 */
    /* 节点 5：非线性(ReLU)，张量 4 -> 5 */
    {.id = 5, .type = AI_LAYER_NL_TYPE, .input_tensors = {1, (const int32_t[1]){4}}, .output_tensors = {1, (const int32_t[1]){5}} }, /* _relu_1_Relu_output_0 */
    /* 节点 6：池化(MaxPool)，张量 5 -> 6 */
    {.id = 6, .type = AI_LAYER_POOL_TYPE, .input_tensors = {1, (const int32_t[1]){5}}, .output_tensors = {1, (const int32_t[1]){6}} }, /* _pool_1_MaxPool_output_0 */
    /* 节点 7：Transpose，张量 6 -> 7（调整通道顺序，元素个数不变） */
    {.id = 8, .type = AI_LAYER_TRANSPOSE_TYPE, .input_tensors = {1, (const int32_t[1]){6}}, .output_tensors = {1, (const int32_t[1]){7}} }, /* _Reshape_output_0_to_chlast */
    /* 节点 8：全连接(fc1)，张量 7 -> 8 */
    {.id = 9, .type = AI_LAYER_DENSE_TYPE, .input_tensors = {1, (const int32_t[1]){7}}, .output_tensors = {1, (const int32_t[1]){8}} }, /* _fc1_Gemm_output_0 */
    /* 节点 9：非线性(ReLU)，张量 8 -> 9 */
    {.id = 10, .type = AI_LAYER_NL_TYPE, .input_tensors = {1, (const int32_t[1]){8}}, .output_tensors = {1, (const int32_t[1]){9}} }, /* _relu_2_Relu_output_0 */
    /* 节点 10：全连接(fc2)，张量 9 -> 10，即最终输出 */
    {.id = 11, .type = AI_LAYER_DENSE_TYPE, .input_tensors = {1, (const int32_t[1]){9}}, .output_tensors = {1, (const int32_t[1]){10}} } /* output */
  },
  .n_nodes = 10
};
#endif
