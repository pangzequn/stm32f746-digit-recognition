/**
  ******************************************************************************
  * @file    network.c
  * @author  AST Embedded Analytics Research Platform
  * @date    2026-10-28T17:11:51+0800
  * @brief   AI Tool Automatic Code Generator for Embedded NN computing
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  ******************************************************************************
  */

/*==============================================================================
 *  本文件由 ST Edge AI Core v4.0.1 自动生成（NetworkRuntime 12.0.1）。
 *  不要手改：重新生成会整份覆盖，所有修改请放到 ai_digit.c。
 *
 *  【怎么读这个文件】建议按下面的顺序：
 *    1. 先看 network.h 里的 STAI_NETWORK_* 宏，掌握接口契约：
 *       输入/输出形状、激活区大小、权重大小、MACC 数量；
 *    2. 再看 network_generate_report.txt 里的层表格，得到“层名 -> 输出形状”的对照；
 *    3. 回到本文件，按 "LITE_KERNEL_SECTION BEGIN <层名>" 搜索：
 *       每段结构都一样 —— 算输入指针、算输出指针、调一次 kernel；
 *    4. 需要核对参数布局时，看每段里的字节偏移（weights + N、activations + N），
 *       各段加起来必须正好等于下面两个宏：
 *       STAI_NETWORK_WEIGHTS_SIZE_BYTES / STAI_NETWORK_ACTIVATION_1_SIZE_BYTES。
 *
 *  【本文件的六个区块】
 *    A. 内部宏与版本号（STAI_MAGIC 等）
 *    B. g_network_info：给 stai_network_get_info() 用的模型元数据
 *    C. 上下文校验：_stai_network_check / _STAI_CONTEXT_ACQUIRE
 *    D. 旧平台层的数组/张量/层描述（只给 maxpool 和 transpose 用）
 *    E. stai_network_run()：10 段 kernel 调用，真正的推理
 *    F. get/set 系列 API
 *
 *  【本模型的内存布局（字节）】
 *    激活区 28,084 B（应用提供，内部复用）：
 *        0     .. 35      conv1 的 scratch（9 个 float）
 *        36    .. 323     conv2 的 scratch（72 个 float）
 *        324   ..         conv1 输出 / pool1 输出（同址覆盖）
 *                         fc1 输出、最终输出也复用这一块
 *        580   .. 835     Relu3 之后的结果，64 个 float
 *        3460  .. 6595    transpose 之后的 784 个 float（fc1 的输入）
 *        6596  ..         conv2 输出 / pool2 输出（同址覆盖）
 *        24948 .. 28083   网络输入，784 个 float
 *
 *    权重区 208,552 B（const，位于 Flash）：
 *        0      conv1 权重 288 B        288    conv1 bias 32 B
 *        320    conv2 权重 4608 B       4928   conv2 bias 64 B
 *        4992   fc1 权重 200704 B       205696 fc1 bias 256 B
 *        205952 fc2 权重 2560 B         208512 fc2 bias 40 B
 *============================================================================*/
#include "ai_lite_inspect.h"
#include "ai_platform_interface.h"
#include "layers.h"
#include "core_convert.h"
#include "network.h"
#include "network_details.h"
#include "network_data.h"
#include "stai_events.h"

#include "lite_operators.h"

#include "ai_lite_inspect.h"
/*****************************************************************************/
/*------------------------------------------------------------------------------
 * A. 内部宏
 *
 * STAI_MAGIC 会被写进上下文的第一个字段，每个 API 入口都会校验它，
 * 用来发现“传了野指针”或“上下文还没初始化就调用”的问题。
 *------------------------------------------------------------------------------*/
#define STAI_INTERNAL_API_MAJOR               (1)
#define STAI_INTERNAL_API_MINOR               (0)
#define STAI_INTERNAL_API_MICRO               (0)

#define STAI_MAGIC                            (0xB1C00100)

/*****************************************************************************/
#define _STAI_CONCAT_ARG(a, b)     a ## b
#define STAI_CONCAT(a, b)         _STAI_CONCAT_ARG(a, b)

/*!  STAI_CAST SECTION                       *********************************/
#define STAI_CAST(type, expr) \
  ((type)(expr))


/*****************************************************************************/
#define STAI_SIZE(_size) \
  ((stai_size)(_size))

/*****************************************************************************/
#define STAI_INIT_BUFFER(_flags, _size, _address) \
  { \
    .size = (_size), \
    .address = (uintptr_t)(_address), \
    .flags = (_flags), \
  }

#define STAI_INIT_TENSOR(_name, _flags, _fmt, _size_bytes, _shape, _scale, _zeropoint) \
  { \
    .size_bytes = (_size_bytes), \
    .flags = (_flags), \
    .format = (stai_format)(_fmt), \
    .shape = STAI_PACK(_shape), \
    .scale = STAI_PACK(_scale), \
    .zeropoint = STAI_PACK(_zeropoint), \
    .name = (_name) \
  }

#define STAI_INIT_ARRAY(_size, _ptr) \
  { .size = STAI_SIZE(_size), .data = STAI_PACK(_ptr) }


#define STAI_CAST_ARRAY(_type, _size, _ptr) \
  { .size = STAI_SIZE(_size), .data = (_type)STAI_PACK(_ptr) }


#define STAI_DECLARE_ARRAY(_type, _size, ...) \
  { .size = STAI_SIZE(_size), .data = (_type[_size]) { STAI_PACK(__VA_ARGS__) } }


#define STAI_EMPTY_ARRAY() \
  { .size = 0, .data = NULL }


#define STAI_INIT_VERSION(_major, _minor, _micro) \
  { .major = (_major), .minor = (_minor), .micro = (_micro), .reserved = 0x0 }

/*****************************************************************************/
/**  Getters and setters  **/

#define STAI_GET_ARRAY_SIZE(nd_array) \
  (nd_array.size)


#define STAI_GET_ARRAY_ELEM(nd_array, pos) \
  (nd_array.data[(pos)])

#define _STAI_SET_ERROR(net_ctx, cond, value, exit) { \
  if (!(net_ctx)) { return STAI_ERROR_NETWORK_INVALID_CONTEXT_HANDLE; } \
  if (((uintptr_t)net_ctx) & (_STAI_CONTEXT_ALIGNMENT-1)) { return STAI_ERROR_NETWORK_INVALID_CONTEXT_ALIGNMENT; } \
  if (((value) >= STAI_ERROR_GENERIC) && (cond)) { \
    if ((net_ctx)->_return_code == STAI_SUCCESS) { \
      (net_ctx)->_return_code = (value); \
    } \
    return (exit); \
  } \
}

/*****************************************************************************/
/* TODO REMOVE THESE TWO MACROS */
#define STAI_EVENT_NODE_START_CB
#define STAI_EVENT_NODE_STOP_CB

#ifdef STAI_EVENT_NODE_START_CB
#ifndef _STAI_NETWORK_EVENT_NODE_START_CB
  #define _STAI_NETWORK_EVENT_NODE_START_CB(_node_id, _buffers_size, ...) \
  if (net_ctx->_callback) { \
    const stai_event_node_start_stop _start_event = { \
      .node_id=(_node_id), \
      .buffers={ \
        .size=(_buffers_size), \
        .data=(stai_ptr const*)(const stai_ptr[_buffers_size])STAI_PACK(__VA_ARGS__) \
      } \
    }; \
    net_ctx->_callback(net_ctx->_callback_cookie, STAI_EVENT_NODE_START, (const void*)&_start_event); \
  }
#endif
#else
  #define _STAI_NETWORK_EVENT_NODE_START_CB(_node_id, _buffers_size, ...) \
    do { /* _STAI_NETWORK_EVENT_NODE_START_CB() */ } while(0);
#endif      /* STAI_EVENT_NODE_START_CB */

