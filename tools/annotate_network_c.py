# -*- coding: utf-8 -*-
"""按锚点给 network.c 插入中文注释，不修改任何代码行。"""

import io
import os

PATH = r"D:\Document\Project\STM32\HDR\Drivers\AI\network\network.c"

raw = open(PATH, "rb").read()
crlf = b"\r\n" in raw
text = raw.decode("utf-8")
lines = text.split("\r\n") if crlf else text.split("\n")
EOL = "\r\n" if crlf else "\n"

ins = {}


def add(idx, block):
    ins.setdefault(idx, []).append(block)


def find(pred, start=0):
    for i in range(start, len(lines)):
        if pred(lines[i]):
            return i
    raise RuntimeError("anchor not found")


HEADER = r"""/*==============================================================================
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
 *============================================================================*/"""

add(find(lambda s: s.strip() == '#include "ai_lite_inspect.h"'), HEADER)

add(find(lambda s: s.strip().startswith("#define STAI_INTERNAL_API_MAJOR")), r"""/*------------------------------------------------------------------------------
 * A. 内部宏
 *
 * STAI_MAGIC 会被写进上下文的第一个字段，每个 API 入口都会校验它，
 * 用来发现“传了野指针”或“上下文还没初始化就调用”的问题。
 *------------------------------------------------------------------------------*/""")

add(find(lambda s: s.strip().startswith("#define g_network_activations_1")), r"""/* 生成器把“激活缓冲区”留空（NULL），要求应用自己提供 28,084 字节的缓冲区，
 * 再通过 stai_network_set_activations() 绑定进来。
 * 这就是 ai_digit.c 里必须先准备 s_activations[] 的原因。 */""")

add(find(lambda s: s.strip() == "#if defined(HAVE_NETWORK_INFO)"), r"""/*------------------------------------------------------------------------------
 * B. 模型元数据（仅当定义了 HAVE_NETWORK_INFO 时编译）
 *
 * 记录模型名、工具/运行时版本、MACC 数量以及输入/输出/激活/权重的描述。
 * 只服务于 stai_network_get_info() 和调试工具，推理过程本身不读它。
 *------------------------------------------------------------------------------*/""")

add(find(lambda s: s.strip().startswith("#define _STAI_CONTEXT_ACQUIRE")), r"""/*------------------------------------------------------------------------------
 * C. 上下文校验
 *
 * 每个 API 入口都先展开 _STAI_CONTEXT_ACQUIRE：把句柄转成上下文指针并校验 magic；
 * 再用 _STAI_SET_ERROR 逐项检查参数，条件成立就设置错误码并立即 return。
 *------------------------------------------------------------------------------*/""")

add(find(lambda s: s.strip().startswith("void _stai_network_check(")) - 1, r"""/* 根据四个指针数组是否为空，重新计算 _flags 中的
 * ACTIVATIONS / INPUTS / OUTPUTS / WEIGHTS 位。
 * set_activations() 之后必须调用它（生成代码已经调了），run() 会检查这些位，
 * 缺一个就返回对应的 STAI_ERROR_NETWORK_INVALID_*_PTR。 */""")

add(find(lambda s: s.strip().startswith("stai_return_code stai_network_init(")) - 1, r"""/*------------------------------------------------------------------------------
 * 初始化：把一份“默认上下文”深拷贝到应用提供的内存里。
 *   - 激活区：填 NULL，必须由 set_activations() 补上（见 B 区上方的说明）；
 *   - 权重：直接指向 Flash 里的 g_network_weights_array，不复制、不占 RAM；
 *   - 输入/输出：也先填 NULL，等 set_activations() 根据激活区基址算出来。
 * 返回 STAI_SUCCESS 表示成功；失败可用 stai_network_get_error() 查原因。
 *------------------------------------------------------------------------------*/""")

add(find(lambda s: s.strip().startswith("stai_return_code stai_network_deinit(")) - 1, r"""/* 反初始化：只把 flags 恢复到初始值。
 * 权重和激活区都是应用提供的内存，这里不释放（也不允许释放）。 */""")

add(find(lambda s: s.strip() == "AI_ARRAY_OBJ_DECLARE("), r"""/*------------------------------------------------------------------------------
 * D. 旧平台层的“数组 / 张量 / 张量链 / 层”描述对象
 *
 * conv 和 dense 是直接调 forward_lite_* kernel 的，不需要这些对象；
 * 只有 maxpool 和 transpose 走平台层的 forward_mp() / forward_transpose()，
 * 所以下面有 5 个数组、5 个张量、3 条张量链、3 个层对象。
 * 这些对象里的 data 指针并不写死，运行时由 hybrid 前向函数按偏移量填进去。
 *------------------------------------------------------------------------------*/""")

