#include "main.h"
#include "ai_digit.h"
#include "ui.h"               /* UI_INK_W / UI_INK_H：手写墨迹位图尺寸 */
#include "network.h"          /* 会带进 stai.h 与所有尺寸宏 */
#include <string.h>

/* 注意：这里不要用 <math.h> 里的 expf()。
   newlib 的 expf 内部会走 double（vcvt.f64.f32 等双精度 VFP 指令），
   在本板子上会触发 UsageFault(UNDEFINSTR)。下面自己实现纯单精度的 exp。 */

#define AI_IN_W   28
#define AI_IN_H   28
#define AI_IN_N    (AI_IN_W * AI_IN_H)

/* 网络上下文：必须用这个宏声明，内部按 8 字节对齐 */
STAI_NETWORK_CONTEXT_DECLARE(s_ctx, STAI_NETWORK_CONTEXT_SIZE)
static stai_network *const s_net = (stai_network *)s_ctx;

/* 运行时要求应用提供 28,084 B 的激活区，32 字节对齐 */
STAI_ALIGNED(32) static uint8_t s_activations[STAI_NETWORK_ACTIVATION_1_SIZE_BYTES];
static stai_ptr  s_act_buffers[STAI_NETWORK_ACTIVATIONS_NUM] = { s_activations };

static stai_ptr  s_in[STAI_NETWORK_IN_NUM];
static stai_ptr  s_out[STAI_NETWORK_OUT_NUM];
static stai_size s_in_len, s_out_len;

static float s_img[AI_IN_N];
static uint8_t s_ready = 0;          /* AI_Init 成功后置 1，未就绪时不跑网络 */

int AI_Init(void)
{
  stai_return_code rc;

  s_ready = 0;

  rc = stai_runtime_init();
  if (rc != STAI_SUCCESS) return -1;

  rc = stai_network_init(s_net);
  if (rc != STAI_SUCCESS) return -2;

  /* 必须在 get_inputs 之前，I/O 缓冲在激活区里 */
  rc = stai_network_set_activations(s_net, s_act_buffers, STAI_NETWORK_ACTIVATIONS_NUM);
  if (rc != STAI_SUCCESS) return -3;

  rc = stai_network_get_inputs(s_net, s_in, &s_in_len);
  if ((rc != STAI_SUCCESS) || (s_in_len < 1)) return -4;

  rc = stai_network_get_outputs(s_net, s_out, &s_out_len);
  if ((rc != STAI_SUCCESS) || (s_out_len < 1)) return -5;

  s_ready = 1;
  return 0;
}

static int AI_Run(void)
{
  if (stai_network_run(s_net, STAI_MODE_SYNC) != STAI_SUCCESS)
  {
    return -1;
  }
  return 0;
}

/* ============ 预处理：包围盒 -> 20x20 -> 质心对齐 -> 28x28 ============ */
static int AI_Preprocess(const uint8_t *ink)
{
  int x, y, minx = UI_INK_W, maxx = -1, miny = UI_INK_H, maxy = -1;
  int bw, bh, side, dw, dh, dx, dy;
  int sum = 0, sx = 0, sy = 0;
  static uint8_t small[20][20];
  static uint8_t canvas[28][28];

  memset(canvas, 0, sizeof(canvas));
  memset(s_img, 0, sizeof(s_img));
  // 裁剪出最小有效图像矩形
  for (y = 0; y < UI_INK_H; y++)
    for (x = 0; x < UI_INK_W; x++)
      if (ink[y * UI_INK_W + x]) {
        if (x < minx) minx = x;
        if (x > maxx) maxx = x;
        if (y < miny) miny = y;
        if (y > maxy) maxy = y;
      }

  if (maxx < 0) return -1;                 /* 空画板 */

  // 算包围盒 决定缩放比例dw dh
  bw = maxx - minx + 1;
  bh = maxy - miny + 1;
  side = (bw > bh) ? bw : bh;
  dw = (bw * 20 + side / 2) / side; if (dw < 1) dw = 1;
  dh = (bh * 20 + side / 2) / side; if (dh < 1) dh = 1;
  
  // 进行面积平均 小化图像
  for (dy = 0; dy < dh; dy++)
    for (dx = 0; dx < dw; dx++)
    {
      int sx0 = minx + dx * bw / dw, sx1 = minx + (dx + 1) * bw / dw;
      int sy0 = miny + dy * bh / dh, sy1 = miny + (dy + 1) * bh / dh;
      int cnt = 0, area = 0;
      for (y = sy0; y < sy1; y++)
        for (x = sx0; x < sx1; x++, area++)
          cnt += ink[y * UI_INK_W + x];
      small[dy][dx] = area ? (uint8_t)(cnt * 255 / area) : 0;
    }
  //计算质心
  for (dy = 0; dy < dh; dy++)
    for (dx = 0; dx < dw; dx++)
    {
      int v = small[dy][dx];
      sum += v; sx += dx * v; sy += dy * v;
    }
  if (sum == 0) return -1;

  {
    int ox = 14 - sx / sum;
    int oy = 14 - sy / sum;

    for (dy = 0; dy < dh; dy++)
      for (dx = 0; dx < dw; dx++)
      {
        int cx = dx + ox, cy = dy + oy;
        if (cx >= 0 && cx < 28 && cy >= 0 && cy < 28)
          canvas[cy][cx] = small[dy][dx];
      }
  }

  /* 归一化：必须与训练一致，见“第 3 步” */
  for (y = 0; y < 28; y++)
    for (x = 0; x < 28; x++)
      s_img[y * 28 + x] = ((float)canvas[y][x] / 255.0f - 0.1307f) / 0.3081f;

  return 0;
}