#ifdef STAI_EVENT_NODE_STOP_CB
#ifndef _STAI_NETWORK_EVENT_NODE_STOP_CB
  #define _STAI_NETWORK_EVENT_NODE_STOP_CB(_node_id, _buffers_size, ...) \
  if (net_ctx->_callback) { \
    const stai_event_node_start_stop _stop_event = { \
      .node_id=(_node_id), \
      .buffers={ \
        .size=(_buffers_size), \
        .data=(stai_ptr const*)(stai_ptr[_buffers_size])STAI_PACK(__VA_ARGS__) \
      } \
    }; \
    net_ctx->_callback(net_ctx->_callback_cookie, STAI_EVENT_NODE_STOP, (const void*)&_stop_event); \
  }
#endif
#else
  #define _STAI_NETWORK_EVENT_NODE_STOP_CB(_node_id, _buffers_size, ...) \
    do { /* _STAI_NETWORK_EVENT_NODE_STOP_CB() */ } while(0);
#endif      /* STAI_EVENT_NODE_STOP_CB */


/*****************************************************************************/
#define _STAI_NETWORK_MODEL_SIGNATURE     "0x5f9784ef26eab8b53249353f54089dc8"
#define _STAI_NETWORK_DATETIME            "2026-10-28T17:11:51+0800"
#define _STAI_NETWORK_COMPILE_DATETIME    __DATE__ " " __TIME__

#define _STAI_CONTEXT_ALIGNMENT        STAI_NETWORK_CONTEXT_ALIGNMENT

/*****************************************************************************/
/* 生成器把“激活缓冲区”留空（NULL），要求应用自己提供 28,084 字节的缓冲区，
 * 再通过 stai_network_set_activations() 绑定进来。
 * 这就是 ai_digit.c 里必须先准备 s_activations[] 的原因。 */
#define g_network_activations_1     (NULL)




/*------------------------------------------------------------------------------
 * B. 模型元数据（仅当定义了 HAVE_NETWORK_INFO 时编译）
 *
 * 记录模型名、工具/运行时版本、MACC 数量以及输入/输出/激活/权重的描述。
 * 只服务于 stai_network_get_info() 和调试工具，推理过程本身不读它。
 *------------------------------------------------------------------------------*/
#if defined(HAVE_NETWORK_INFO)
/*****************************************************************************/
static const stai_network_info g_network_info = {
  .model_signature = _STAI_NETWORK_MODEL_SIGNATURE,
  .c_compile_datetime = _STAI_NETWORK_COMPILE_DATETIME,
  .c_model_name = STAI_NETWORK_MODEL_NAME,
  .c_model_datetime = _STAI_NETWORK_DATETIME,
  .c_model_signature = 0x0,
  .runtime_version = STAI_INIT_VERSION(12, 0, 1),
  .tool_version = STAI_INIT_VERSION(4, 0, 1),
  .api_version = STAI_INIT_VERSION(1, 0, 0),
  .n_macc = STAI_NETWORK_MACC_NUM,
  .n_nodes = STAI_NETWORK_NODES_NUM,
  .flags = STAI_NETWORK_FLAGS,
  .n_inputs = STAI_NETWORK_IN_NUM,
  .n_outputs = STAI_NETWORK_OUT_NUM,
  .n_activations = STAI_NETWORK_ACTIVATIONS_NUM,
  .n_weights = STAI_NETWORK_WEIGHTS_NUM,
  .n_states = STAI_NETWORK_STATES_NUM,
  .inputs = (stai_tensor[STAI_NETWORK_IN_NUM]) {
    STAI_INIT_TENSOR(
      STAI_NETWORK_IN_1_NAME,
      STAI_NETWORK_IN_1_FLAGS,
      STAI_NETWORK_IN_1_FORMAT,
      STAI_NETWORK_IN_1_SIZE_BYTES,
      STAI_DECLARE_ARRAY(int32_t, 4, 1, 1, 28, 28),
      STAI_EMPTY_ARRAY(),
      STAI_EMPTY_ARRAY()),
    },
    .outputs = (stai_tensor[STAI_NETWORK_OUT_NUM]) {
    STAI_INIT_TENSOR(
      STAI_NETWORK_OUT_1_NAME,
      STAI_NETWORK_OUT_1_FLAGS,
      STAI_NETWORK_OUT_1_FORMAT,
      STAI_NETWORK_OUT_1_SIZE_BYTES,
      STAI_DECLARE_ARRAY(int32_t, 2, 1, 10),
      STAI_EMPTY_ARRAY(),
      STAI_EMPTY_ARRAY()),
    },
  .activations = (stai_tensor[STAI_NETWORK_ACTIVATIONS_NUM]) {
    STAI_INIT_TENSOR(
      (NULL),
      STAI_NETWORK_ACTIVATION_1_FLAGS,
      STAI_FORMAT_U8,
      STAI_NETWORK_ACTIVATION_1_SIZE_BYTES,
      STAI_DECLARE_ARRAY(int32_t, 1, 28084),
      STAI_EMPTY_ARRAY(),
      STAI_EMPTY_ARRAY()),
    },
  .weights = (stai_tensor[STAI_NETWORK_WEIGHTS_NUM]) {
    STAI_INIT_TENSOR(
      (NULL),
      STAI_NETWORK_WEIGHT_1_FLAGS,
      STAI_FORMAT_U8,
      STAI_NETWORK_WEIGHT_1_SIZE_BYTES,
      STAI_DECLARE_ARRAY(int32_t, 1, 208552),
      STAI_EMPTY_ARRAY(),
      STAI_EMPTY_ARRAY()),
    },

  .states = NULL
};
#endif

/*------------------------------------------------------------------------------
 * C. 上下文校验
 *
 * 每个 API 入口都先展开 _STAI_CONTEXT_ACQUIRE：把句柄转成上下文指针并校验 magic；
 * 再用 _STAI_SET_ERROR 逐项检查参数，条件成立就设置错误码并立即 return。
 *------------------------------------------------------------------------------*/
#define _STAI_CONTEXT_ACQUIRE(_net_ctx, _net_handle) \
  _stai_network_context* _net_ctx = (_stai_network_context*)(_net_handle); \
  STAI_ASSERT(_net_ctx != NULL) \
  _STAI_SET_ERROR(_net_ctx, _net_ctx->_magic != STAI_MAGIC, \
                  STAI_ERROR_NETWORK_INVALID_CONTEXT_HANDLE, _net_ctx->_return_code)


/*****************************************************************************/
/* 根据四个指针数组是否为空，重新计算 _flags 中的
 * ACTIVATIONS / INPUTS / OUTPUTS / WEIGHTS 位。
 * set_activations() 之后必须调用它（生成代码已经调了），run() 会检查这些位，
 * 缺一个就返回对应的 STAI_ERROR_NETWORK_INVALID_*_PTR。 */
static
void _stai_network_check(_stai_network_context* net_ctx)
{
  stai_size idx;

// Check activations status
  for (idx=0; idx<STAI_NETWORK_ACTIVATIONS_NUM; idx++) {
    if (net_ctx->_activations[idx] == NULL) break;
  }
  net_ctx->_flags |= (idx == STAI_NETWORK_ACTIVATIONS_NUM) ? STAI_FLAG_ACTIVATIONS : STAI_FLAG_NONE;
// Check inputs status
  for (idx=0; idx<STAI_NETWORK_IN_NUM; idx++) {
    if (net_ctx->_inputs[idx] == NULL) break;
  }
  net_ctx->_flags |= (idx == STAI_NETWORK_IN_NUM) ? STAI_FLAG_INPUTS : STAI_FLAG_NONE;

  // Check outputs status
  for (idx=0; idx<STAI_NETWORK_OUT_NUM; idx++) {
    if (net_ctx->_outputs[idx] == NULL) break;
  }
  net_ctx->_flags |= (idx == STAI_NETWORK_OUT_NUM) ? STAI_FLAG_OUTPUTS : STAI_FLAG_NONE;

// Check weights status
  for (idx=0; idx<STAI_NETWORK_WEIGHTS_NUM; idx++) {
    if (net_ctx->_weights[idx] == NULL) break;
  }
  net_ctx->_flags |= (idx == STAI_NETWORK_WEIGHTS_NUM) ? STAI_FLAG_WEIGHTS : STAI_FLAG_NONE;
STAI_PRINT("  [_stai_network_check] flags: 0x%08x\n", net_ctx->_flags)
}