add(find(lambda s: s.strip() == "AI_TENSOR_OBJ_DECLARE("), r"""/* 张量对象：把“数组 + 形状 + 格式”组合成平台层能识别的张量。
 * 这里只做描述，真正指向哪块内存要等 hybrid 前向函数设置 data。 */""")

add(find(lambda s: s.strip() == "AI_TENSOR_CHAIN_OBJ_DECLARE("), r"""/* 张量链：描述“这个层从哪个张量读、写到哪个张量”，相当于层的接线表。 */""")

add(find(lambda s: s.strip() == "AI_LAYER_OBJ_DECLARE("), r"""/* 层对象：把层类型（POOL / TRANSPOSE）、前向函数、张量链组合在一起。
 * 参数里的 out_mapping 描述内存排布（哪个维度是通道/宽/高）。 */""")

add(find(lambda s: s.strip().startswith("void forward_lite_mp__pool_MaxPool_output_0(")), r"""/* pool1 / pool2 的 hybrid 前向：先把输入、输出数组的 data 指针指向激活区偏移，
 * 再交给平台层 forward_mp()。输入输出同址，属于“原地池化”。 */""")

add(find(lambda s: s.strip().startswith("void forward_lite_transpose__Reshape_output_0_to_chlast(")), r"""/* NCHW -> 通道在最后的转换。
 * PyTorch 的卷积输出是 NCHW，而 dense 层要求 784 个值按“通道在最后”的顺序排列，
 * 所以这里把 16x7x7 重排成 784 个连续 float 供 fc1 使用。 */""")

add(find(lambda s: s.strip().startswith("stai_return_code stai_network_run(")) - 1, r"""/*==============================================================================
 * E. 推理主流程：按顺序执行 10 段 kernel 调用
 *
 * 每段的模板都相同：
 *   1) 从 _inputs / _activations / _weights 加固定字节偏移，得到输入/输出/权重指针；
 *   2) 调一次 forward_lite_* kernel（实现位于 NetworkRuntime1201_CM7_GCC.a）；
 *   3) 发 node start/stop 事件（本工程没注册 callback，编译后是空操作）。
 *
 * mode 参数当前版本未使用：生成代码里 STAI_UNUSED(mode)，一律同步执行。
 *============================================================================*/""")

LAYER_COMMENTS = {
    "/* LITE_KERNEL_SECTION BEGIN _conv1_Conv_output_0 */": r"""/* 第 1 层 Conv1：1x28x28 -> 8x28x28，3x3，pad=1，stride=1
 *   输入   : _inputs[0]（= 激活区 + 24948）
 *   输出   : 激活区 + 324
 *   权重   : +0    （8*1*3*3 个 float = 288 B）
 *   bias   : +288  （8 个 float = 32 B）
 *   scratch: +0    （卷积临时区） */""",
    "/* LITE_KERNEL_SECTION BEGIN _relu_Relu_output_0 */": r"""/* 第 2 层 ReLU1：原地作用在 +324，共 8*28*28 = 6272 个 float。
 * 输入输出同一个地址，省内存。 */""",
    "/* LITE_KERNEL_SECTION BEGIN _pool_MaxPool_output_0 */": r"""/* 第 3 层 MaxPool1：8x28x28 -> 8x14x14，窗口 2x2，步长 2，原地。 */""",
    "/* LITE_KERNEL_SECTION BEGIN _conv2_Conv_output_0 */": r"""/* 第 4 层 Conv2：8x14x14 -> 16x14x14，3x3，pad=1
 *   输入   : +324
 *   输出   : +6596
 *   权重   : +320    （16*8*3*3*4 = 4608 B）
 *   bias   : +4928   （16 个 float = 64 B）
 *   scratch: +36     （72 个 float = 288 B） */""",
    "/* LITE_KERNEL_SECTION BEGIN _relu_1_Relu_output_0 */": r"""/* 第 5 层 ReLU2：原地作用在 +6596，共 16*14*14 = 3136 个 float。 */""",
    "/* LITE_KERNEL_SECTION BEGIN _pool_1_MaxPool_output_0 */": r"""/* 第 6 层 MaxPool2：16x14x14 -> 16x7x7，窗口 2x2，原地。 */""",
    "/* LITE_KERNEL_SECTION BEGIN _Reshape_output_0_to_chlast */": r"""/* 第 7 层 Reshape/Transpose：16x7x7 -> 784 个连续 float
 *   输入: +6596    输出: +3460（供 fc1 读取） */""",
    "/* LITE_KERNEL_SECTION BEGIN _fc1_Gemm_output_0 */": r"""/* 第 8 层 Dense(fc1)：784 -> 64
 *   输入: +3460    输出: +324（64 个 float）
 *   权重: +4992    （64*784*4 = 200,704 B）
 *   bias: +205696  （64 个 float = 256 B） */""",
    "/* LITE_KERNEL_SECTION BEGIN _relu_2_Relu_output_0 */": r"""/* 第 9 层 ReLU3：+324 -> +580（64 个 float）
 * 这里不是原地：+324 后面还要被最终输出复用，所以结果先放到 +580。 */""",
    "/* LITE_KERNEL_SECTION BEGIN output */": r"""/* 第 10 层 Dense(fc2)：64 -> 10
 *   输入: +580     输出: _outputs[0]（= 激活区 +324，40 B）
 *   权重: +205952  （10*64*4 = 2560 B）
 *   bias: +208512  （10 个 float = 40 B）
 * 注意输出与 conv1 的输出同址：conv1 的结果早已无用，生成器做了内存复用。 */""",
}