/* ============ 纯单精度 exp（不用 libm，避免 double 指令）============ */
static float AI_Expf(float x)
{
  union { float f; int32_t i; } u;
  float   t, p;
  int32_t n;

  if (x <= -88.0f)
  {
    return 0.0f;
  }
  if (x >= 88.0f)
  {
    x = 88.0f;
  }

  /* x = n*ln2 + t，|t| <= ln2/2 */
  n = (int32_t)(x * 1.4426950408889634f);
  t = x - (float)n * 0.6931471805599453f;

  /* exp(t) 的 5 阶泰勒展开，误差 < 1e-6 */
  p = 1.0f + t * (1.0f + t * (0.5f + t * (1.0f / 6.0f
        + t * (1.0f / 24.0f + t * (1.0f / 120.0f)))));

  /* 乘 2^n：直接改指数位 */
  u.f = p;
  u.i += (n << 23);

  return u.f;
}

uint8_t AI_Predict(const uint8_t *ink, uint8_t pct[10])
{
  const float *logits;
  float m = -1e30f, sum = 0.0f, e[10];
  int i, best = 0;

  for (i = 0; i < 10; i++) pct[i] = 0;        /* 提前失败时不留垃圾值 */

  if (!s_ready) return 0xFF;                  /* AI_Init 失败，直接报未识别 */

  if (AI_Preprocess(ink) != 0) return 0xFF;   /* 空画板 */

  memcpy(s_in[0], s_img, STAI_NETWORK_IN_1_SIZE_BYTES);   /* 3136 B */
  if (AI_Run() != 0) return 0xFF;

  logits = (const float *)s_out[0];

  for (i = 0; i < 10; i++) if (logits[i] > m) { m = logits[i]; best = i; }
  for (i = 0; i < 10; i++) { e[i] = AI_Expf(logits[i] - m); sum += e[i]; }
  for (i = 0; i < 10; i++)
  {
    float p = e[i] / sum * 100.0f + 0.5f;
    pct[i] = (p > 100.0f) ? 100 : (uint8_t)p;
  }

  return (uint8_t)best;
}

/**
  * @brief  用一张全 0 的假图跑一次推理，用来测速 / 判断运行时是否卡死。
  * @retval >=0 单次推理耗时（ms）；-1 未初始化；-2 推理返回错误
  * @note   调用前建议先把状态栏改成 "AI TEST"，若卡住不返回就能确认是运行时的问题。
  */
int AI_SelfTest(void)
{
  uint32_t t0;
  float *in;
  int i;

  if (!s_ready) return -1;

  in = (float *)s_in[0];
  for (i = 0; i < AI_IN_N; i++) in[i] = 0.0f;

  t0 = HAL_GetTick();
  if (AI_Run() != 0) return -2;

  return (int)(HAL_GetTick() - t0);
}