/*****************************************************************************/
/*------------------------------------------------------------------------------
 * 初始化：把一份“默认上下文”深拷贝到应用提供的内存里。
 *   - 激活区：填 NULL，必须由 set_activations() 补上（见 B 区上方的说明）；
 *   - 权重：直接指向 Flash 里的 g_network_weights_array，不复制、不占 RAM；
 *   - 输入/输出：也先填 NULL，等 set_activations() 根据激活区基址算出来。
 * 返回 STAI_SUCCESS 表示成功；失败可用 stai_network_get_error() 查原因。
 *------------------------------------------------------------------------------*/
STAI_API_ENTRY
stai_return_code stai_network_init(
  stai_network* network)
{
  /* Memory where to store internal context is provided by applications as a raw byte buffer */
  _stai_network_context* net_ctx = (_stai_network_context*)(network);
  net_ctx->_return_code = STAI_SUCCESS;
  STAI_PRINT("[Entering Network Init] network(%p) context_size(%d)\n", net_ctx, (int32_t)sizeof(_stai_network_context))

  _STAI_SET_ERROR(net_ctx, STAI_NETWORK_CONTEXT_SIZE != sizeof(_stai_network_context),
                 STAI_ERROR_NETWORK_INVALID_CONTEXT_SIZE, net_ctx->_return_code)

  {
    const _stai_network_context _network_context = {
      ._magic = STAI_MAGIC,
      ._signature = STAI_NETWORK_MODEL_SIGNATURE,
      ._flags = STAI_NETWORK_FLAGS,
      ._return_code = STAI_SUCCESS,
      ._callback = NULL,
      ._callback_cookie = NULL,
      ._activations = {
      (stai_ptr)g_network_activations_1
      },
      ._weights = {
      (stai_ptr)g_network_weights_array
      },
      ._inputs = {
    NULL},
      ._outputs = {
    NULL},
    };

    // Deep copy of internal context to opaque buffer provided by app
    *net_ctx = _network_context;

    _stai_network_check(net_ctx);
  }

  return net_ctx->_return_code;
}


/* 反初始化：只把 flags 恢复到初始值。
 * 权重和激活区都是应用提供的内存，这里不释放（也不允许释放）。 */
STAI_API_ENTRY
stai_return_code stai_network_deinit(
  stai_network* network)
{
  _STAI_CONTEXT_ACQUIRE(net_ctx, network)

  /*  Reset flags to initial state  */
  net_ctx->_flags = STAI_NETWORK_FLAGS;
  return net_ctx->_return_code;
}

/*****************************************************************************/





/* Array#0 */
/*------------------------------------------------------------------------------
 * D. 旧平台层的“数组 / 张量 / 张量链 / 层”描述对象
 *
 * conv 和 dense 是直接调 forward_lite_* kernel 的，不需要这些对象；
 * 只有 maxpool 和 transpose 走平台层的 forward_mp() / forward_transpose()，
 * 所以下面有 5 个数组、5 个张量、3 条张量链、3 个层对象。
 * 这些对象里的 data 指针并不写死，运行时由 hybrid 前向函数按偏移量填进去。
 *------------------------------------------------------------------------------*/
AI_ARRAY_OBJ_DECLARE(
  _relu_Relu_output_0_output_array, AI_ARRAY_FORMAT_FLOAT,
  NULL, NULL, 6272, AI_STATIC)

/* Array#1 */
AI_ARRAY_OBJ_DECLARE(
  _pool_MaxPool_output_0_output_array, AI_ARRAY_FORMAT_FLOAT,
  NULL, NULL, 1568, AI_STATIC)

/* Array#2 */
AI_ARRAY_OBJ_DECLARE(
  _relu_1_Relu_output_0_output_array, AI_ARRAY_FORMAT_FLOAT,
  NULL, NULL, 3136, AI_STATIC)

/* Array#3 */
AI_ARRAY_OBJ_DECLARE(
  _pool_1_MaxPool_output_0_output_array, AI_ARRAY_FORMAT_FLOAT,
  NULL, NULL, 784, AI_STATIC)

/* Array#4 */
AI_ARRAY_OBJ_DECLARE(
  _Reshape_output_0_to_chlast_output_array, AI_ARRAY_FORMAT_FLOAT,
  NULL, NULL, 784, AI_STATIC)



/* Tensor #0 */
/* 张量对象：把“数组 + 形状 + 格式”组合成平台层能识别的张量。
 * 这里只做描述，真正指向哪块内存要等 hybrid 前向函数设置 data。 */
AI_TENSOR_OBJ_DECLARE(
  _pool_MaxPool_output_0_output, AI_STATIC,
  14, 0x0,
  AI_SHAPE_INIT(4, 1, 8, 14, 14), AI_STRIDE_INIT(4, 4, 4, 32, 448),
  1, &_pool_MaxPool_output_0_output_array, NULL)

/* Tensor #1 */
AI_TENSOR_OBJ_DECLARE(
  _relu_Relu_output_0_output, AI_STATIC,
  17, 0x0,
  AI_SHAPE_INIT(4, 1, 8, 28, 28), AI_STRIDE_INIT(4, 4, 4, 32, 896),
  1, &_relu_Relu_output_0_output_array, NULL)

/* Tensor #2 */
AI_TENSOR_OBJ_DECLARE(
  _pool_1_MaxPool_output_0_output, AI_STATIC,
  13, 0x0,
  AI_SHAPE_INIT(4, 1, 16, 7, 7), AI_STRIDE_INIT(4, 4, 4, 64, 448),
  1, &_pool_1_MaxPool_output_0_output_array, NULL)

/* Tensor #3 */
AI_TENSOR_OBJ_DECLARE(
  _relu_1_Relu_output_0_output, AI_STATIC,
  15, 0x0,
  AI_SHAPE_INIT(4, 1, 16, 14, 14), AI_STRIDE_INIT(4, 4, 4, 64, 896),
  1, &_relu_1_Relu_output_0_output_array, NULL)

/* Tensor #4 */
AI_TENSOR_OBJ_DECLARE(
  _Reshape_output_0_to_chlast_output, AI_STATIC,
  0, 0x0,
  AI_SHAPE_INIT(4, 1, 7, 7, 16), AI_STRIDE_INIT(4, 4, 4, 28, 196),
  1, &_Reshape_output_0_to_chlast_output_array, NULL)


/* 张量链：描述“这个层从哪个张量读、写到哪个张量”，相当于层的接线表。 */
AI_TENSOR_CHAIN_OBJ_DECLARE(
  _pool_MaxPool_output_0_chain, AI_STATIC_CONST, 4,
  AI_TENSOR_LIST_OBJ_INIT(AI_FLAG_NONE, 1, &_relu_Relu_output_0_output),
  AI_TENSOR_LIST_OBJ_INIT(AI_FLAG_NONE, 1, &_pool_MaxPool_output_0_output),
  AI_TENSOR_LIST_OBJ_EMPTY,
  AI_TENSOR_LIST_OBJ_EMPTY
)

/* 层对象：把层类型（POOL / TRANSPOSE）、前向函数、张量链组合在一起。
 * 参数里的 out_mapping 描述内存排布（哪个维度是通道/宽/高）。 */
AI_LAYER_OBJ_DECLARE(
  _pool_MaxPool_output_0_layer, 3,
  POOL_TYPE, 0x0, NULL,
  pool, forward_mp,
  &_pool_MaxPool_output_0_chain,
  NULL, &_pool_MaxPool_output_0_layer, AI_STATIC, 
  .pool_size = AI_SHAPE_2D_INIT(2, 2), 
  .pool_stride = AI_SHAPE_2D_INIT(2, 2), 
  .pool_pad = AI_SHAPE_INIT(4, 0, 0, 0, 0), 
)