for anchor, comment in LAYER_COMMENTS.items():
    add(find(lambda s, a=anchor: s.strip() == a), comment)

add(find(lambda s: s.strip().startswith("stai_size stai_network_get_context_size(")), r"""/*------------------------------------------------------------------------------
 * F. get / set 系列 API
 * 这些函数只做“查/改上下文里的指针和数量”，不做额外内存分配。
 *------------------------------------------------------------------------------*/

/* 返回上下文结构体需要多少字节；应用用 STAI_NETWORK_CONTEXT_DECLARE 分配。 */""")

API_COMMENTS = [
    ("stai_return_code stai_network_get_info(", r"""/* 填充 stai_network_info：模型名、工具/运行时版本、MACC、各张量描述。
 * 只有在编译时定义了 HAVE_NETWORK_INFO 才有实际内容。 */"""),
    ("stai_return_code stai_network_get_activations(", r"""/* 取激活缓冲区指针和数量（必须先 set_activations，否则拿到 NULL）。 */"""),
    ("stai_return_code stai_network_get_weights(", r"""/* 取权重缓冲区指针：指向 Flash 里的 g_network_weights_array。 */"""),
    ("stai_return_code stai_network_get_inputs(", r"""/* 取输入缓冲区指针：set_activations 之后等于“激活区 + 24948”。
 * 应用往这里 memcpy 784 个 float 再 run()。 */"""),
    ("stai_return_code stai_network_get_outputs(", r"""/* 取输出缓冲区指针：set_activations 之后等于“激活区 + 324”。
 * run() 结束后从这里读 10 个 logits。 */"""),
    ("stai_return_code stai_network_get_error(", r"""/* 取最近一次错误码，排查时比只看返回值更直接。 */"""),
    ("stai_return_code stai_network_get_states(", r"""/* 取内部状态缓冲区；本模型是无状态网络（STAI_NETWORK_STATES_NUM = 0），返回空。 */"""),
    ("stai_return_code stai_network_set_activations(", r"""/* 绑定激活缓冲区，并在这里算出输入/输出指针：
 *     _inputs[0]  = 激活区基址 + 24948
 *     _outputs[0] = 激活区基址 + 324
 * 关键点：
 *   1) 必须在 get_inputs() / get_outputs() 之前调用，否则拿到的是 NULL；
 *   2) 会检查缓冲区地址是否满足 4 字节对齐；
 *   3) 传进来的指针必须是 28,084 字节且生命周期覆盖整个推理过程。 */"""),
    ("stai_return_code stai_network_set_weights(", r"""/* 替换权重指针（例如把权重搬到 QSPI/SDRAM 时使用）。
 * 本工程权重留在 Flash，不调用这个函数。 */"""),
    ("stai_return_code stai_network_set_inputs(", r"""/* 替换输入缓冲区地址。本模型是“allocate-inputs”，输入在激活区内部，
 * 一般不需要调用；只有想让输入来自别处（如 DMA 缓冲）时才用。 */"""),
    ("stai_return_code stai_network_set_outputs(", r"""/* 替换输出缓冲区地址，用途同 set_inputs。 */"""),
    ("stai_return_code stai_network_set_states(", r"""/* 设置内部状态缓冲区；无状态网络用不到。 */"""),
    ("stai_return_code stai_network_set_callback(", r"""/* 注册事件回调：每层开始/结束时回调一次，可用于逐层 profiling。
 * 传 NULL 取消。本工程未注册，所以 _STAI_NETWORK_EVENT_* 全部是空操作。 */"""),
]

for sig, comment in API_COMMENTS:
    idx = find(lambda s, a=sig: s.strip().startswith(a))
    # 这些函数前面一行通常是 STAI_API_ENTRY，注释放在它上面更整齐
    if lines[idx - 1].strip() == "STAI_API_ENTRY":
        idx -= 1
    add(idx, comment)

# 按行号升序输出
out = []
for i, line in enumerate(lines):
    if i in ins:
        for block in ins[i]:
            out.extend(block.split("\n"))
    out.append(line)

data = EOL.join(out)
open(PATH, "wb").write(data.encode("utf-8"))
print("inserted blocks:", len(ins), "total lines:", len(out))