AI_TENSOR_CHAIN_OBJ_DECLARE(
  _pool_1_MaxPool_output_0_chain, AI_STATIC_CONST, 4,
  AI_TENSOR_LIST_OBJ_INIT(AI_FLAG_NONE, 1, &_relu_1_Relu_output_0_output),
  AI_TENSOR_LIST_OBJ_INIT(AI_FLAG_NONE, 1, &_pool_1_MaxPool_output_0_output),
  AI_TENSOR_LIST_OBJ_EMPTY,
  AI_TENSOR_LIST_OBJ_EMPTY
)

AI_LAYER_OBJ_DECLARE(
  _pool_1_MaxPool_output_0_layer, 6,
  POOL_TYPE, 0x0, NULL,
  pool, forward_mp,
  &_pool_1_MaxPool_output_0_chain,
  NULL, &_pool_1_MaxPool_output_0_layer, AI_STATIC, 
  .pool_size = AI_SHAPE_2D_INIT(2, 2), 
  .pool_stride = AI_SHAPE_2D_INIT(2, 2), 
  .pool_pad = AI_SHAPE_INIT(4, 0, 0, 0, 0), 
)

AI_TENSOR_CHAIN_OBJ_DECLARE(
  _Reshape_output_0_to_chlast_chain, AI_STATIC_CONST, 4,
  AI_TENSOR_LIST_OBJ_INIT(AI_FLAG_NONE, 1, &_pool_1_MaxPool_output_0_output),
  AI_TENSOR_LIST_OBJ_INIT(AI_FLAG_NONE, 1, &_Reshape_output_0_to_chlast_output),
  AI_TENSOR_LIST_OBJ_EMPTY,
  AI_TENSOR_LIST_OBJ_EMPTY
)

AI_LAYER_OBJ_DECLARE(
  _Reshape_output_0_to_chlast_layer, 8,
  TRANSPOSE_TYPE, 0x0, NULL,
  transpose, forward_transpose,
  &_Reshape_output_0_to_chlast_chain,
  NULL, &_Reshape_output_0_to_chlast_layer, AI_STATIC, 
  .out_mapping = AI_SHAPE_INIT(6, AI_SHAPE_IN_CHANNEL, AI_SHAPE_WIDTH, AI_SHAPE_HEIGHT, AI_SHAPE_CHANNEL, AI_SHAPE_DEPTH, AI_SHAPE_EXTENSION), 
)
/**  Hybrid layers declarations section  *************************************/
/* pool1 / pool2 的 hybrid 前向：先把输入、输出数组的 data 指针指向激活区偏移，
 * 再交给平台层 forward_mp()。输入输出同址，属于“原地池化”。 */
void forward_lite_mp__pool_MaxPool_output_0(_stai_network_context* net_ctx)
{
  _relu_Relu_output_0_output_array.data = AI_PTR(net_ctx->_activations[0] + 324);
  _relu_Relu_output_0_output_array.data_start = AI_PTR(net_ctx->_activations[0] + 324);
  _pool_MaxPool_output_0_output_array.data = AI_PTR(net_ctx->_activations[0] + 324);
  _pool_MaxPool_output_0_output_array.data_start = AI_PTR(net_ctx->_activations[0] + 324);
  _STAI_NETWORK_EVENT_NODE_START_CB(3, 1, { _relu_Relu_output_0_output.data->data});
  forward_mp(&_pool_MaxPool_output_0_layer);
  _STAI_NETWORK_EVENT_NODE_STOP_CB(3, 1, { _pool_MaxPool_output_0_output.data->data});
}
void forward_lite_mp__pool_1_MaxPool_output_0(_stai_network_context* net_ctx)
{
  _relu_1_Relu_output_0_output_array.data = AI_PTR(net_ctx->_activations[0] + 6596);
  _relu_1_Relu_output_0_output_array.data_start = AI_PTR(net_ctx->_activations[0] + 6596);
  _pool_1_MaxPool_output_0_output_array.data = AI_PTR(net_ctx->_activations[0] + 324);
  _pool_1_MaxPool_output_0_output_array.data_start = AI_PTR(net_ctx->_activations[0] + 324);
  _STAI_NETWORK_EVENT_NODE_START_CB(6, 1, { _relu_1_Relu_output_0_output.data->data});
  forward_mp(&_pool_1_MaxPool_output_0_layer);
  _STAI_NETWORK_EVENT_NODE_STOP_CB(6, 1, { _pool_1_MaxPool_output_0_output.data->data});
}
/* NCHW -> 通道在最后的转换。
 * PyTorch 的卷积输出是 NCHW，而 dense 层要求 784 个值按“通道在最后”的顺序排列，
 * 所以这里把 16x7x7 重排成 784 个连续 float 供 fc1 使用。 */
void forward_lite_transpose__Reshape_output_0_to_chlast(_stai_network_context* net_ctx)
{
  _pool_1_MaxPool_output_0_output_array.data = AI_PTR(net_ctx->_activations[0] + 324);
  _pool_1_MaxPool_output_0_output_array.data_start = AI_PTR(net_ctx->_activations[0] + 324);
  _Reshape_output_0_to_chlast_output_array.data = AI_PTR(net_ctx->_activations[0] + 3460);
  _Reshape_output_0_to_chlast_output_array.data_start = AI_PTR(net_ctx->_activations[0] + 3460);
  _STAI_NETWORK_EVENT_NODE_START_CB(8, 1, { _pool_1_MaxPool_output_0_output.data->data});
  forward_transpose(&_Reshape_output_0_to_chlast_layer);
  _STAI_NETWORK_EVENT_NODE_STOP_CB(8, 1, { _Reshape_output_0_to_chlast_output.data->data});
}

/*****************************************************************************/


static const ai_u32 _conv1_Conv_output_0_t_in_0_shape_ch_const_u32 = 1;
static const ai_u32 _conv1_Conv_output_0_t_out_0_shape_ch_const_u32 = 8;
static const ai_u32 _conv1_Conv_output_0_t_in_0_shape_w_const_u32 = 28;
static const ai_u32 _conv1_Conv_output_0_t_in_0_shape_h_const_u32 = 28;
static const ai_u32 _conv1_Conv_output_0_t_out_0_shape_w_const_u32 = 28;
static const ai_u32 _conv1_Conv_output_0_t_out_0_shape_h_const_u32 = 28;
static const ai_u32 _conv1_Conv_output_0_t_weight_0_shape_w_const_u32 = 3;
static const ai_u32 _conv1_Conv_output_0_t_weight_0_shape_h_const_u32 = 3;
static const ai_i32 _conv1_Conv_output_0_l_pad_W_0_const_s32 = 1;
static const ai_i32 _conv1_Conv_output_0_l_pad_H_0_const_s32 = 1;
static const ai_u16 _conv1_Conv_output_0_l_stride_1_const_u16 = 1;
static const ai_u16 _conv1_Conv_output_0_l_stride_0_const_u16 = 1;
static const ai_u16 _conv1_Conv_output_0_l_dilation_H_const_u16 = 1;
static const ai_u16 _conv1_Conv_output_0_l_dilation_W_const_u16 = 1;

static const ai_i32 _relu_Relu_output_0_t_in_0_shape_ch_h_w_prod_const_s32 = 6272;


static const ai_u32 _conv2_Conv_output_0_t_in_0_shape_ch_const_u32 = 8;
static const ai_u32 _conv2_Conv_output_0_t_out_0_shape_ch_const_u32 = 16;
static const ai_u32 _conv2_Conv_output_0_t_in_0_shape_w_const_u32 = 14;
static const ai_u32 _conv2_Conv_output_0_t_in_0_shape_h_const_u32 = 14;
static const ai_u32 _conv2_Conv_output_0_t_out_0_shape_w_const_u32 = 14;
static const ai_u32 _conv2_Conv_output_0_t_out_0_shape_h_const_u32 = 14;
static const ai_u32 _conv2_Conv_output_0_t_weight_0_shape_w_const_u32 = 3;
static const ai_u32 _conv2_Conv_output_0_t_weight_0_shape_h_const_u32 = 3;
static const ai_i32 _conv2_Conv_output_0_l_pad_W_0_const_s32 = 1;
static const ai_i32 _conv2_Conv_output_0_l_pad_H_0_const_s32 = 1;
static const ai_u16 _conv2_Conv_output_0_l_stride_1_const_u16 = 1;
static const ai_u16 _conv2_Conv_output_0_l_stride_0_const_u16 = 1;
static const ai_u16 _conv2_Conv_output_0_l_dilation_H_const_u16 = 1;
static const ai_u16 _conv2_Conv_output_0_l_dilation_W_const_u16 = 1;

static const ai_i32 _relu_1_Relu_output_0_t_in_0_shape_ch_h_w_prod_const_s32 = 3136;




static const ai_i32 _relu_2_Relu_output_0_t_in_0_shape_ch_prod_const_s32 = 64;

/*==============================================================================
 * E. 推理主流程：按顺序执行 10 段 kernel 调用
 *
 * 每段的模板都相同：
 *   1) 从 _inputs / _activations / _weights 加固定字节偏移，得到输入/输出/权重指针；
 *   2) 调一次 forward_lite_* kernel（实现位于 NetworkRuntime1201_CM7_GCC.a）；
 *   3) 发 node start/stop 事件（本工程没注册 callback，编译后是空操作）。
 *
 * mode 参数当前版本未使用：生成代码里 STAI_UNUSED(mode)，一律同步执行。
 *============================================================================*/
STAI_API_ENTRY
stai_return_code stai_network_run(
  stai_network* network,
  const stai_run_mode mode)
{
   STAI_UNUSED(mode)
  _STAI_CONTEXT_ACQUIRE(net_ctx, network)

  _STAI_SET_ERROR(net_ctx, (net_ctx->_flags & STAI_FLAG_ACTIVATIONS) != STAI_FLAG_ACTIVATIONS,
        STAI_ERROR_NETWORK_INVALID_ACTIVATIONS_PTR, net_ctx->_return_code)

  _STAI_SET_ERROR(net_ctx, (net_ctx->_flags & STAI_FLAG_INPUTS) != STAI_FLAG_INPUTS,
                  STAI_ERROR_NETWORK_INVALID_IN_PTR, net_ctx->_return_code)
  _STAI_SET_ERROR(net_ctx, (net_ctx->_flags & STAI_FLAG_OUTPUTS) != STAI_FLAG_OUTPUTS,
                  STAI_ERROR_NETWORK_INVALID_OUT_PTR, net_ctx->_return_code)

  _STAI_SET_ERROR(net_ctx, (net_ctx->_flags & STAI_FLAG_WEIGHTS) != STAI_FLAG_WEIGHTS,
                  STAI_ERROR_NETWORK_INVALID_WEIGHTS_PTR, net_ctx->_return_code)


/* 第 1 层 Conv1：1x28x28 -> 8x28x28，3x3，pad=1，stride=1
 *   输入   : _inputs[0]（= 激活区 + 24948）
 *   输出   : 激活区 + 324
 *   权重   : +0    （8*1*3*3 个 float = 288 B）
 *   bias   : +288  （8 个 float = 32 B）
 *   scratch: +0    （卷积临时区） */
  /* LITE_KERNEL_SECTION BEGIN _conv1_Conv_output_0 */
  {
      const ai_float* _conv1_Conv_output_0_t_in_0_ptr_const_f32 = (ai_float*)(net_ctx->_inputs[0] + 0);
    ai_float* _conv1_Conv_output_0_t_out_0_ptr_f32 = (ai_float*)(net_ctx->_activations[0] + 324);
    const ai_u8* _conv1_Conv_output_0_t_weight_0_ptr_const_u8 = (ai_u8*)(net_ctx->_weights[0] + 0);
    const ai_u8* _conv1_Conv_output_0_t_weight_1_ptr_const_u8 = (ai_u8*)(net_ctx->_weights[0] + 288);
    ai_float* _conv1_Conv_output_0_t_scratch_0_ptr_f32 = (ai_float*)(net_ctx->_activations[0] + 0);
  
  _STAI_NETWORK_EVENT_NODE_START_CB(1, 1, {(stai_ptr) _conv1_Conv_output_0_t_in_0_ptr_const_f32});
    
  forward_lite_conv2d_if32of32wf32(_conv1_Conv_output_0_t_in_0_ptr_const_f32, _conv1_Conv_output_0_t_out_0_ptr_f32, _conv1_Conv_output_0_t_weight_0_ptr_const_u8, _conv1_Conv_output_0_t_weight_1_ptr_const_u8, _conv1_Conv_output_0_t_scratch_0_ptr_f32, _conv1_Conv_output_0_t_in_0_shape_ch_const_u32, _conv1_Conv_output_0_t_out_0_shape_ch_const_u32, _conv1_Conv_output_0_t_in_0_shape_w_const_u32, _conv1_Conv_output_0_t_in_0_shape_h_const_u32, _conv1_Conv_output_0_t_out_0_shape_w_const_u32, _conv1_Conv_output_0_t_out_0_shape_h_const_u32, _conv1_Conv_output_0_t_weight_0_shape_w_const_u32, _conv1_Conv_output_0_t_weight_0_shape_h_const_u32, _conv1_Conv_output_0_l_pad_W_0_const_s32, _conv1_Conv_output_0_l_pad_H_0_const_s32, _conv1_Conv_output_0_l_stride_1_const_u16, _conv1_Conv_output_0_l_stride_0_const_u16, 3, 3, _conv1_Conv_output_0_l_dilation_H_const_u16, _conv1_Conv_output_0_l_dilation_W_const_u16, (ai_size)(1));
    
  _STAI_NETWORK_EVENT_NODE_STOP_CB(1, 1, {(stai_ptr) _conv1_Conv_output_0_t_out_0_ptr_f32});
  }
  /* LITE_KERNEL_SECTION END _conv1_Conv_output_0 */
/* 第 2 层 ReLU1：原地作用在 +324，共 8*28*28 = 6272 个 float。
 * 输入输出同一个地址，省内存。 */
  /* LITE_KERNEL_SECTION BEGIN _relu_Relu_output_0 */
  {
      ai_handle _relu_Relu_output_0_t_out_0_ptr_handle = (ai_handle)(net_ctx->_activations[0] + 324);
    const ai_handle _relu_Relu_output_0_t_in_0_ptr_const_handle = (ai_handle)(net_ctx->_activations[0] + 324);
  
  _STAI_NETWORK_EVENT_NODE_START_CB(2, 1, {(stai_ptr) _relu_Relu_output_0_t_in_0_ptr_const_handle});
    
  forward_lite_nl_relu_if32of32(_relu_Relu_output_0_t_out_0_ptr_handle, _relu_Relu_output_0_t_in_0_ptr_const_handle, _relu_Relu_output_0_t_in_0_shape_ch_h_w_prod_const_s32, NULL);
    
  _STAI_NETWORK_EVENT_NODE_STOP_CB(2, 1, {(stai_ptr) _relu_Relu_output_0_t_out_0_ptr_handle});
  }
  /* LITE_KERNEL_SECTION END _relu_Relu_output_0 */
/* 第 3 层 MaxPool1：8x28x28 -> 8x14x14，窗口 2x2，步长 2，原地。 */
  /* LITE_KERNEL_SECTION BEGIN _pool_MaxPool_output_0 */
  {
    
  forward_lite_mp__pool_MaxPool_output_0(net_ctx);
  }
  /* LITE_KERNEL_SECTION END _pool_MaxPool_output_0 */
/* 第 4 层 Conv2：8x14x14 -> 16x14x14，3x3，pad=1
 *   输入   : +324
 *   输出   : +6596
 *   权重   : +320    （16*8*3*3*4 = 4608 B）
 *   bias   : +4928   （16 个 float = 64 B）
 *   scratch: +36     （72 个 float = 288 B） */
  /* LITE_KERNEL_SECTION BEGIN _conv2_Conv_output_0 */
  {
      const ai_float* _conv2_Conv_output_0_t_in_0_ptr_const_f32 = (ai_float*)(net_ctx->_activations[0] + 324);
    ai_float* _conv2_Conv_output_0_t_out_0_ptr_f32 = (ai_float*)(net_ctx->_activations[0] + 6596);
    const ai_u8* _conv2_Conv_output_0_t_weight_0_ptr_const_u8 = (ai_u8*)(net_ctx->_weights[0] + 320);
    const ai_u8* _conv2_Conv_output_0_t_weight_1_ptr_const_u8 = (ai_u8*)(net_ctx->_weights[0] + 4928);
    ai_float* _conv2_Conv_output_0_t_scratch_0_ptr_f32 = (ai_float*)(net_ctx->_activations[0] + 36);
  
  _STAI_NETWORK_EVENT_NODE_START_CB(4, 1, {(stai_ptr) _conv2_Conv_output_0_t_in_0_ptr_const_f32});
    
  forward_lite_conv2d_if32of32wf32(_conv2_Conv_output_0_t_in_0_ptr_const_f32, _conv2_Conv_output_0_t_out_0_ptr_f32, _conv2_Conv_output_0_t_weight_0_ptr_const_u8, _conv2_Conv_output_0_t_weight_1_ptr_const_u8, _conv2_Conv_output_0_t_scratch_0_ptr_f32, _conv2_Conv_output_0_t_in_0_shape_ch_const_u32, _conv2_Conv_output_0_t_out_0_shape_ch_const_u32, _conv2_Conv_output_0_t_in_0_shape_w_const_u32, _conv2_Conv_output_0_t_in_0_shape_h_const_u32, _conv2_Conv_output_0_t_out_0_shape_w_const_u32, _conv2_Conv_output_0_t_out_0_shape_h_const_u32, _conv2_Conv_output_0_t_weight_0_shape_w_const_u32, _conv2_Conv_output_0_t_weight_0_shape_h_const_u32, _conv2_Conv_output_0_l_pad_W_0_const_s32, _conv2_Conv_output_0_l_pad_H_0_const_s32, _conv2_Conv_output_0_l_stride_1_const_u16, _conv2_Conv_output_0_l_stride_0_const_u16, 3, 3, _conv2_Conv_output_0_l_dilation_H_const_u16, _conv2_Conv_output_0_l_dilation_W_const_u16, (ai_size)(1));
    
  _STAI_NETWORK_EVENT_NODE_STOP_CB(4, 1, {(stai_ptr) _conv2_Conv_output_0_t_out_0_ptr_f32});
  }
  /* LITE_KERNEL_SECTION END _conv2_Conv_output_0 */
/* 第 5 层 ReLU2：原地作用在 +6596，共 16*14*14 = 3136 个 float。 */
  /* LITE_KERNEL_SECTION BEGIN _relu_1_Relu_output_0 */
  {
      ai_handle _relu_1_Relu_output_0_t_out_0_ptr_handle = (ai_handle)(net_ctx->_activations[0] + 6596);
    const ai_handle _relu_1_Relu_output_0_t_in_0_ptr_const_handle = (ai_handle)(net_ctx->_activations[0] + 6596);
  
  _STAI_NETWORK_EVENT_NODE_START_CB(5, 1, {(stai_ptr) _relu_1_Relu_output_0_t_in_0_ptr_const_handle});
    
  forward_lite_nl_relu_if32of32(_relu_1_Relu_output_0_t_out_0_ptr_handle, _relu_1_Relu_output_0_t_in_0_ptr_const_handle, _relu_1_Relu_output_0_t_in_0_shape_ch_h_w_prod_const_s32, NULL);
    
  _STAI_NETWORK_EVENT_NODE_STOP_CB(5, 1, {(stai_ptr) _relu_1_Relu_output_0_t_out_0_ptr_handle});
  }
  /* LITE_KERNEL_SECTION END _relu_1_Relu_output_0 */
/* 第 6 层 MaxPool2：16x14x14 -> 16x7x7，窗口 2x2，原地。 */
  /* LITE_KERNEL_SECTION BEGIN _pool_1_MaxPool_output_0 */
  {
    
  forward_lite_mp__pool_1_MaxPool_output_0(net_ctx);
  }
  /* LITE_KERNEL_SECTION END _pool_1_MaxPool_output_0 */
/* 第 7 层 Reshape/Transpose：16x7x7 -> 784 个连续 float
 *   输入: +6596    输出: +3460（供 fc1 读取） */
  /* LITE_KERNEL_SECTION BEGIN _Reshape_output_0_to_chlast */
  {
    
  forward_lite_transpose__Reshape_output_0_to_chlast(net_ctx);
  }
  /* LITE_KERNEL_SECTION END _Reshape_output_0_to_chlast */
/* 第 8 层 Dense(fc1)：784 -> 64
 *   输入: +3460    输出: +324（64 个 float）
 *   权重: +4992    （64*784*4 = 200,704 B）
 *   bias: +205696  （64 个 float = 256 B） */
  /* LITE_KERNEL_SECTION BEGIN _fc1_Gemm_output_0 */
  {
      forward_lite_dense_if32of32wf32_args arg_30f51e = {
      .output = (float*)(net_ctx->_activations[0] + 324),
      .input = (float*)(net_ctx->_activations[0] + 3460),
      .weights = (float*)(net_ctx->_weights[0] + 4992),
      .bias = (float*)(net_ctx->_weights[0] + 205696),
      .n_channel_in = 784,
      .n_channel_out = 64,
      .n_elements = 1,
    };
  
  _STAI_NETWORK_EVENT_NODE_START_CB(9, 1, {(stai_ptr) (float*)(net_ctx->_activations[0] + 3460)});
    
  forward_lite_dense_if32of32wf32((forward_lite_dense_if32of32wf32_args*)&arg_30f51e);
    
  _STAI_NETWORK_EVENT_NODE_STOP_CB(9, 1, {(stai_ptr) (float*)(net_ctx->_activations[0] + 324)});
  }
  /* LITE_KERNEL_SECTION END _fc1_Gemm_output_0 */
/* 第 9 层 ReLU3：+324 -> +580（64 个 float）
 * 这里不是原地：+324 后面还要被最终输出复用，所以结果先放到 +580。 */
  /* LITE_KERNEL_SECTION BEGIN _relu_2_Relu_output_0 */
  {
      ai_handle _relu_2_Relu_output_0_t_out_0_ptr_handle = (ai_handle)(net_ctx->_activations[0] + 580);
    const ai_handle _relu_2_Relu_output_0_t_in_0_ptr_const_handle = (ai_handle)(net_ctx->_activations[0] + 324);
  
  _STAI_NETWORK_EVENT_NODE_START_CB(10, 1, {(stai_ptr) _relu_2_Relu_output_0_t_in_0_ptr_const_handle});
    
  forward_lite_nl_relu_if32of32(_relu_2_Relu_output_0_t_out_0_ptr_handle, _relu_2_Relu_output_0_t_in_0_ptr_const_handle, _relu_2_Relu_output_0_t_in_0_shape_ch_prod_const_s32, NULL);
    
  _STAI_NETWORK_EVENT_NODE_STOP_CB(10, 1, {(stai_ptr) _relu_2_Relu_output_0_t_out_0_ptr_handle});
  }
  /* LITE_KERNEL_SECTION END _relu_2_Relu_output_0 */
/* 第 10 层 Dense(fc2)：64 -> 10
 *   输入: +580     输出: _outputs[0]（= 激活区 +324，40 B）
 *   权重: +205952  （10*64*4 = 2560 B）
 *   bias: +208512  （10 个 float = 40 B）
 * 注意输出与 conv1 的输出同址：conv1 的结果早已无用，生成器做了内存复用。 */
  /* LITE_KERNEL_SECTION BEGIN output */
  {
      forward_lite_dense_if32of32wf32_args arg_30f51e = {
      .output = (float*)(net_ctx->_outputs[0] + 0),
      .input = (float*)(net_ctx->_activations[0] + 580),
      .weights = (float*)(net_ctx->_weights[0] + 205952),
      .bias = (float*)(net_ctx->_weights[0] + 208512),
      .n_channel_in = 64,
      .n_channel_out = 10,
      .n_elements = 1,
    };
  
  _STAI_NETWORK_EVENT_NODE_START_CB(11, 1, {(stai_ptr) (float*)(net_ctx->_activations[0] + 580)});
    
  forward_lite_dense_if32of32wf32((forward_lite_dense_if32of32wf32_args*)&arg_30f51e);
    
  _STAI_NETWORK_EVENT_NODE_STOP_CB(11, 1, {(stai_ptr) (float*)(net_ctx->_outputs[0] + 0)});
  }
  /* LITE_KERNEL_SECTION END output */
  return net_ctx->_return_code;
}

/*****************************************************************************/
/*  Getters APIs Section  */
STAI_API_ENTRY
/*------------------------------------------------------------------------------
 * F. get / set 系列 API
 * 这些函数只做“查/改上下文里的指针和数量”，不做额外内存分配。
 *------------------------------------------------------------------------------*/

/* 返回上下文结构体需要多少字节；应用用 STAI_NETWORK_CONTEXT_DECLARE 分配。 */
stai_size stai_network_get_context_size()
{
  return (stai_size)STAI_NETWORK_CONTEXT_SIZE;
}

#if defined(HAVE_NETWORK_INFO)
/* 填充 stai_network_info：模型名、工具/运行时版本、MACC、各张量描述。
 * 只有在编译时定义了 HAVE_NETWORK_INFO 才有实际内容。 */
STAI_API_ENTRY
stai_return_code stai_network_get_info(
  stai_network* network,
  stai_network_info* info)
{
  _STAI_CONTEXT_ACQUIRE(net_ctx, network)
  _STAI_SET_ERROR(net_ctx, info==NULL, STAI_ERROR_NETWORK_INVALID_INFO, net_ctx->_return_code)

  // Copy of network info struct
  *info = g_network_info;

  return STAI_SUCCESS;
}
#endif


/* 取激活缓冲区指针和数量（必须先 set_activations，否则拿到 NULL）。 */
STAI_API_ENTRY
stai_return_code stai_network_get_activations(
  stai_network* network, stai_ptr* activations, stai_size* n_activations)
{
  _STAI_CONTEXT_ACQUIRE(net_ctx, network)

  _STAI_SET_ERROR(net_ctx, !n_activations, STAI_ERROR_NETWORK_INVALID_API_ARGUMENTS, net_ctx->_return_code)
  *n_activations = STAI_NETWORK_ACTIVATIONS_NUM;
for (stai_size idx=0; activations && (idx<STAI_NETWORK_ACTIVATIONS_NUM); idx++) {
    // get address of the activations buffers
    activations[idx] = net_ctx->_activations[idx];
  }return net_ctx->_return_code;
}


/* 取权重缓冲区指针：指向 Flash 里的 g_network_weights_array。 */
STAI_API_ENTRY
stai_return_code stai_network_get_weights(
  stai_network* network, stai_ptr* weights, stai_size* n_weights)
{
  _STAI_CONTEXT_ACQUIRE(net_ctx, network)
  _STAI_SET_ERROR(net_ctx, !n_weights, STAI_ERROR_NETWORK_INVALID_API_ARGUMENTS, net_ctx->_return_code)
  *n_weights = STAI_NETWORK_WEIGHTS_NUM;
for (stai_size idx=0; weights && (idx<STAI_NETWORK_WEIGHTS_NUM); idx++) {
    // get address of the weights buffers
    weights[idx] = net_ctx->_weights[idx];
  }return net_ctx->_return_code;
}


/* 取输入缓冲区指针：set_activations 之后等于“激活区 + 24948”。
 * 应用往这里 memcpy 784 个 float 再 run()。 */
STAI_API_ENTRY
stai_return_code stai_network_get_inputs(
  stai_network* network, stai_ptr* inputs, stai_size* n_inputs)
{
  _STAI_CONTEXT_ACQUIRE(net_ctx, network)
  _STAI_SET_ERROR(net_ctx, !n_inputs, STAI_ERROR_NETWORK_INVALID_API_ARGUMENTS, net_ctx->_return_code)
  *n_inputs = STAI_NETWORK_IN_NUM;
  for (stai_size idx=0; inputs && (idx<STAI_NETWORK_IN_NUM); idx++) {
    inputs[idx] = net_ctx->_inputs[idx];
  }
  return net_ctx->_return_code;
}


/* 取输出缓冲区指针：set_activations 之后等于“激活区 + 324”。
 * run() 结束后从这里读 10 个 logits。 */
STAI_API_ENTRY
stai_return_code stai_network_get_outputs(
  stai_network* network, stai_ptr* outputs, stai_size* n_outputs)
{
  _STAI_CONTEXT_ACQUIRE(net_ctx, network)
  _STAI_SET_ERROR(net_ctx, !n_outputs, STAI_ERROR_NETWORK_INVALID_API_ARGUMENTS, net_ctx->_return_code)
  *n_outputs = STAI_NETWORK_OUT_NUM;
  for (stai_size idx=0; outputs && (idx<STAI_NETWORK_OUT_NUM); idx++) {
    outputs[idx] = net_ctx->_outputs[idx];
  }
  return net_ctx->_return_code;
}


/* 取最近一次错误码，排查时比只看返回值更直接。 */
STAI_API_ENTRY
stai_return_code stai_network_get_error(
  stai_network* network)
{
  _STAI_CONTEXT_ACQUIRE(net_ctx, network)

  /* return 1st generated error or STAI_SUCCESS if no errors so far */
  return net_ctx->_return_code;
}


/* 取内部状态缓冲区；本模型是无状态网络（STAI_NETWORK_STATES_NUM = 0），返回空。 */
STAI_API_ENTRY
stai_return_code stai_network_get_states(
  stai_network* network, stai_ptr* states, stai_size* n_states)
{
  _STAI_CONTEXT_ACQUIRE(net_ctx, network)
  _STAI_SET_ERROR(net_ctx, !n_states, STAI_ERROR_NETWORK_INVALID_API_ARGUMENTS, net_ctx->_return_code)
  /* get the number of internals states (supporting multi-heap also for internal states) */
  *n_states = STAI_NETWORK_STATES_NUM;

  STAI_UNUSED(states)
return net_ctx->_return_code;
}


/*****************************************************************************/
/*  Setters APIs Section  */

/* 绑定激活缓冲区，并在这里算出输入/输出指针：
 *     _inputs[0]  = 激活区基址 + 24948
 *     _outputs[0] = 激活区基址 + 324
 * 关键点：
 *   1) 必须在 get_inputs() / get_outputs() 之前调用，否则拿到的是 NULL；
 *   2) 会检查缓冲区地址是否满足 4 字节对齐；
 *   3) 传进来的指针必须是 28,084 字节且生命周期覆盖整个推理过程。 */
STAI_API_ENTRY
stai_return_code stai_network_set_activations(
  stai_network* network,
  const stai_ptr* activations,
  const stai_size n_activations)
{
  _STAI_CONTEXT_ACQUIRE(net_ctx, network)
const uintptr_t _activations_alignment[] = STAI_NETWORK_ACTIVATIONS_ALIGNMENTS;
  STAI_PRINT("  [stai_network_set_activations] network(%p) activations[%d]: %p\n\n", net_ctx, n_activations, activations)
  _STAI_SET_ERROR(net_ctx, !activations,
                  STAI_ERROR_NETWORK_INVALID_API_ARGUMENTS, net_ctx->_return_code)
  _STAI_SET_ERROR(net_ctx, n_activations!=STAI_NETWORK_ACTIVATIONS_NUM,
                  STAI_ERROR_NETWORK_INVALID_ACTIVATIONS_NUM, net_ctx->_return_code)

  for (stai_size idx=0; activations && idx<STAI_NETWORK_ACTIVATIONS_NUM; idx++) {
    STAI_PRINT("  activation[%d]: %p\n", idx, activations[idx])
    _STAI_SET_ERROR(net_ctx, activations[idx]==NULL,
                    STAI_ERROR_NETWORK_INVALID_ACTIVATIONS_PTR, net_ctx->_return_code)
    _STAI_SET_ERROR(net_ctx, ((uintptr_t)activations[idx]) & (_activations_alignment[idx]-1),
                    STAI_ERROR_INVALID_BUFFER_ALIGNMENT, net_ctx->_return_code)
    net_ctx->_activations[idx] = activations[idx];
  }
  net_ctx->_inputs[0] = activations[0] + 24948;

  net_ctx->_outputs[0] = activations[0] + 324;
_stai_network_check(net_ctx);
  return net_ctx->_return_code;
}


/* 替换权重指针（例如把权重搬到 QSPI/SDRAM 时使用）。
 * 本工程权重留在 Flash，不调用这个函数。 */
STAI_API_ENTRY
stai_return_code stai_network_set_weights(
  stai_network* network,
  const stai_ptr* weights,
  const stai_size n_weights)
{
  _STAI_CONTEXT_ACQUIRE(net_ctx, network)
const uintptr_t _weights_alignment[] = STAI_NETWORK_WEIGHTS_ALIGNMENTS;
  _STAI_SET_ERROR(net_ctx, !weights,
                  STAI_ERROR_NETWORK_INVALID_API_ARGUMENTS, net_ctx->_return_code)
  _STAI_SET_ERROR(net_ctx, n_weights!=STAI_NETWORK_WEIGHTS_NUM,
                  STAI_ERROR_NETWORK_INVALID_WEIGHTS_NUM, net_ctx->_return_code)
  for (stai_size idx=0; weights && idx<STAI_NETWORK_WEIGHTS_NUM; idx++) {
    STAI_PRINT("  weight[%d]: %p\n", idx, weights[idx])
    _STAI_SET_ERROR(net_ctx, weights[idx]==NULL,
                    STAI_ERROR_NETWORK_INVALID_WEIGHTS_PTR, net_ctx->_return_code)
    _STAI_SET_ERROR(net_ctx, ((uintptr_t)weights[idx]) & (_weights_alignment[idx]-1),
                    STAI_ERROR_INVALID_BUFFER_ALIGNMENT, net_ctx->_return_code)
    net_ctx->_weights[idx] = weights[idx];
  }_stai_network_check(net_ctx);
  return net_ctx->_return_code;
}


/* 替换输入缓冲区地址。本模型是“allocate-inputs”，输入在激活区内部，
 * 一般不需要调用；只有想让输入来自别处（如 DMA 缓冲）时才用。 */
STAI_API_ENTRY
stai_return_code stai_network_set_inputs(
  stai_network* network,
  const stai_ptr* inputs,
  const stai_size n_inputs)
{
  const uintptr_t _inputs_alignment[] = STAI_NETWORK_IN_ALIGNMENTS;
  _STAI_CONTEXT_ACQUIRE(net_ctx, network)
  _STAI_SET_ERROR(net_ctx, !inputs,
                  STAI_ERROR_NETWORK_INVALID_API_ARGUMENTS, net_ctx->_return_code)
  _STAI_SET_ERROR(net_ctx, n_inputs!=STAI_NETWORK_IN_NUM,
                  STAI_ERROR_NETWORK_INVALID_IN_NUM, net_ctx->_return_code)

  for (stai_size idx=0; inputs && idx<STAI_NETWORK_IN_NUM; idx++) {
    STAI_PRINT("  input[%d]: %p\n", idx, inputs[idx])
    _STAI_SET_ERROR(net_ctx, inputs[idx]==NULL,
                    STAI_ERROR_NETWORK_INVALID_IN_PTR, net_ctx->_return_code)
    _STAI_SET_ERROR(net_ctx, ((uintptr_t)inputs[idx]) & (_inputs_alignment[idx]-1),
                    STAI_ERROR_INVALID_BUFFER_ALIGNMENT, net_ctx->_return_code)
    net_ctx->_inputs[idx] = inputs[idx];
  }

  _stai_network_check(net_ctx);
  return net_ctx->_return_code;
}


/* 替换输出缓冲区地址，用途同 set_inputs。 */
STAI_API_ENTRY
stai_return_code stai_network_set_outputs(
  stai_network* network,
  const stai_ptr* outputs,
  const stai_size n_outputs)
{
  const uintptr_t _outputs_alignment[] = STAI_NETWORK_OUT_ALIGNMENTS;
  _STAI_CONTEXT_ACQUIRE(net_ctx, network)
  _STAI_SET_ERROR(net_ctx, !outputs,
                  STAI_ERROR_NETWORK_INVALID_API_ARGUMENTS, net_ctx->_return_code)
  _STAI_SET_ERROR(net_ctx, n_outputs!=STAI_NETWORK_OUT_NUM,
                  STAI_ERROR_NETWORK_INVALID_OUT_NUM, net_ctx->_return_code)

  for (stai_size idx=0; outputs && idx<n_outputs; idx++) {
    STAI_PRINT("  output[%d]: %p\n", idx, outputs[idx])
    _STAI_SET_ERROR(net_ctx, outputs[idx]==NULL,
                    STAI_ERROR_NETWORK_INVALID_OUT_PTR, net_ctx->_return_code)
    _STAI_SET_ERROR(net_ctx, ((uintptr_t)outputs[idx]) & (_outputs_alignment[idx]-1),
                    STAI_ERROR_INVALID_BUFFER_ALIGNMENT, net_ctx->_return_code)
    net_ctx->_outputs[idx] = outputs[idx];
  }

  _stai_network_check(net_ctx);
  return net_ctx->_return_code;
}


/* 设置内部状态缓冲区；无状态网络用不到。 */
STAI_API_ENTRY
stai_return_code stai_network_set_states(
  stai_network* network,
  const stai_ptr* states,
  const stai_size n_states)
{
  _STAI_CONTEXT_ACQUIRE(net_ctx, network)

  STAI_UNUSED(states)
  STAI_UNUSED(n_states)
_stai_network_check(net_ctx);
  return net_ctx->_return_code;
}

/* 注册事件回调：每层开始/结束时回调一次，可用于逐层 profiling。
 * 传 NULL 取消。本工程未注册，所以 _STAI_NETWORK_EVENT_* 全部是空操作。 */
STAI_API_ENTRY
stai_return_code stai_network_set_callback(
  stai_network* network, const stai_event_cb cb, void* cb_cookie)
{
  _STAI_CONTEXT_ACQUIRE(net_ctx, network)
  STAI_PRINT("  set_callback %p cb %p cookie %p\n", net_ctx, cb, cb_cookie)
  // _STAI_SET_ERROR(net_ctx, cb==NULL, STAI_ERROR_NETWORK_INVALID_CALLBACK, net_ctx->_return_code)
  net_ctx->_callback = cb;
  net_ctx->_callback_cookie = cb_cookie;
  return net_ctx->_return_code;
}

#undef _STAI_SET_ERROR
#undef _STAI_CONTEXT_ALIGNMENT
#undef _STAI_CONTEXT_ACQUIRE
#undef _STAI_NETWORK_EVENT_NODE_START_CB
#undef _STAI_NETWORK_EVENT_NODE_STOP_CB
#undef _STAI_NETWORK_MODEL_SIGNATURE
#undef _STAI_NETWORK_DATETIME
#undef _STAI_NETWORK_COMPILE_DATETIME

