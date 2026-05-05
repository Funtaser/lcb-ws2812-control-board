/* USER CODE BEGIN Header：CubeMX 用户代码区开始，保留该标记 */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : STM32G030F6P6 的 WS2812 灯带控制器
  *
  * 电控对接说明：
  *   1. 主控通过 USART2 与本板通信，串口参数为 115200 8N1。
  *   2. 上电后主控必须先发送 "OK"，之后再发送 COUNT/BRI/COLOR/ANIM。
  *   3. 内置动画通过 "ANIM <编号>" 选择。
  *   4. ANIM 4 预留为自定义动画模板。
  *   5. 动画代码中不要加入 HAL_Delay()，所有动画必须是
  *      非阻塞逻辑，并由 HAL_GetTick() 提供时间基准。
  ******************************************************************************
  */
/* USER CODE END Header：CubeMX 用户代码区结束，保留该标记 */

#include "main.h"  /* 包含所需头文件 */

#include <stdbool.h>  /* 包含所需头文件 */
#include <stdint.h>  /* 包含所需头文件 */
#include <string.h>  /* 包含所需头文件 */

/* -------------------------------------------------------------------------- */
/* 用户配置区                                                         */
/* -------------------------------------------------------------------------- */
/* 电控队员通常只需要改这一段：
 *   - WS2812_LED_MAX：编译期支持的最大灯珠数量。
 *   - WS2812_LED_DEFAULT：主控发送 COUNT 前的默认灯珠数量。
 *   - DEFAULT_BRIGHTNESS：主控发送 BRI 前的默认亮度。
 *
 * 上电后的串口命令仍然优先：
 *   COUNT <1..300> 会修改 g_led_count。
 *   BRI <0..255> 会修改 g_brightness。
 */
#define WS2812_LED_MAX              300u  /* 编译期支持的最大灯珠数量，最多 300 颗 */
#define WS2812_LED_DEFAULT          300u   /* 主控发送 COUNT 前的默认灯珠数量 */
#define WS2812_RESET_BYTES          80u   /* 4MHz SPI 下 80 字节低电平约 160us */

#define UART_BAUDRATE               115200u
#define UART_LINE_MAX               64u

/* PB3，低电平有效状态灯 */
#define STATUS_LED_GPIO_PORT        GPIOB
#define STATUS_LED_PIN              GPIO_PIN_3

/* ADC 与保护参数 */
#define ADC_FULL_SCALE              4095u
#define ADC_VREF_MV                 3300u
#define ADC_SAMPLE_COUNT        64u    /* ADC 每个通道连续采样次数，降低电流显示跳变 */
#define ADC_TASK_PERIOD_MS          20u
#define ADC_FILTER_SHIFT            2u    /* IIR 系数 1/4，整数滤波，无浮点 */

/* PA0: 24V 母线经 120k / 10k 分压，Vbus = Vadc * 13 */
#define VBUS_DIV_TOP_OHM            120000u
#define VBUS_DIV_BOTTOM_OHM         10000u

/* 母线电压软件校准：
 * 电表实测 25.19V，串口/ADC 平均约 25.207V
 * 校准后 Vbus = 原始计算值 * 25190 / 25220
 */
#define VBUS_CAL_REAL_MV            25235u
#define VBUS_CAL_ADC_MV             25271u

#define VBUS_TYPEC_MAX_MV           6000u  /* 小于 6V 认为是 Type-C/USB 调试供电，不报低压 */
#define VBUS_UNDERVOLT_ENTER_MV     19800u /* 6V~19.8V 连续确认后判定为低压 */
#define VBUS_UNDERVOLT_EXIT_MV      20300u /* 高于 20.3V 连续确认后退出低压 */
#define VBUS_OVERVOLT_ENTER_MV      25600u /* 高于 25.6V 连续确认后判定为过压 */
#define VBUS_OVERVOLT_EXIT_MV       25300u /* 低于 25.2V 连续确认后退出过压 */

/* PA6: 20mΩ 电流采样电阻，放大倍数 20，Vadc = I * 0.02 * 20 = I * 0.4 */
#define ISENSE_SHUNT_MOHM           20u
#define ISENSE_GAIN                 20u
#define ISENSE_OVERCURRENT_ENTER_MA 2000u /* 高于 2A 连续确认后判定为过流 */
#define ISENSE_OVERCURRENT_EXIT_MA  1600u /* 低于 1.6A 连续确认后退出过流 */

#define ISENSE_CAL_NUM          990u  /* 电流比例校准分子：184.5mV 实测点校准到约 461mA */
#define ISENSE_CAL_DEN          1000u  /* 电流比例校准分母 */

#define FAULT_SEVERE_ENTER_COUNT    2u
#define FAULT_LOW_ENTER_COUNT       3u
#define FAULT_EXIT_COUNT            5u

#define FAULT_RED_BRIGHTNESS        18u
#define DEFAULT_BRIGHTNESS          80u

/* -------------------------------------------------------------------------- */
/* 电控自定义动画配置区                                         */
/* -------------------------------------------------------------------------- */
/* ANIM 4 预留给电控队员。
 * 修改 CUSTOM_ANIM_1_PERIOD_MS 可以改变动画帧间隔。
 * 真正的动画逻辑在 custom_animation_1_task() 函数中。
 */
#define CUSTOM_ANIM_1_PERIOD_MS 50u  /* 定义宏 CUSTOM_ANIM_1_PERIOD_MS */

#define DEV_MODE_TIMEOUT_MS     300000u  /* 5 分钟自动退出开发者模式 */

/* -------------------------------------------------------------------------- */
/* HAL 外设句柄                                                                */
/* -------------------------------------------------------------------------- */
ADC_HandleTypeDef hadc1;  /* 定义 HAL 外设句柄 */
SPI_HandleTypeDef hspi1;  /* 定义 HAL 外设句柄 */
UART_HandleTypeDef huart2;  /* 定义 HAL 外设句柄 */

/* -------------------------------------------------------------------------- */
/* 应用类型定义                                                          */
/* -------------------------------------------------------------------------- */
typedef enum  /* 开始定义枚举类型 */
{  /* 代码块开始 */
  APP_WAIT_FOR_HOST = 0,  /* 赋值或更新变量 */
  APP_IDLE,  /* 枚举或初始化列表项 */
  APP_STATIC_COLOR,  /* 枚举或初始化列表项 */
  APP_ANIM_RED_CHASE,  /* 枚举或初始化列表项 */
  APP_ANIM_RAINBOW,  /* 枚举或初始化列表项 */
  APP_ANIM_RED_BREATH,  /* 枚举或初始化列表项 */

  /* 电控自定义动画步骤 1/5：
   * 在这里添加新的自定义动画模式。
   * 示例：APP_ANIM_CUSTOM_1 由命令 "ANIM 4" 控制。
   * 如果需要更多动画，可继续添加 APP_ANIM_CUSTOM_2 等枚举。
   */
  APP_ANIM_CUSTOM_1  /* 代码行说明 */
} AppMode_t;  /* 结束类型或代码块定义 */

typedef enum  /* 开始定义枚举类型 */
{  /* 代码块开始 */
  FAULT_NONE = 0,  /* 赋值或更新变量 */
  FAULT_UNDERVOLTAGE,  /* 枚举或初始化列表项 */
  FAULT_OVERVOLTAGE,  /* 枚举或初始化列表项 */
  FAULT_OVERCURRENT  /* 代码行说明 */
} Fault_t;  /* 结束类型或代码块定义 */

typedef struct  /* 开始定义结构体类型 */
{  /* 代码块开始 */
  uint8_t r;  /* 定义局部变量 */
  uint8_t g;  /* 定义局部变量 */
  uint8_t b;  /* 定义局部变量 */
} Rgb_t;  /* 结束类型或代码块定义 */

typedef struct  /* 开始定义结构体类型 */
{  /* 代码块开始 */
  bool enabled;  /* 定义局部变量 */
  Fault_t forced_fault;  /* 定义局部变量 */
  bool force_mode;  /* 定义局部变量 */
  AppMode_t forced_mode;  /* 定义局部变量 */
  uint32_t enabled_ms;  /* 定义局部变量 */
} DevMode_t;  /* 结束类型或代码块定义 */

/* -------------------------------------------------------------------------- */
/* 私有变量                                                          */
/* -------------------------------------------------------------------------- */
static uint16_t g_led_count = WS2812_LED_DEFAULT;  /* 声明静态函数或变量 */
static uint8_t g_brightness = DEFAULT_BRIGHTNESS;  /* 声明静态函数或变量 */
static Rgb_t g_leds[WS2812_LED_MAX];  /* 声明静态函数或变量 */
static uint8_t g_ws2812_spi_buf[WS2812_LED_MAX * 12u + (WS2812_RESET_BYTES * 2u)];  /* 前置 reset + 数据 + 后置 reset */

static AppMode_t g_mode = APP_WAIT_FOR_HOST;  /* 声明静态函数或变量 */
static Fault_t g_fault = FAULT_NONE;  /* 声明静态函数或变量 */
static Rgb_t g_static_color = {0, 0, 0};  /* 声明静态函数或变量 */
static DevMode_t g_dev = {false, FAULT_NONE, false, APP_WAIT_FOR_HOST, 0u};  /* 声明静态函数或变量 */

static uint32_t g_bus_mv = 0;  /* 最近一次原始换算电压，单位 mV */
static uint32_t g_current_ma = 0;  /* 最近一次原始换算电流，单位 mA */
static uint32_t g_bus_mv_filtered = 0;  /* IIR 滤波后的电压，单位 mV */
static uint32_t g_current_ma_filtered = 0;  /* IIR 滤波后的电流，单位 mA */
static uint8_t g_fault_enter_count = 0;  /* 保护进入连续确认计数 */
static uint8_t g_fault_exit_count = 0;  /* 保护退出连续确认计数 */
static uint32_t g_last_adc_ms = 0;  /* 声明静态函数或变量 */
static uint32_t g_last_anim_ms = 0;  /* 声明静态函数或变量 */
static uint32_t g_last_cmd_ms = 0;  /* 声明静态函数或变量 */
static uint16_t g_anim_step = 0;  /* 声明静态函数或变量 */
/* g_anim_step 是动画共用帧计数器。
 * 切换动画模式时应将它清零。
 * 自定义动画可以把它作为位置、相位或帧编号使用。
 */

static uint8_t g_uart_rx_byte = 0;  /* 声明静态函数或变量 */
static volatile uint8_t g_uart_line_ready = 0;  /* 声明静态函数或变量 */
static volatile uint8_t g_uart_rx_len = 0;  /* 声明静态函数或变量 */
static volatile uint8_t g_uart_drop_until_eol = 0;  /* 超长帧丢弃直到行尾 */
static volatile uint8_t g_uart_overflow_count = 0;  /* 串口行溢出计数，饱和计数 */
static char g_uart_line[UART_LINE_MAX];  /* 声明静态函数或变量 */

/* -------------------------------------------------------------------------- */
/* 函数声明                                                        */
/* -------------------------------------------------------------------------- */
void SystemClock_Config(void);  /* 定义或声明函数 */
static void MX_GPIO_Init(void);  /* 声明静态函数或变量 */
static void MX_ADC1_Init(void);  /* 声明静态函数或变量 */
static void MX_SPI1_Init(void);  /* 声明静态函数或变量 */
static void MX_USART2_UART_Init(void);  /* 声明静态函数或变量 */

static void app_init(void);  /* 声明静态函数或变量 */
static void app_task(void);  /* 声明静态函数或变量 */
static void protection_task(uint32_t now);  /* 声明静态函数或变量 */
static void update_fault_state(void);  /* 保护状态机：滤波、迟滞、连续确认 */
static uint32_t filter_u32(uint32_t current, uint32_t sample);  /* 整数 IIR 滤波 */
static void app_set_mode(AppMode_t mode, bool clear_leds);  /* 统一模式切换入口 */
static void status_led_task(uint32_t now);  /* 声明静态函数或变量 */
static void animation_task(uint32_t now);  /* 声明静态函数或变量 */
static void process_uart_line(char *line);  /* 声明静态函数或变量 */
static bool process_dev_command(const char *p);  /* 声明静态函数或变量 */
static void dev_mode_disable(void);  /* 声明静态函数或变量 */
static void dev_mode_task(uint32_t now);  /* 声明静态函数或变量 */
static Fault_t get_effective_fault(void);  /* 声明静态函数或变量 */
static AppMode_t get_effective_mode(void);  /* 声明静态函数或变量 */
static const char *fault_to_str(Fault_t fault);  /* 声明静态函数或变量 */
static const char *mode_to_str(AppMode_t mode);  /* 声明静态函数或变量 */

static void ws2812_show(void);  /* 声明静态函数或变量 */
static void ws2812_force_reset_low(void);  /* 仅发送 reset 低电平，稳定上电/模式切换 */
static void ws2812_clear(void);  /* 声明静态函数或变量 */
static void ws2812_set_all(uint8_t r, uint8_t g, uint8_t b);  /* 声明静态函数或变量 */
static void ws2812_set_pixel(uint16_t index, uint8_t r, uint8_t g, uint8_t b);  /* 声明静态函数或变量 */
static void custom_animation_1_task(uint32_t now, bool *should_update);  /* 声明静态函数或变量 */
static Rgb_t wheel(uint8_t pos);  /* 声明静态函数或变量 */

static uint16_t adc_read_raw_avg(uint32_t channel);  /* 切换通道后丢首样，再多次平均 */
static uint32_t adc_raw_to_mv(uint16_t raw);  /* 声明静态函数或变量 */
static uint32_t read_bus_mv(void);  /* 声明静态函数或变量 */
static uint32_t read_current_ma(void);  /* 声明静态函数或变量 */

static void status_led_set(bool on);  /* 声明静态函数或变量 */
static void status_led_set_pwm(uint32_t now, uint8_t brightness_percent);  /* 声明静态函数或变量 */
static uint8_t status_led_breath_level(uint32_t now, uint32_t period_ms, uint8_t min_percent, uint8_t max_percent);  /* 声明静态函数或变量 */
static void uart_send_str(const char *s);  /* 声明静态函数或变量 */
static void uart_send_stat(void);  /* 声明静态函数或变量 */
static void uart_send_dev_status(void);  /* 声明静态函数或变量 */
static uint32_t append_u32(char *dst, uint32_t pos, uint32_t value);  /* 声明静态函数或变量 */
static void uppercase_ascii(char *s);  /* 声明静态函数或变量 */
static const char *skip_spaces(const char *s);  /* 声明静态函数或变量 */
static bool is_token_end(char ch);  /* 命令词边界检查 */
static bool match_token(const char *s, const char *token);  /* 严格匹配一个命令词 */
static bool only_spaces_left(const char *s);  /* 参数尾部合法性检查 */
static bool parse_uint(const char **ps, uint32_t *value);  /* 声明静态函数或变量 */

/* -------------------------------------------------------------------------- */
/* Main                                                                       */
/* -------------------------------------------------------------------------- */
int main(void)  /* 定义或声明函数 */
{  /* 代码块开始 */
  HAL_Init();  /* 调用函数执行对应操作 */
  SystemClock_Config();  /* 调用函数执行对应操作 */

  MX_GPIO_Init();  /* 调用函数执行对应操作 */
  MX_ADC1_Init();  /* 调用函数执行对应操作 */
  MX_SPI1_Init();  /* 调用函数执行对应操作 */
  MX_USART2_UART_Init();  /* 调用函数执行对应操作 */

  app_init();  /* 调用函数执行对应操作 */

  while (1)  /* 执行循环 */
  {  /* 代码块开始 */
    app_task();  /* 调用函数执行对应操作 */
  }  /* 代码块结束 */
}  /* 代码块结束 */

/* -------------------------------------------------------------------------- */
/* Application                                                                */
/* -------------------------------------------------------------------------- */
static void app_init(void)  /* 定义或声明函数 */
{  /* 代码块开始 */
  status_led_set(false);  /* 调用函数执行对应操作 */
  ws2812_force_reset_low();  /* 上电先给 WS2812 一个明确 reset 低电平 */
  ws2812_clear();  /* 调用函数执行对应操作 */
  ws2812_show();  /* 调用函数执行对应操作 */

  if (HAL_UART_Receive_IT(&huart2, &g_uart_rx_byte, 1) != HAL_OK)  /* 判断条件是否成立 */
  {  /* 代码块开始 */
    Error_Handler();  /* 调用函数或完成表达式 */
  }  /* 代码块结束 */
}  /* 代码块结束 */

static void app_task(void)  /* 定义或声明函数 */
{  /* 代码块开始 */
  uint32_t now = HAL_GetTick();  /* 定义局部变量 */

  if (g_uart_line_ready != 0u)  /* 判断条件是否成立 */
  {  /* 代码块开始 */
    char line[UART_LINE_MAX];  /* 定义局部变量 */

    __disable_irq();  /* 调用函数或完成表达式 */
    strncpy(line, g_uart_line, sizeof(line));  /* 调用函数或完成表达式 */
    line[sizeof(line) - 1u] = '\0';  /* 赋值或更新变量 */
    g_uart_line_ready = 0u;  /* 赋值或更新变量 */
    __enable_irq();  /* 调用函数或完成表达式 */

    process_uart_line(line);  /* 调用函数或完成表达式 */
  }  /* 代码块结束 */

  /* 任务执行顺序：
   *   1. protection_task：更新真实 ADC 电压、电流和故障状态。
   *   2. dev_mode_task：开发者模式超时后自动关闭。
   *   3. status_led_task：更新 PB3 板载状态灯。
   *   4. animation_task：更新 WS2812 灯带。
   *
   * 所有任务都应保持非阻塞，不要在这里加入长时间 HAL_Delay()。
   */
  protection_task(now);  /* 调用函数执行对应操作 */
  dev_mode_task(now);  /* 调用函数执行对应操作 */
  status_led_task(now);  /* 调用函数执行对应操作 */
  animation_task(now);  /* 调用函数执行对应操作 */
}  /* 代码块结束 */

static void protection_task(uint32_t now)  /* 定义或声明函数 */
{  /* 代码块开始 */
  if ((now - g_last_adc_ms) < ADC_TASK_PERIOD_MS)  /* 判断条件是否成立 */
  {  /* 代码块开始 */
    return;  /* 返回函数结果 */
  }  /* 代码块结束 */

  g_last_adc_ms = now;  /* 赋值或更新变量 */
  g_bus_mv = read_bus_mv();  /* 读取 PA0 分压后的母线电压 */
  g_current_ma = read_current_ma();  /* 读取 PA6 电流采样 */
  g_bus_mv_filtered = filter_u32(g_bus_mv_filtered, g_bus_mv);  /* 电压滤波 */
  g_current_ma_filtered = filter_u32(g_current_ma_filtered, g_current_ma);  /* 电流滤波 */
  update_fault_state();  /* 滤波 + 迟滞 + 连续确认 */
}  /* 代码块结束 */

static uint32_t filter_u32(uint32_t current, uint32_t sample)  /* 定义或声明函数 */
{  /* 代码块开始 */
  uint32_t delta;  /* 定义局部变量 */

  if (current == 0u)  /* 第一次采样直接装载，避免上电从 0 慢慢爬升造成假低压 */
  {  /* 代码块开始 */
    return sample;  /* 返回函数结果 */
  }  /* 代码块结束 */

  if (sample >= current)  /* 判断条件是否成立 */
  {  /* 代码块开始 */
    delta = sample - current;  /* 赋值或更新变量 */
    return current + ((delta + ((1u << ADC_FILTER_SHIFT) - 1u)) >> ADC_FILTER_SHIFT);  /* 返回函数结果 */
  }  /* 代码块结束 */

  delta = current - sample;  /* 赋值或更新变量 */
  return current - ((delta + ((1u << ADC_FILTER_SHIFT) - 1u)) >> ADC_FILTER_SHIFT);  /* 返回函数结果 */
}  /* 代码块结束 */

static void update_fault_state(void)  /* 定义或声明函数 */
{  /* 代码块开始 */
  Fault_t request = FAULT_NONE;  /* 根据当前滤波值请求进入的新故障 */
  uint8_t enter_need = FAULT_LOW_ENTER_COUNT;  /* 默认低压确认次数 */
  bool exit_ok = false;  /* 当前故障是否满足退出阈值 */

  /* 进入优先级：过流 > 过压 > 低压。 */
  if (g_current_ma_filtered > ISENSE_OVERCURRENT_ENTER_MA)  /* 判断条件是否成立 */
  {  /* 代码块开始 */
    request = FAULT_OVERCURRENT;  /* 赋值或更新变量 */
    enter_need = FAULT_SEVERE_ENTER_COUNT;  /* 赋值或更新变量 */
  }  /* 代码块结束 */
  else if (g_bus_mv_filtered > VBUS_OVERVOLT_ENTER_MV)  /* 判断条件是否成立 */
  {  /* 代码块开始 */
    request = FAULT_OVERVOLTAGE;  /* 赋值或更新变量 */
    enter_need = FAULT_SEVERE_ENTER_COUNT;  /* 赋值或更新变量 */
  }  /* 代码块结束 */
  else if ((g_bus_mv_filtered >= VBUS_TYPEC_MAX_MV) && (g_bus_mv_filtered < VBUS_UNDERVOLT_ENTER_MV))  /* 判断条件是否成立 */
  {  /* 代码块开始 */
    request = FAULT_UNDERVOLTAGE;  /* 赋值或更新变量 */
    enter_need = FAULT_LOW_ENTER_COUNT;  /* 赋值或更新变量 */
  }  /* 代码块结束 */

  if (g_fault == FAULT_NONE)  /* 当前无故障，按请求进入 */
  {  /* 代码块开始 */
    g_fault_exit_count = 0u;  /* 赋值或更新变量 */
    if (request == FAULT_NONE)  /* 判断条件是否成立 */
    {  /* 代码块开始 */
      g_fault_enter_count = 0u;  /* 赋值或更新变量 */
      return;  /* 返回函数结果 */
    }  /* 代码块结束 */

    if (++g_fault_enter_count >= enter_need)  /* 判断条件是否成立 */
    {  /* 代码块开始 */
      g_fault = request;  /* 赋值或更新变量 */
      g_fault_enter_count = 0u;  /* 赋值或更新变量 */
    }  /* 代码块结束 */
    return;  /* 返回函数结果 */
  }  /* 代码块结束 */

  /* 已在故障中时，允许更高优先级故障覆盖。 */
  if ((g_fault != FAULT_OVERCURRENT) && (request == FAULT_OVERCURRENT))  /* 判断条件是否成立 */
  {  /* 代码块开始 */
    if (++g_fault_enter_count >= FAULT_SEVERE_ENTER_COUNT)  /* 判断条件是否成立 */
    {  /* 代码块开始 */
      g_fault = FAULT_OVERCURRENT;  /* 赋值或更新变量 */
      g_fault_enter_count = 0u;  /* 赋值或更新变量 */
      g_fault_exit_count = 0u;  /* 赋值或更新变量 */
    }  /* 代码块结束 */
    return;  /* 返回函数结果 */
  }  /* 代码块结束 */

  if ((g_fault == FAULT_UNDERVOLTAGE) && (request == FAULT_OVERVOLTAGE))  /* 判断条件是否成立 */
  {  /* 代码块开始 */
    if (++g_fault_enter_count >= FAULT_SEVERE_ENTER_COUNT)  /* 判断条件是否成立 */
    {  /* 代码块开始 */
      g_fault = FAULT_OVERVOLTAGE;  /* 赋值或更新变量 */
      g_fault_enter_count = 0u;  /* 赋值或更新变量 */
      g_fault_exit_count = 0u;  /* 赋值或更新变量 */
    }  /* 代码块结束 */
    return;  /* 返回函数结果 */
  }  /* 代码块结束 */

  g_fault_enter_count = 0u;  /* 赋值或更新变量 */

  if (g_fault == FAULT_OVERCURRENT)  /* 判断条件是否成立 */
  {  /* 代码块开始 */
    exit_ok = (g_current_ma_filtered < ISENSE_OVERCURRENT_EXIT_MA);  /* 赋值或更新变量 */
  }  /* 代码块结束 */
  else if (g_fault == FAULT_OVERVOLTAGE)  /* 判断另一个条件是否成立 */
  {  /* 代码块开始 */
    exit_ok = (g_bus_mv_filtered < VBUS_OVERVOLT_EXIT_MV);  /* 赋值或更新变量 */
  }  /* 代码块结束 */
  else if (g_fault == FAULT_UNDERVOLTAGE)  /* 判断另一个条件是否成立 */
  {  /* 代码块开始 */
    /* 高于退出阈值表示电池恢复；低于 6V 表示切到 Type-C/USB 调试供电。 */
    exit_ok = (g_bus_mv_filtered > VBUS_UNDERVOLT_EXIT_MV) || (g_bus_mv_filtered < VBUS_TYPEC_MAX_MV);  /* 赋值或更新变量 */
  }  /* 代码块结束 */

  if (exit_ok)  /* 判断条件是否成立 */
  {  /* 代码块开始 */
    if (++g_fault_exit_count >= FAULT_EXIT_COUNT)  /* 判断条件是否成立 */
    {  /* 代码块开始 */
      g_fault = FAULT_NONE;  /* 赋值或更新变量 */
      g_fault_exit_count = 0u;  /* 赋值或更新变量 */
    }  /* 代码块结束 */
  }  /* 代码块结束 */
  else  /* 处理否则情况 */
  {  /* 代码块开始 */
    g_fault_exit_count = 0u;  /* 赋值或更新变量 */
  }  /* 代码块结束 */
}  /* 代码块结束 */

static void app_set_mode(AppMode_t mode, bool clear_leds)  /* 定义或声明函数 */
{  /* 代码块开始 */
  g_mode = mode;  /* 赋值或更新变量 */
  g_anim_step = 0u;  /* 赋值或更新变量 */
  g_last_anim_ms = HAL_GetTick();  /* 切模式后重新计时，避免第一帧继承旧动画相位 */

  if (clear_leds)  /* 判断条件是否成立 */
  {  /* 代码块开始 */
    ws2812_clear();  /* 调用函数执行对应操作 */
    ws2812_show();  /* 调用函数执行对应操作 */
  }  /* 代码块结束 */
}  /* 代码块结束 */

static void animation_task(uint32_t now)  /* 定义或声明函数 */
{  /* 代码块开始 */
  bool should_update = false;  /* 定义局部变量 */
  Fault_t fault = get_effective_fault();  /* 定义局部变量 */
  AppMode_t mode = get_effective_mode();  /* 定义局部变量 */

  /* 电控动画框架说明：
   *   - 本函数会在主循环中反复运行。
   *   - 使用 (now - g_last_anim_ms) 控制动画帧间隔。
   *   - 通过 ws2812_set_pixel()、ws2812_set_all()、ws2812_clear() 修改 g_leds[] 缓存。
   *   - 一帧数据准备完成后，将 should_update 置为 true。
   *   - ws2812_show() 只在本函数底部统一调用一次。
   *   - 不要在 for 循环内重复调用 ws2812_show()。
   *   - 不要在动画代码中使用 HAL_Delay()。
   */

  if (fault == FAULT_OVERCURRENT || fault == FAULT_OVERVOLTAGE)  /* 判断条件是否成立 */
  {  /* 代码块开始 */
    /* 严重故障：灯带低亮度红色快速闪烁。 */
    bool on = ((now / 100u) & 1u) != 0u;  /* 定义局部变量 */
    static bool last_on = false;  /* 声明静态函数或变量 */

    if (on != last_on || (now - g_last_anim_ms) >= 500u)  /* 判断条件是否成立 */
    {  /* 代码块开始 */
      last_on = on;  /* 赋值或更新变量 */
      g_last_anim_ms = now;  /* 赋值或更新变量 */
      should_update = true;  /* 赋值或更新变量 */
      if (on)  /* 判断条件是否成立 */
      {  /* 代码块开始 */
        ws2812_set_all(FAULT_RED_BRIGHTNESS, 0, 0);  /* 调用函数执行对应操作 */
      }  /* 代码块结束 */
      else  /* 处理否则情况 */
      {  /* 代码块开始 */
        ws2812_clear();  /* 调用函数执行对应操作 */
      }  /* 代码块结束 */
    }  /* 代码块结束 */
  }  /* 代码块结束 */
  else if (fault == FAULT_UNDERVOLTAGE)  /* 判断另一个条件是否成立 */
  {  /* 代码块开始 */
    /* 低压故障：灯带低亮度红色慢闪。 */
    uint32_t phase = now % 2000u;  /* 定义局部变量 */
    bool on = phase < 500u;  /* 定义局部变量 */
    static bool last_on = false;  /* 声明静态函数或变量 */

    if (on != last_on || (now - g_last_anim_ms) >= 1000u)  /* 判断条件是否成立 */
    {  /* 代码块开始 */
      last_on = on;  /* 赋值或更新变量 */
      g_last_anim_ms = now;  /* 赋值或更新变量 */
      should_update = true;  /* 赋值或更新变量 */
      if (on)  /* 判断条件是否成立 */
      {  /* 代码块开始 */
        ws2812_set_all(FAULT_RED_BRIGHTNESS, 0, 0);  /* 调用函数执行对应操作 */
      }  /* 代码块结束 */
      else  /* 处理否则情况 */
      {  /* 代码块开始 */
        ws2812_clear();  /* 调用函数执行对应操作 */
      }  /* 代码块结束 */
    }  /* 代码块结束 */
  }  /* 代码块结束 */
  else if (mode == APP_WAIT_FOR_HOST)  /* 判断另一个条件是否成立 */
  {  /* 代码块开始 */
    if ((now - g_last_anim_ms) >= 60u)  /* 判断条件是否成立 */
    {  /* 代码块开始 */
      g_last_anim_ms = now;  /* 赋值或更新变量 */
      should_update = true;  /* 赋值或更新变量 */

      ws2812_clear();  /* 等待主控：单红点扫描，避免整条填充造成“多起点”误判 */
      if (g_led_count > 0u)  /* 判断条件是否成立 */
      {  /* 代码块开始 */
        uint16_t head = (uint16_t)(g_anim_step % g_led_count);  /* 定义局部变量 */
        ws2812_set_pixel(head, 24u, 0u, 0u);  /* 主扫描点 */
        if (g_led_count > 1u)  /* 判断条件是否成立 */
        {  /* 代码块开始 */
          ws2812_set_pixel((uint16_t)((head + g_led_count - 1u) % g_led_count), 8u, 0u, 0u);  /* 短拖尾 */
        }  /* 代码块结束 */
        if (g_led_count > 2u)  /* 判断条件是否成立 */
        {  /* 代码块开始 */
          ws2812_set_pixel((uint16_t)((head + g_led_count - 2u) % g_led_count), 3u, 0u, 0u);  /* 短拖尾 */
        }  /* 代码块结束 */
      }  /* 代码块结束 */

      g_anim_step++;  /* 执行一条语句 */
    }  /* 代码块结束 */
  }  /* 代码块结束 */
  else if (mode == APP_ANIM_RED_CHASE)  /* 判断另一个条件是否成立 */
  {  /* 代码块开始 */
    if ((now - g_last_anim_ms) >= 40u)  /* 判断条件是否成立 */
    {  /* 代码块开始 */
      g_last_anim_ms = now;  /* 赋值或更新变量 */
      should_update = true;  /* 赋值或更新变量 */

      ws2812_clear();  /* 调用函数执行对应操作 */
      ws2812_set_pixel(g_anim_step % g_led_count, 255, 0, 0);  /* 调用函数执行对应操作 */
      if (g_led_count > 2u)  /* 判断条件是否成立 */
      {  /* 代码块开始 */
        ws2812_set_pixel((g_anim_step + g_led_count - 1u) % g_led_count, 48, 0, 0);  /* 调用函数执行对应操作 */
        ws2812_set_pixel((g_anim_step + g_led_count - 2u) % g_led_count, 12, 0, 0);  /* 调用函数执行对应操作 */
      }  /* 代码块结束 */
      g_anim_step++;  /* 执行一条语句 */
    }  /* 代码块结束 */
  }  /* 代码块结束 */
  else if (mode == APP_ANIM_RAINBOW)  /* 判断另一个条件是否成立 */
  {  /* 代码块开始 */
    if ((now - g_last_anim_ms) >= 30u)  /* 判断条件是否成立 */
    {  /* 代码块开始 */
      g_last_anim_ms = now;  /* 赋值或更新变量 */
      should_update = true;  /* 赋值或更新变量 */

      for (uint16_t i = 0; i < g_led_count; i++)  /* 执行循环 */
      {  /* 代码块开始 */
        Rgb_t c = wheel((uint8_t)((i * 256u / g_led_count + g_anim_step) & 0xFFu));  /* 定义局部变量 */
        ws2812_set_pixel(i, c.r, c.g, c.b);  /* 调用函数执行对应操作 */
      }  /* 代码块结束 */
      g_anim_step++;  /* 执行一条语句 */
    }  /* 代码块结束 */
  }  /* 代码块结束 */
  else if (mode == APP_ANIM_RED_BREATH)  /* 判断另一个条件是否成立 */
  {  /* 代码块开始 */
    if ((now - g_last_anim_ms) >= 20u)  /* 判断条件是否成立 */
    {  /* 代码块开始 */
      uint8_t level;  /* 定义局部变量 */
      uint16_t phase;  /* 定义局部变量 */

      g_last_anim_ms = now;  /* 赋值或更新变量 */
      should_update = true;  /* 赋值或更新变量 */

      phase = g_anim_step % 512u;  /* 赋值或更新变量 */
      level = (phase < 256u) ? (uint8_t)phase : (uint8_t)(511u - phase);  /* 赋值或更新变量 */
      ws2812_set_all(level, 0, 0);  /* 调用函数执行对应操作 */
      g_anim_step++;  /* 执行一条语句 */
    }  /* 代码块结束 */
  }  /* 代码块结束 */
  else if (mode == APP_ANIM_CUSTOM_1)  /* 判断另一个条件是否成立 */
  {  /* 代码块开始 */
    /* ECU CUSTOM ANIMATION STEP 4/5:
     * ANIM 4 enters this branch.
     * Put the actual custom animation in custom_animation_1_task().
     */
    custom_animation_1_task(now, &should_update);  /* 调用函数执行对应操作 */
  }  /* 代码块结束 */

  if (should_update)  /* 判断条件是否成立 */
  {  /* 代码块开始 */
    ws2812_show();  /* 调用函数执行对应操作 */
  }  /* 代码块结束 */
}  /* 代码块结束 */

/* PB3 状态灯模式，低电平点亮。
 * 新状态语义：
 *   - 呼吸：普通状态或非严重状态。
 *   - 常亮：稳定连接状态。
 *   - 硬闪：保护故障状态。
 * 优先级：故障 > 命令确认脉冲 > 主控/动画状态。
 */
static void status_led_task(uint32_t now)  /* 定义或声明函数 */
{  /* 代码块开始 */
  Fault_t fault = get_effective_fault();  /* 定义局部变量 */
  AppMode_t mode = get_effective_mode();  /* 定义局部变量 */

  if (fault == FAULT_OVERCURRENT)  /* 判断条件是否成立 */
  {  /* 代码块开始 */
    /* 严重故障：满亮高速频闪，和普通状态明显区分。 */
    status_led_set((now % 120u) < 60u);  /* 调用函数执行对应操作 */
  }  /* 代码块结束 */
  else if (fault == FAULT_OVERVOLTAGE)  /* 判断另一个条件是否成立 */
  {  /* 代码块开始 */
    /* 过压故障：三次短促硬闪，然后长时间停顿。 */
    uint32_t phase = now % 1200u;  /* 定义局部变量 */
    bool on = (phase < 100u) ||  /* 定义局部变量 */
              ((phase >= 220u) && (phase < 320u)) ||  /* 赋值或更新变量 */
              ((phase >= 440u) && (phase < 540u));  /* 赋值或更新变量 */
    status_led_set(on);  /* 调用函数执行对应操作 */
  }  /* 代码块结束 */
  else if (fault == FAULT_UNDERVOLTAGE)  /* 判断另一个条件是否成立 */
  {  /* 代码块开始 */
    /* 低压故障：超慢呼吸，紧急程度较低但持续提示。 */
    uint8_t level = status_led_breath_level(now, 3200u, 5u, 80u);  /* 定义局部变量 */
    status_led_set_pwm(now, level);  /* 调用函数执行对应操作 */
  }  /* 代码块结束 */
  else if ((now - g_last_cmd_ms) < 120u)  /* 判断另一个条件是否成立 */
  {  /* 代码块开始 */
    /* 收到一条完整串口命令：满亮短脉冲作为确认提示。 */
    status_led_set(true);  /* 调用函数执行对应操作 */
  }  /* 代码块结束 */
  else if (mode == APP_WAIT_FOR_HOST)  /* 判断另一个条件是否成立 */
  {  /* 代码块开始 */
    /* 等待主控 OK：慢速呼吸。 */
    uint8_t level = status_led_breath_level(now, 1800u, 5u, 95u);  /* 定义局部变量 */
    status_led_set_pwm(now, level);  /* 调用函数执行对应操作 */
  }  /* 代码块结束 */
  else if ((mode == APP_ANIM_RED_CHASE) ||  /* 判断另一个条件是否成立 */
           (mode == APP_ANIM_RAINBOW) ||  /* 赋值或更新变量 */
           (mode == APP_ANIM_RED_BREATH) ||  /* 赋值或更新变量 */
           (mode == APP_ANIM_CUSTOM_1))  /* 赋值或更新变量 */
  {  /* 代码块开始 */
    /* 动画运行中：快速呼吸。 */
    uint8_t level = status_led_breath_level(now, 800u, 10u, 100u);  /* 定义局部变量 */
    status_led_set_pwm(now, level);  /* 调用函数执行对应操作 */
  }  /* 代码块结束 */
  else if (mode == APP_STATIC_COLOR)  /* 判断另一个条件是否成立 */
  {  /* 代码块开始 */
    /* 静态颜色输出中：中等亮度常亮。 */
    status_led_set_pwm(now, 45u);  /* 调用函数执行对应操作 */
  }  /* 代码块结束 */
  else  /* 处理否则情况 */
  {  /* 代码块开始 */
    /* 主控已连接且空闲：低亮常亮。 */
    status_led_set_pwm(now, 15u);  /* 调用函数执行对应操作 */
  }  /* 代码块结束 */
}  /* 代码块结束 */

/* PB3 状态灯软件 PWM。
 * brightness_percent：0 表示熄灭，100 表示全亮。
 * 20ms PWM 周期对小状态灯来说闪烁感较低，同时计算开销很小。
 */
static void status_led_set_pwm(uint32_t now, uint8_t brightness_percent)  /* 定义或声明函数 */
{  /* 代码块开始 */
  uint32_t phase;  /* 定义局部变量 */

  if (brightness_percent == 0u)  /* 判断条件是否成立 */
  {  /* 代码块开始 */
    status_led_set(false);  /* 调用函数执行对应操作 */
    return;  /* 返回函数结果 */
  }  /* 代码块结束 */

  if (brightness_percent >= 100u)  /* 判断条件是否成立 */
  {  /* 代码块开始 */
    status_led_set(true);  /* 调用函数执行对应操作 */
    return;  /* 返回函数结果 */
  }  /* 代码块结束 */

  phase = now % 20u;  /* 赋值或更新变量 */
  status_led_set((phase * 100u) < ((uint32_t)brightness_percent * 20u));  /* 调用函数执行对应操作 */
}  /* 代码块结束 */

/* 三角波呼吸亮度曲线：故意只使用整数运算，避免浮点开销。 */
static uint8_t status_led_breath_level(uint32_t now, uint32_t period_ms, uint8_t min_percent, uint8_t max_percent)  /* 定义或声明函数 */
{  /* 代码块开始 */
  uint32_t phase;  /* 定义局部变量 */
  uint32_t half;  /* 定义局部变量 */
  uint32_t ramp;  /* 定义局部变量 */
  uint32_t level;  /* 定义局部变量 */

  if (period_ms < 2u)  /* 判断条件是否成立 */
  {  /* 代码块开始 */
    return max_percent;  /* 返回函数结果 */
  }  /* 代码块结束 */

  if (max_percent <= min_percent)  /* 判断条件是否成立 */
  {  /* 代码块开始 */
    return min_percent;  /* 返回函数结果 */
  }  /* 代码块结束 */

  phase = now % period_ms;  /* 赋值或更新变量 */
  half = period_ms / 2u;  /* 赋值或更新变量 */
  if (half == 0u)  /* 判断条件是否成立 */
  {  /* 代码块开始 */
    return max_percent;  /* 返回函数结果 */
  }  /* 代码块结束 */

  if (phase < half)  /* 判断条件是否成立 */
  {  /* 代码块开始 */
    ramp = phase;  /* 赋值或更新变量 */
  }  /* 代码块结束 */
  else  /* 处理否则情况 */
  {  /* 代码块开始 */
    ramp = period_ms - phase;  /* 赋值或更新变量 */
    if (ramp > half)  /* 判断条件是否成立 */
    {  /* 代码块开始 */
      ramp = half;  /* 赋值或更新变量 */
    }  /* 代码块结束 */
  }  /* 代码块结束 */

  level = (uint32_t)min_percent + (((uint32_t)(max_percent - min_percent) * ramp) / half);  /* 赋值或更新变量 */
  if (level > 100u)  /* 判断条件是否成立 */
  {  /* 代码块开始 */
    level = 100u;  /* 赋值或更新变量 */
  }  /* 代码块结束 */
  return (uint8_t)level;  /* 返回函数结果 */
}  /* 代码块结束 */


/* -------------------------------------------------------------------------- */
/* Developer mode                                                             */
/* -------------------------------------------------------------------------- */
static void dev_mode_disable(void)  /* 定义或声明函数 */
{  /* 代码块开始 */
  g_dev.enabled = false;  /* 赋值或更新变量 */
  g_dev.forced_fault = FAULT_NONE;  /* 赋值或更新变量 */
  g_dev.force_mode = false;  /* 赋值或更新变量 */
  g_dev.forced_mode = APP_WAIT_FOR_HOST;  /* 赋值或更新变量 */
  g_dev.enabled_ms = 0u;  /* 赋值或更新变量 */
}  /* 代码块结束 */

static void dev_mode_task(uint32_t now)  /* 定义或声明函数 */
{  /* 代码块开始 */
  if (g_dev.enabled && ((now - g_dev.enabled_ms) > DEV_MODE_TIMEOUT_MS))  /* 判断条件是否成立 */
  {  /* 代码块开始 */
    dev_mode_disable();  /* 调用函数执行对应操作 */
    uart_send_str("DEV AUTO_OFF\r\n");  /* 调用函数执行对应操作 */
  }  /* 代码块结束 */
}  /* 代码块结束 */

static Fault_t get_effective_fault(void)  /* 定义或声明函数 */
{  /* 代码块开始 */
  if (g_dev.enabled && g_dev.forced_fault != FAULT_NONE)  /* 判断条件是否成立 */
  {  /* 代码块开始 */
    return g_dev.forced_fault;  /* 返回函数结果 */
  }  /* 代码块结束 */
  return g_fault;  /* 返回函数结果 */
}  /* 代码块结束 */

static AppMode_t get_effective_mode(void)  /* 定义或声明函数 */
{  /* 代码块开始 */
  if (g_dev.enabled && g_dev.force_mode)  /* 判断条件是否成立 */
  {  /* 代码块开始 */
    return g_dev.forced_mode;  /* 返回函数结果 */
  }  /* 代码块结束 */
  return g_mode;  /* 返回函数结果 */
}  /* 代码块结束 */

static const char *fault_to_str(Fault_t fault)  /* 定义或声明函数 */
{  /* 代码块开始 */
  switch (fault)  /* 根据取值选择分支 */
  {  /* 代码块开始 */
  case FAULT_NONE:         return "NONE";  /* 处理该分支 */
  case FAULT_UNDERVOLTAGE: return "UNDERVOLTAGE";  /* 处理该分支 */
  case FAULT_OVERVOLTAGE:  return "OVERVOLTAGE";  /* 处理该分支 */
  case FAULT_OVERCURRENT:  return "OVERCURRENT";  /* 处理该分支 */
  default:                 return "UNKNOWN";  /* 处理默认分支 */
  }  /* 代码块结束 */
}  /* 代码块结束 */

static const char *mode_to_str(AppMode_t mode)  /* 定义或声明函数 */
{  /* 代码块开始 */
  /* ECU CUSTOM ANIMATION STEP 2/5:
   * If a new AppMode_t is added, add its printable name here.
   * 这只影响 DEV STATUS 的输出，但有助于调试。
   */
  switch (mode)  /* 根据取值选择分支 */
  {  /* 代码块开始 */
  case APP_WAIT_FOR_HOST:    return "WAIT_HOST";  /* 处理该分支 */
  case APP_IDLE:             return "IDLE";  /* 处理该分支 */
  case APP_STATIC_COLOR:     return "STATIC_COLOR";  /* 处理该分支 */
  case APP_ANIM_RED_CHASE:   return "ANIM_RED_CHASE";  /* 处理该分支 */
  case APP_ANIM_RAINBOW:     return "ANIM_RAINBOW";  /* 处理该分支 */
  case APP_ANIM_RED_BREATH:  return "ANIM_RED_BREATH";  /* 处理该分支 */
  case APP_ANIM_CUSTOM_1:    return "ANIM_CUSTOM_1";  /* 处理该分支 */
  default:                   return "UNKNOWN";  /* 处理默认分支 */
  }  /* 代码块结束 */
}  /* 代码块结束 */

static bool process_dev_command(const char *p)  /* 定义或声明函数 */
{  /* 代码块开始 */
  const char *q;  /* 执行一条语句 */

  if (!match_token(p, "DEV"))  /* 判断条件是否成立 */
  {  /* 代码块开始 */
    return false;  /* 返回函数结果 */
  }  /* 代码块结束 */

  q = skip_spaces(p + 3);  /* 赋值或更新变量 */

  if ((*q == '\0') || (match_token(q, "HELP") && only_spaces_left(q + 4)))  /* 判断条件是否成立 */
  {  /* 代码块开始 */
    uart_send_str("DEV CMDS: ON OFF STATUS ADC FAULT NONE|LOW|OVERV|OVERI MODE NONE|WAIT|IDLE|COLOR|ANIM1|ANIM2|ANIM3|ANIM4\r\n");  /* 调用函数执行对应操作 */
    return true;  /* 返回函数结果 */
  }  /* 代码块结束 */

  if (match_token(q, "ON") && only_spaces_left(q + 2))  /* 判断条件是否成立 */
  {  /* 代码块开始 */
    g_dev.enabled = true;  /* 赋值或更新变量 */
    g_dev.forced_fault = FAULT_NONE;  /* 赋值或更新变量 */
    g_dev.force_mode = false;  /* 赋值或更新变量 */
    g_dev.forced_mode = APP_WAIT_FOR_HOST;  /* 赋值或更新变量 */
    g_dev.enabled_ms = HAL_GetTick();  /* 赋值或更新变量 */
    uart_send_str("OK DEV ON\r\n");  /* 调用函数执行对应操作 */
    return true;  /* 返回函数结果 */
  }  /* 代码块结束 */

  if (match_token(q, "OFF") && only_spaces_left(q + 3))  /* 判断条件是否成立 */
  {  /* 代码块开始 */
    dev_mode_disable();  /* 调用函数执行对应操作 */
    uart_send_str("OK DEV OFF\r\n");  /* 调用函数执行对应操作 */
    return true;  /* 返回函数结果 */
  }  /* 代码块结束 */

  if (match_token(q, "STATUS") && only_spaces_left(q + 6))  /* 判断条件是否成立 */
  {  /* 代码块开始 */
    uart_send_dev_status();  /* 调用函数执行对应操作 */
    return true;  /* 返回函数结果 */
  }  /* 代码块结束 */

  if (match_token(q, "ADC") && only_spaces_left(q + 3))  /* 判断条件是否成立 */
  {  /* 代码块开始 */
    uart_send_stat();  /* 调用函数执行对应操作 */
    return true;  /* 返回函数结果 */
  }  /* 代码块结束 */

  if (!g_dev.enabled)  /* 判断条件是否成立 */
  {  /* 代码块开始 */
    uart_send_str("ERR DEV OFF\r\n");  /* 调用函数执行对应操作 */
    return true;  /* 返回函数结果 */
  }  /* 代码块结束 */

  if (match_token(q, "FAULT"))  /* 判断条件是否成立 */
  {  /* 代码块开始 */
    q = skip_spaces(q + 5);  /* 赋值或更新变量 */

    if ((match_token(q, "NONE") && only_spaces_left(q + 4)) ||
        (match_token(q, "CLEAR") && only_spaces_left(q + 5)))  /* 判断条件是否成立 */
    {  /* 代码块开始 */
      g_dev.forced_fault = FAULT_NONE;  /* 赋值或更新变量 */
      uart_send_str("OK DEV FAULT NONE\r\n");  /* 调用函数执行对应操作 */
    }  /* 代码块结束 */
    else if ((match_token(q, "LOW") && only_spaces_left(q + 3)) ||
             (match_token(q, "UNDER") && only_spaces_left(q + 5)) ||
             (match_token(q, "UNDERV") && only_spaces_left(q + 6)))  /* 判断另一个条件是否成立 */
    {  /* 代码块开始 */
      g_dev.forced_fault = FAULT_UNDERVOLTAGE;  /* 赋值或更新变量 */
      uart_send_str("OK DEV FAULT UNDERVOLTAGE\r\n");  /* 调用函数执行对应操作 */
    }  /* 代码块结束 */
    else if ((match_token(q, "OVERV") && only_spaces_left(q + 5)) ||
             (match_token(q, "OVER_V") && only_spaces_left(q + 6)) ||
             (match_token(q, "VHIGH") && only_spaces_left(q + 5)))  /* 判断另一个条件是否成立 */
    {  /* 代码块开始 */
      g_dev.forced_fault = FAULT_OVERVOLTAGE;  /* 赋值或更新变量 */
      uart_send_str("OK DEV FAULT OVERVOLTAGE\r\n");  /* 调用函数执行对应操作 */
    }  /* 代码块结束 */
    else if ((match_token(q, "OVERI") && only_spaces_left(q + 5)) ||
             (match_token(q, "OVER_C") && only_spaces_left(q + 6)) ||
             (match_token(q, "CURRENT") && only_spaces_left(q + 7)))  /* 判断另一个条件是否成立 */
    {  /* 代码块开始 */
      g_dev.forced_fault = FAULT_OVERCURRENT;  /* 赋值或更新变量 */
      uart_send_str("OK DEV FAULT OVERCURRENT\r\n");  /* 调用函数执行对应操作 */
    }  /* 代码块结束 */
    else  /* 处理否则情况 */
    {  /* 代码块开始 */
      uart_send_str("ERR DEV FAULT NONE|LOW|OVERV|OVERI\r\n");  /* 调用函数执行对应操作 */
    }  /* 代码块结束 */
    return true;  /* 返回函数结果 */
  }  /* 代码块结束 */

  if (match_token(q, "MODE") || match_token(q, "STATE") || match_token(q, "LED"))  /* 判断条件是否成立 */
  {  /* 代码块开始 */
    if (match_token(q, "MODE")) { q = skip_spaces(q + 4); }  /* 判断条件是否成立 */
    else if (match_token(q, "STATE")) { q = skip_spaces(q + 5); }  /* 判断另一个条件是否成立 */
    else { q = skip_spaces(q + 3); }  /* 处理否则情况 */

    if ((match_token(q, "NONE") && only_spaces_left(q + 4)) ||
        (match_token(q, "REAL") && only_spaces_left(q + 4)))  /* 判断条件是否成立 */
    {  /* 代码块开始 */
      g_dev.force_mode = false;  /* 赋值或更新变量 */
      g_anim_step = 0u;  /* 赋值或更新变量 */
      g_last_anim_ms = HAL_GetTick();  /* 赋值或更新变量 */
      uart_send_str("OK DEV MODE REAL\r\n");  /* 调用函数执行对应操作 */
    }  /* 代码块结束 */
    else if ((match_token(q, "WAIT") && only_spaces_left(q + 4)) ||
             (match_token(q, "HOST") && only_spaces_left(q + 4)))  /* 判断另一个条件是否成立 */
    {  /* 代码块开始 */
      g_dev.force_mode = true;  /* 赋值或更新变量 */
      g_dev.forced_mode = APP_WAIT_FOR_HOST;  /* 赋值或更新变量 */
      g_anim_step = 0u;  /* 赋值或更新变量 */
      g_last_anim_ms = HAL_GetTick();  /* 赋值或更新变量 */
      uart_send_str("OK DEV MODE WAIT_HOST\r\n");  /* 调用函数执行对应操作 */
    }  /* 代码块结束 */
    else if ((match_token(q, "IDLE") && only_spaces_left(q + 4)) ||
             (match_token(q, "READY") && only_spaces_left(q + 5)))  /* 判断另一个条件是否成立 */
    {  /* 代码块开始 */
      g_dev.force_mode = true;  /* 赋值或更新变量 */
      g_dev.forced_mode = APP_IDLE;  /* 赋值或更新变量 */
      g_anim_step = 0u;  /* 赋值或更新变量 */
      g_last_anim_ms = HAL_GetTick();  /* 赋值或更新变量 */
      ws2812_clear();  /* 调用函数执行对应操作 */
      ws2812_show();  /* 调用函数执行对应操作 */
      uart_send_str("OK DEV MODE IDLE\r\n");  /* 调用函数执行对应操作 */
    }  /* 代码块结束 */
    else if (match_token(q, "COLOR") && only_spaces_left(q + 5))  /* 判断另一个条件是否成立 */
    {  /* 代码块开始 */
      g_dev.force_mode = true;  /* 赋值或更新变量 */
      g_dev.forced_mode = APP_STATIC_COLOR;  /* 赋值或更新变量 */
      g_anim_step = 0u;  /* 赋值或更新变量 */
      g_last_anim_ms = HAL_GetTick();  /* 赋值或更新变量 */
      g_static_color.r = 32u;  /* 赋值或更新变量 */
      g_static_color.g = 0u;  /* 赋值或更新变量 */
      g_static_color.b = 32u;  /* 赋值或更新变量 */
      ws2812_set_all(g_static_color.r, g_static_color.g, g_static_color.b);  /* 调用函数执行对应操作 */
      ws2812_show();  /* 调用函数执行对应操作 */
      uart_send_str("OK DEV MODE COLOR\r\n");  /* 调用函数执行对应操作 */
    }  /* 代码块结束 */
    else if ((match_token(q, "ANIM1") && only_spaces_left(q + 5)) ||
             (match_token(q, "1") && only_spaces_left(q + 1)))  /* 判断另一个条件是否成立 */
    {  /* 代码块开始 */
      g_dev.force_mode = true;  /* 赋值或更新变量 */
      g_dev.forced_mode = APP_ANIM_RED_CHASE;  /* 赋值或更新变量 */
      g_anim_step = 0u;  /* 赋值或更新变量 */
      g_last_anim_ms = HAL_GetTick();  /* 赋值或更新变量 */
      uart_send_str("OK DEV MODE ANIM1\r\n");  /* 调用函数执行对应操作 */
    }  /* 代码块结束 */
    else if ((match_token(q, "ANIM2") && only_spaces_left(q + 5)) ||
             (match_token(q, "2") && only_spaces_left(q + 1)))  /* 判断另一个条件是否成立 */
    {  /* 代码块开始 */
      g_dev.force_mode = true;  /* 赋值或更新变量 */
      g_dev.forced_mode = APP_ANIM_RAINBOW;  /* 赋值或更新变量 */
      g_anim_step = 0u;  /* 赋值或更新变量 */
      g_last_anim_ms = HAL_GetTick();  /* 赋值或更新变量 */
      uart_send_str("OK DEV MODE ANIM2\r\n");  /* 调用函数执行对应操作 */
    }  /* 代码块结束 */
    else if ((match_token(q, "ANIM3") && only_spaces_left(q + 5)) ||
             (match_token(q, "3") && only_spaces_left(q + 1)))  /* 判断另一个条件是否成立 */
    {  /* 代码块开始 */
      g_dev.force_mode = true;  /* 赋值或更新变量 */
      g_dev.forced_mode = APP_ANIM_RED_BREATH;  /* 赋值或更新变量 */
      g_anim_step = 0u;  /* 赋值或更新变量 */
      g_last_anim_ms = HAL_GetTick();  /* 赋值或更新变量 */
      uart_send_str("OK DEV MODE ANIM3\r\n");  /* 调用函数执行对应操作 */
    }  /* 代码块结束 */
    else if ((match_token(q, "ANIM4") && only_spaces_left(q + 5)) ||
             (match_token(q, "4") && only_spaces_left(q + 1)) ||
             (match_token(q, "CUSTOM") && only_spaces_left(q + 6)))  /* 判断另一个条件是否成立 */
    {  /* 代码块开始 */
      g_dev.force_mode = true;  /* 赋值或更新变量 */
      g_dev.forced_mode = APP_ANIM_CUSTOM_1;  /* 赋值或更新变量 */
      g_anim_step = 0u;  /* 赋值或更新变量 */
      g_last_anim_ms = HAL_GetTick();  /* 赋值或更新变量 */
      uart_send_str("OK DEV MODE ANIM4 CUSTOM\r\n");  /* 调用函数执行对应操作 */
    }  /* 代码块结束 */
    else  /* 处理否则情况 */
    {  /* 代码块开始 */
      uart_send_str("ERR DEV MODE NONE|WAIT|IDLE|COLOR|ANIM1|ANIM2|ANIM3|ANIM4\r\n");  /* 调用函数执行对应操作 */
    }  /* 代码块结束 */
    return true;  /* 返回函数结果 */
  }  /* 代码块结束 */

  uart_send_str("ERR DEV CMD\r\n");  /* 调用函数执行对应操作 */
  return true;  /* 返回函数结果 */
}  /* 代码块结束 */

/* -------------------------------------------------------------------------- */
/* 串口命令解析器                                                        */
/* -------------------------------------------------------------------------- */
static void process_uart_line(char *line)  /* 定义或声明函数 */
{  /* 代码块开始 */
  const char *p;  /* 执行一条语句 */
  uint32_t a, b, c;  /* 定义局部变量 */

  uppercase_ascii(line);  /* 调用函数或完成表达式 */
  p = skip_spaces(line);  /* 赋值或更新变量 */

  if (*p == '\0')  /* 判断条件是否成立 */
  {  /* 代码块开始 */
    return;  /* 返回函数结果 */
  }  /* 代码块结束 */

  g_last_cmd_ms = HAL_GetTick();  /* 赋值或更新变量 */

  if (process_dev_command(p))  /* 判断条件是否成立 */
  {  /* 代码块开始 */
    return;  /* 返回函数结果 */
  }  /* 代码块结束 */

  if (match_token(p, "OK") && only_spaces_left(p + 2))  /* 判断条件是否成立 */
  {  /* 代码块开始 */
    app_set_mode(APP_IDLE, true);  /* 退出等待主控状态并清灯 */
    uart_send_str("OK\r\n");  /* 调用函数执行对应操作 */
    return;  /* 返回函数结果 */
  }  /* 代码块结束 */

  if (match_token(p, "PING") && only_spaces_left(p + 4))  /* 判断条件是否成立 */
  {  /* 代码块开始 */
    uart_send_str("PONG\r\n");  /* 调用函数执行对应操作 */
    return;  /* 返回函数结果 */
  }  /* 代码块结束 */

  if (match_token(p, "STAT") && only_spaces_left(p + 4))  /* 判断条件是否成立 */
  {  /* 代码块开始 */
    uart_send_stat();  /* 调用函数执行对应操作 */
    return;  /* 返回函数结果 */
  }  /* 代码块结束 */

  if (g_mode == APP_WAIT_FOR_HOST)  /* 判断条件是否成立 */
  {  /* 代码块开始 */
    uart_send_str("ERR NEED_OK\r\n");  /* 调用函数执行对应操作 */
    return;  /* 返回函数结果 */
  }  /* 代码块结束 */

  if (match_token(p, "OFF") && only_spaces_left(p + 3))  /* 判断条件是否成立 */
  {  /* 代码块开始 */
    app_set_mode(APP_IDLE, true);  /* 赋值或更新变量 */
    uart_send_str("OK OFF\r\n");  /* 调用函数执行对应操作 */
    return;  /* 返回函数结果 */
  }  /* 代码块结束 */

  if (match_token(p, "COUNT"))  /* 判断条件是否成立 */
  {  /* 代码块开始 */
    p = skip_spaces(p + 5);  /* 赋值或更新变量 */
    if (parse_uint(&p, &a) && only_spaces_left(p) && a >= 1u && a <= WS2812_LED_MAX)  /* 判断条件是否成立 */
    {  /* 代码块开始 */
      g_led_count = (uint16_t)a;  /* 赋值或更新变量 */
      if (g_anim_step >= g_led_count)  /* 判断条件是否成立 */
      {  /* 代码块开始 */
        g_anim_step = 0u;  /* 赋值或更新变量 */
      }  /* 代码块结束 */
      ws2812_clear();  /* 调用函数执行对应操作 */
      ws2812_show();  /* 调用函数执行对应操作 */
      uart_send_str("OK COUNT\r\n");  /* 调用函数执行对应操作 */
    }  /* 代码块结束 */
    else  /* 处理否则情况 */
    {  /* 代码块开始 */
      uart_send_str("ERR COUNT 1..300\r\n");  /* 调用函数执行对应操作 */
    }  /* 代码块结束 */
    return;  /* 返回函数结果 */
  }  /* 代码块结束 */

  if (match_token(p, "BRI"))  /* 判断条件是否成立 */
  {  /* 代码块开始 */
    p = skip_spaces(p + 3);  /* 赋值或更新变量 */
    if (parse_uint(&p, &a) && only_spaces_left(p) && a <= 255u)  /* 判断条件是否成立 */
    {  /* 代码块开始 */
      g_brightness = (uint8_t)a;  /* 赋值或更新变量 */
      uart_send_str("OK BRI\r\n");  /* 调用函数执行对应操作 */
    }  /* 代码块结束 */
    else  /* 处理否则情况 */
    {  /* 代码块开始 */
      uart_send_str("ERR BRI 0..255\r\n");  /* 调用函数执行对应操作 */
    }  /* 代码块结束 */
    return;  /* 返回函数结果 */
  }  /* 代码块结束 */

  if (match_token(p, "COLOR"))  /* 判断条件是否成立 */
  {  /* 代码块开始 */
    p = skip_spaces(p + 5);  /* 赋值或更新变量 */
    if (parse_uint(&p, &a) && parse_uint(&p, &b) && parse_uint(&p, &c) && only_spaces_left(p) &&
        a <= 255u && b <= 255u && c <= 255u)  /* 赋值或更新变量 */
    {  /* 代码块开始 */
      g_static_color.r = (uint8_t)a;  /* 赋值或更新变量 */
      g_static_color.g = (uint8_t)b;  /* 赋值或更新变量 */
      g_static_color.b = (uint8_t)c;  /* 赋值或更新变量 */
      app_set_mode(APP_STATIC_COLOR, false);  /* 统一清动画状态 */
      ws2812_set_all(g_static_color.r, g_static_color.g, g_static_color.b);  /* 调用函数执行对应操作 */
      ws2812_show();  /* 调用函数执行对应操作 */
      uart_send_str("OK COLOR\r\n");  /* 调用函数执行对应操作 */
    }  /* 代码块结束 */
    else  /* 处理否则情况 */
    {  /* 代码块开始 */
      uart_send_str("ERR COLOR R G B\r\n");  /* 调用函数执行对应操作 */
    }  /* 代码块结束 */
    return;  /* 返回函数结果 */
  }  /* 代码块结束 */

  if (match_token(p, "ANIM"))  /* 判断条件是否成立 */
  {  /* 代码块开始 */
    p = skip_spaces(p + 4);  /* 赋值或更新变量 */
    if (parse_uint(&p, &a) && only_spaces_left(p))  /* 判断条件是否成立 */
    {  /* 代码块开始 */
      if (a == 0u)  /* 判断条件是否成立 */
      {  /* 代码块开始 */
        app_set_mode(APP_IDLE, true);  /* 赋值或更新变量 */
        uart_send_str("OK ANIM OFF\r\n");  /* 调用函数执行对应操作 */
      }  /* 代码块结束 */
      else if (a == 1u)  /* 判断另一个条件是否成立 */
      {  /* 代码块开始 */
        app_set_mode(APP_ANIM_RED_CHASE, false);  /* 赋值或更新变量 */
        uart_send_str("OK ANIM RED_CHASE\r\n");  /* 调用函数执行对应操作 */
      }  /* 代码块结束 */
      else if (a == 2u)  /* 判断另一个条件是否成立 */
      {  /* 代码块开始 */
        app_set_mode(APP_ANIM_RAINBOW, false);  /* 赋值或更新变量 */
        uart_send_str("OK ANIM RAINBOW\r\n");  /* 调用函数执行对应操作 */
      }  /* 代码块结束 */
      else if (a == 3u)  /* 判断另一个条件是否成立 */
      {  /* 代码块开始 */
        app_set_mode(APP_ANIM_RED_BREATH, false);  /* 赋值或更新变量 */
        uart_send_str("OK ANIM RED_BREATH\r\n");  /* 调用函数执行对应操作 */
      }  /* 代码块结束 */
      else if (a == 4u)  /* 判断另一个条件是否成立 */
      {  /* 代码块开始 */
        /* ECU CUSTOM ANIMATION STEP 3/5:
         * 命令 "ANIM 4" 会进入 APP_ANIM_CUSTOM_1。
         * When adding more animations, map new numbers here, for example:
         *   ANIM 5 -> APP_ANIM_CUSTOM_2
         */
        app_set_mode(APP_ANIM_CUSTOM_1, false);  /* 赋值或更新变量 */
        uart_send_str("OK ANIM CUSTOM_1\r\n");  /* 调用函数执行对应操作 */
      }  /* 代码块结束 */
      else  /* 处理否则情况 */
      {  /* 代码块开始 */
        uart_send_str("ERR ANIM 0..4\r\n");  /* 调用函数执行对应操作 */
      }  /* 代码块结束 */
    }  /* 代码块结束 */
    else  /* 处理否则情况 */
    {  /* 代码块开始 */
      uart_send_str("ERR ANIM 0..4\r\n");  /* 调用函数执行对应操作 */
    }  /* 代码块结束 */
    return;  /* 返回函数结果 */
  }  /* 代码块结束 */

  uart_send_str("ERR UNKNOWN\r\n");  /* 调用函数执行对应操作 */
}  /* 代码块结束 */

/* -------------------------------------------------------------------------- */
/* 电控自定义动画代码区                                                   */
/* -------------------------------------------------------------------------- */
/* 电控自定义动画步骤 5/5：修改这个函数即可定义 ANIM 4。
 *
 * 自定义动画编写流程：
 *   1. 先决定动画帧间隔，通常为 20~100ms，可修改 CUSTOM_ANIM_1_PERIOD_MS。
 *   2. 到达帧间隔后，将 g_last_anim_ms 置为 now。
 *   3. 使用 ws2812_clear()/ws2812_set_pixel() 清空或重写灯带缓存。
 *   4. 递增 g_anim_step，让下一帧产生变化。
 *   5. 一帧准备好后，将 *should_update 设置为 true。
 *
 * 编写规则：
 *   - 不要在这里调用 HAL_Delay()。
 *   - 不要在这里调用 ws2812_show()，animation_task() 会统一调用一次。
 *   - 只允许写入 0..g_led_count-1 范围内的灯珠。
 *   - RGB 数值范围为 0..255，全局亮度会在 ws2812_show() 中统一叠加。
 *
 * 当前示例效果：
 *   ANIM 4 显示蓝色扫描点和柔和拖尾。
 * 如需替换为电控最终动画，直接改写下面的示例逻辑。
 */
static void custom_animation_1_task(uint32_t now, bool *should_update)  /* 定义或声明函数 */
{  /* 代码块开始 */
  uint16_t head;  /* 定义局部变量 */

  if ((now - g_last_anim_ms) < CUSTOM_ANIM_1_PERIOD_MS)  /* 判断条件是否成立 */
  {  /* 代码块开始 */
    return;  /* 返回函数结果 */
  }  /* 代码块结束 */

  g_last_anim_ms = now;  /* 赋值或更新变量 */
  *should_update = true;

  ws2812_clear();  /* 调用函数执行对应操作 */

  if (g_led_count == 0u)  /* 判断条件是否成立 */
  {  /* 代码块开始 */
    return;  /* 返回函数结果 */
  }  /* 代码块结束 */

  head = (uint16_t)(g_anim_step % g_led_count);  /* 赋值或更新变量 */

  /* Main dot. */
  ws2812_set_pixel(head, 0u, 0u, 255u);  /* 调用函数执行对应操作 */

  /* Two-pixel tail. Keep modulo arithmetic so it works for any LED count. */
  if (g_led_count > 1u)  /* 判断条件是否成立 */
  {  /* 代码块开始 */
    ws2812_set_pixel((uint16_t)((head + g_led_count - 1u) % g_led_count), 0u, 0u, 64u);  /* 调用函数执行对应操作 */
  }  /* 代码块结束 */
  if (g_led_count > 2u)  /* 判断条件是否成立 */
  {  /* 代码块开始 */
    ws2812_set_pixel((uint16_t)((head + g_led_count - 2u) % g_led_count), 0u, 0u, 16u);  /* 调用函数执行对应操作 */
  }  /* 代码块结束 */

  g_anim_step++;  /* 执行一条语句 */
}  /* 代码块结束 */

/* -------------------------------------------------------------------------- */
/* WS2812 驱动：SPI1 MOSI 使用 PA7，SPI 频率 4MHz，每个 WS2812 位用 4 个 SPI 位编码             */
/* Encoding: 0 -> 1000, 1 -> 1110                                             */
/* -------------------------------------------------------------------------- */
static void ws2812_set_pixel(uint16_t index, uint8_t r, uint8_t g, uint8_t b)  /* 定义或声明函数 */
{  /* 代码块开始 */
  if (index >= g_led_count)  /* 只允许写入当前有效灯珠数量以内 */
  {  /* 代码块开始 */
    return;  /* 返回函数结果 */
  }  /* 代码块结束 */

  g_leds[index].r = r;  /* 赋值或更新变量 */
  g_leds[index].g = g;  /* 赋值或更新变量 */
  g_leds[index].b = b;  /* 赋值或更新变量 */
}  /* 代码块结束 */

static void ws2812_set_all(uint8_t r, uint8_t g, uint8_t b)  /* 定义或声明函数 */
{  /* 代码块开始 */
  for (uint16_t i = 0; i < g_led_count; i++)  /* 执行循环 */
  {  /* 代码块开始 */
    ws2812_set_pixel(i, r, g, b);  /* 调用函数执行对应操作 */
  }  /* 代码块结束 */
}  /* 代码块结束 */

static void ws2812_clear(void)  /* 定义或声明函数 */
{  /* 代码块开始 */
  memset(g_leds, 0, sizeof(g_leds));  /* 清空全部缓存，避免 COUNT 变大后旧灯色残留 */
}  /* 代码块结束 */

static uint8_t ws2812_encode_pair(uint8_t byte, uint8_t high_bit)  /* 定义或声明函数 */
{  /* 代码块开始 */
  uint8_t first = (byte & (uint8_t)(1u << high_bit)) ? 0xEu : 0x8u;  /* 定义局部变量 */
  uint8_t second = (byte & (uint8_t)(1u << (high_bit - 1u))) ? 0xEu : 0x8u;  /* 定义局部变量 */
  return (uint8_t)((first << 4u) | second);  /* 返回函数结果 */
}  /* 代码块结束 */

static uint8_t scale_component(uint8_t value)  /* 定义或声明函数 */
{  /* 代码块开始 */
  return (uint8_t)(((uint16_t)value * (uint16_t)g_brightness + 127u) / 255u);  /* 返回函数结果 */
}  /* 代码块结束 */

static void ws2812_encode_color_byte(uint8_t value, uint8_t **pp)  /* 定义或声明函数 */
{  /* 代码块开始 */
  uint8_t *p = *pp;  /* 定义局部变量 */
  p[0] = ws2812_encode_pair(value, 7u);  /* 赋值或更新变量 */
  p[1] = ws2812_encode_pair(value, 5u);  /* 赋值或更新变量 */
  p[2] = ws2812_encode_pair(value, 3u);  /* 赋值或更新变量 */
  p[3] = ws2812_encode_pair(value, 1u);  /* 赋值或更新变量 */
  *pp = p + 4u;
}  /* 代码块结束 */

static void ws2812_force_reset_low(void)  /* 定义或声明函数 */
{  /* 代码块开始 */
  memset(g_ws2812_spi_buf, 0, WS2812_RESET_BYTES);  /* 低电平 reset 码 */
  (void)HAL_SPI_Transmit(&hspi1, g_ws2812_spi_buf, WS2812_RESET_BYTES, 20u);  /* 调用函数或完成表达式 */
}  /* 代码块结束 */

static void ws2812_show(void)  /* 定义或声明函数 */
{  /* 代码块开始 */
  uint8_t *p = g_ws2812_spi_buf;  /* 定义局部变量 */
  uint16_t count = g_led_count;  /* 定义局部变量 */

  if (count > WS2812_LED_MAX)  /* 双保险，防止异常写坏 SPI 缓冲 */
  {  /* 代码块开始 */
    count = WS2812_LED_MAX;  /* 赋值或更新变量 */
  }  /* 代码块结束 */

  /* 前置 reset：快速切模式、异常恢复、上电第一帧时更稳。 */
  memset(p, 0, WS2812_RESET_BYTES);  /* 调用函数或完成表达式 */
  p += WS2812_RESET_BYTES;  /* 赋值或更新变量 */

  for (uint16_t i = 0; i < count; i++)  /* 执行循环 */
  {  /* 代码块开始 */
    uint8_t g = scale_component(g_leds[i].g);  /* 定义局部变量 */
    uint8_t r = scale_component(g_leds[i].r);  /* 定义局部变量 */
    uint8_t b = scale_component(g_leds[i].b);  /* 定义局部变量 */

    /* WS2812 颜色顺序为 GRB。 */
    ws2812_encode_color_byte(g, &p);  /* 调用函数执行对应操作 */
    ws2812_encode_color_byte(r, &p);  /* 调用函数执行对应操作 */
    ws2812_encode_color_byte(b, &p);  /* 调用函数执行对应操作 */
  }  /* 代码块结束 */

  /* 后置 reset：保持数据线低电平超过 50us；4MHz 下发送 80 字节约为 160us。 */
  memset(p, 0, WS2812_RESET_BYTES);  /* 调用函数或完成表达式 */
  p += WS2812_RESET_BYTES;  /* 赋值或更新变量 */

  (void)HAL_SPI_Transmit(&hspi1, g_ws2812_spi_buf, (uint16_t)(p - g_ws2812_spi_buf), 100u);  /* 调用函数或完成表达式 */
}  /* 代码块结束 */

static Rgb_t wheel(uint8_t pos)  /* 调用函数或完成表达式 */
{  /* 代码块开始 */
  Rgb_t c;  /* 定义局部变量 */

  if (pos < 85u)  /* 判断条件是否成立 */
  {  /* 代码块开始 */
    c.r = (uint8_t)(255u - pos * 3u);  /* 赋值或更新变量 */
    c.g = (uint8_t)(pos * 3u);  /* 赋值或更新变量 */
    c.b = 0u;  /* 赋值或更新变量 */
  }  /* 代码块结束 */
  else if (pos < 170u)  /* 判断另一个条件是否成立 */
  {  /* 代码块开始 */
    pos = (uint8_t)(pos - 85u);  /* 赋值或更新变量 */
    c.r = 0u;  /* 赋值或更新变量 */
    c.g = (uint8_t)(255u - pos * 3u);  /* 赋值或更新变量 */
    c.b = (uint8_t)(pos * 3u);  /* 赋值或更新变量 */
  }  /* 代码块结束 */
  else  /* 处理否则情况 */
  {  /* 代码块开始 */
    pos = (uint8_t)(pos - 170u);  /* 赋值或更新变量 */
    c.r = (uint8_t)(pos * 3u);  /* 赋值或更新变量 */
    c.g = 0u;  /* 赋值或更新变量 */
    c.b = (uint8_t)(255u - pos * 3u);  /* 赋值或更新变量 */
  }  /* 代码块结束 */

  return c;  /* 返回函数结果 */
}  /* 代码块结束 */

/* -------------------------------------------------------------------------- */
/* ADC 采样相关函数                                                                        */
/* -------------------------------------------------------------------------- */
static uint16_t adc_read_raw_avg(uint32_t channel)  /* 定义或声明函数 */
{
	ADC_ChannelConfTypeDef sConfig = {0};
	uint32_t sum = 0u;
	uint32_t valid_count = 0u;
	uint16_t raw = 0u;

	sConfig.Channel = channel;
	sConfig.Rank = ADC_REGULAR_RANK_1;
	sConfig.SamplingTime = ADC_SAMPLINGTIME_COMMON_1;

	if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
	{
		return 0u;
	}

	/* 切换 ADC 通道后第一次采样可能带有上一个通道残留，所以丢弃 */
	if (HAL_ADC_Start(&hadc1) == HAL_OK)
	{
		if (HAL_ADC_PollForConversion(&hadc1, 5u) == HAL_OK)
		{
			(void)HAL_ADC_GetValue(&hadc1);
		}

		(void)HAL_ADC_Stop(&hadc1);
	}

	for (uint8_t i = 0u; i < ADC_SAMPLE_COUNT; i++)
	{
		if (HAL_ADC_Start(&hadc1) != HAL_OK)
		{

			continue;
		}

		if (HAL_ADC_PollForConversion(&hadc1, 5u) == HAL_OK)
		{
			raw = (uint16_t)HAL_ADC_GetValue(&hadc1);
			sum += raw;
			valid_count++;
		}
		(void)HAL_ADC_Stop(&hadc1);
	}

	if (valid_count == 0u)
	{
		return 0u;
	}

	return (uint16_t)((sum + (valid_count / 2u)) / valid_count);
}
static uint32_t adc_raw_to_mv(uint16_t raw)  /* 定义或声明函数 */
{  /* 代码块开始 */
  return ((uint32_t)raw * ADC_VREF_MV + (ADC_FULL_SCALE / 2u)) / ADC_FULL_SCALE;  /* 返回函数结果 */
}  /* 代码块结束 */

static uint32_t read_bus_mv(void)  /* 定义或声明函数 */
{  /* 代码块开始 */
 uint32_t adc_mv = adc_raw_to_mv(adc_read_raw_avg(ADC_CHANNEL_0));  /* PA0 */
 uint32_t bus_mv = (adc_mv * (VBUS_DIV_TOP_OHM + VBUS_DIV_BOTTOM_OHM)) / VBUS_DIV_BOTTOM_OHM;

 return (uint32_t)(((uint64_t)bus_mv * VBUS_CAL_REAL_MV + (VBUS_CAL_ADC_MV / 2u)) / VBUS_CAL_ADC_MV);  /* 返回函数结果 */
}  /* 代码块结束 */

static uint32_t read_current_ma(void)  /* 定义或声明函数 */
{  /* 代码块开始 */
  uint32_t adc_mv = adc_raw_to_mv(adc_read_raw_avg(ADC_CHANNEL_6));  /* PA6 */
  uint32_t denominator = ISENSE_SHUNT_MOHM * ISENSE_GAIN;  /* 定义局部变量 */

  if (denominator == 0u)  /* 判断条件是否成立 */
  {  /* 代码块开始 */
    return 0u;  /* 返回函数结果 */
  }  /* 代码块结束 */

  /* 电流计算：I(mA) = Vadc(mV) * 1000 / (采样电阻mΩ * 放大倍数) */
	uint32_t current_ma = (adc_mv * 1000u + (denominator / 2u)) / denominator;

	current_ma = (current_ma * ISENSE_CAL_NUM + (ISENSE_CAL_DEN / 2u)) / ISENSE_CAL_DEN;

	return current_ma;
}  /* 代码块结束 */

/* -------------------------------------------------------------------------- */
/* GPIO 与串口辅助函数                                                        */
/* -------------------------------------------------------------------------- */
static void status_led_set(bool on)  /* 定义或声明函数 */
{  /* 代码块开始 */
  HAL_GPIO_WritePin(STATUS_LED_GPIO_PORT, STATUS_LED_PIN, on ? GPIO_PIN_RESET : GPIO_PIN_SET);  /* 调用函数执行对应操作 */
}  /* 代码块结束 */

static void uart_send_str(const char *s)  /* 定义或声明函数 */
{  /* 代码块开始 */
  (void)HAL_UART_Transmit(&huart2, (uint8_t *)s, (uint16_t)strlen(s), 50u);  /* 调用函数或完成表达式 */
}  /* 代码块结束 */

static uint32_t append_u32(char *dst, uint32_t pos, uint32_t value)  /* 定义或声明函数 */
{  /* 代码块开始 */
  char tmp[10];  /* 定义局部变量 */
  uint32_t n = 0;  /* 定义局部变量 */

  if (value == 0u)  /* 判断条件是否成立 */
  {  /* 代码块开始 */
    dst[pos++] = '0';  /* 赋值或更新变量 */
    return pos;  /* 返回函数结果 */
  }  /* 代码块结束 */

  while (value > 0u && n < sizeof(tmp))  /* 执行循环 */
  {  /* 代码块开始 */
    tmp[n++] = (char)('0' + (value % 10u));  /* 赋值或更新变量 */
    value /= 10u;  /* 赋值或更新变量 */
  }  /* 代码块结束 */

  while (n > 0u)  /* 执行循环 */
  {  /* 代码块开始 */
    dst[pos++] = tmp[--n];  /* 赋值或更新变量 */
  }  /* 代码块结束 */

  return pos;  /* 返回函数结果 */
}  /* 代码块结束 */

static void uart_send_stat(void)  /* 定义或声明函数 */
{  /* 代码块开始 */
  char out[64];  /* 定义局部变量 */
  uint32_t pos = 0;  /* 定义局部变量 */

  /* STAT 纯数据输出，字段顺序：电压mV 电流mA 真实故障 有效故障。
   * 示例：24000 800 0 0
   * 注意：U < 6000 且故障为 0 时，表示 Type-C/USB 调试供电。
   */
  pos = append_u32(out, pos, g_bus_mv_filtered);  /* 赋值或更新变量 */
  out[pos++] = ' ';  /* 赋值或更新变量 */
  pos = append_u32(out, pos, g_current_ma_filtered);  /* 赋值或更新变量 */
  out[pos++] = ' ';  /* 赋值或更新变量 */
  pos = append_u32(out, pos, (uint32_t)g_fault);  /* 赋值或更新变量 */
  out[pos++] = ' ';  /* 赋值或更新变量 */
  pos = append_u32(out, pos, (uint32_t)get_effective_fault());  /* 赋值或更新变量 */
  memcpy(&out[pos], "\r\n", 2u); pos += 2u;  /* 赋值或更新变量 */

  (void)HAL_UART_Transmit(&huart2, (uint8_t *)out, (uint16_t)pos, 50u);  /* 调用函数或完成表达式 */
}  /* 代码块结束 */

static void uart_send_dev_status(void)  /* 定义或声明函数 */
{  /* 代码块开始 */
  uart_send_str("DEV=");  /* 调用函数执行对应操作 */
  uart_send_str(g_dev.enabled ? "ON" : "OFF");  /* 调用函数执行对应操作 */
  uart_send_str(" REAL_FAULT=");  /* 调用函数执行对应操作 */
  uart_send_str(fault_to_str(g_fault));  /* 调用函数执行对应操作 */
  uart_send_str(" EFFECTIVE_FAULT=");  /* 调用函数执行对应操作 */
  uart_send_str(fault_to_str(get_effective_fault()));  /* 调用函数执行对应操作 */
  uart_send_str(" REAL_MODE=");  /* 调用函数执行对应操作 */
  uart_send_str(mode_to_str(g_mode));  /* 调用函数执行对应操作 */
  uart_send_str(" EFFECTIVE_MODE=");  /* 调用函数执行对应操作 */
  uart_send_str(mode_to_str(get_effective_mode()));  /* 调用函数执行对应操作 */
  uart_send_str(" FORCE_MODE=");  /* 调用函数执行对应操作 */
  uart_send_str(g_dev.force_mode ? "YES" : "NO");  /* 调用函数执行对应操作 */
  uart_send_str("\r\n");  /* 调用函数执行对应操作 */
}  /* 代码块结束 */

static void uppercase_ascii(char *s)  /* 定义或声明函数 */
{  /* 代码块开始 */
  while (*s != '\0')  /* 执行循环 */
  {  /* 代码块开始 */
    if (*s >= 'a' && *s <= 'z')  /* 判断条件是否成立 */
    {  /* 代码块开始 */
      *s = (char)(*s - ('a' - 'A'));
    }  /* 代码块结束 */
    s++;  /* 执行一条语句 */
  }  /* 代码块结束 */
}  /* 代码块结束 */

static const char *skip_spaces(const char *s)  /* 定义或声明函数 */
{  /* 代码块开始 */
  while (*s == ' ' || *s == '\t')  /* 执行循环 */
  {  /* 代码块开始 */
    s++;  /* 执行一条语句 */
  }  /* 代码块结束 */
  return s;  /* 返回函数结果 */
}  /* 代码块结束 */

static bool is_token_end(char ch)  /* 定义或声明函数 */
{  /* 代码块开始 */
  return (ch == '\0') || (ch == ' ') || (ch == '\t');  /* 返回函数结果 */
}  /* 代码块结束 */

static bool match_token(const char *s, const char *token)  /* 定义或声明函数 */
{  /* 代码块开始 */
  uint32_t n = (uint32_t)strlen(token);  /* 定义局部变量 */
  return (strncmp(s, token, n) == 0) && is_token_end(s[n]);  /* 返回函数结果 */
}  /* 代码块结束 */

static bool only_spaces_left(const char *s)  /* 定义或声明函数 */
{  /* 代码块开始 */
  s = skip_spaces(s);  /* 赋值或更新变量 */
  return *s == '\0';  /* 返回函数结果 */
}  /* 代码块结束 */

static bool parse_uint(const char **ps, uint32_t *value)  /* 定义或声明函数 */
{  /* 代码块开始 */
  const char *s = skip_spaces(*ps);  /* 赋值或更新变量 */
  uint32_t v = 0;  /* 定义局部变量 */
  bool has_digit = false;  /* 定义局部变量 */

  while (*s >= '0' && *s <= '9')  /* 执行循环 */
  {  /* 代码块开始 */
    has_digit = true;  /* 赋值或更新变量 */
    v = v * 10u + (uint32_t)(*s - '0');  /* 赋值或更新变量 */
    s++;  /* 执行一条语句 */
  }  /* 代码块结束 */

  if (!has_digit)  /* 判断条件是否成立 */
  {  /* 代码块开始 */
    return false;  /* 返回函数结果 */
  }  /* 代码块结束 */

  *value = v;
  *ps = s;
  return true;  /* 返回函数结果 */
}  /* 代码块结束 */

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)  /* 定义或声明函数 */
{  /* 代码块开始 */
  if (huart->Instance == USART2)  /* 判断条件是否成立 */
  {  /* 代码块开始 */
    char ch = (char)g_uart_rx_byte;  /* 定义局部变量 */

    if (g_uart_drop_until_eol != 0u)  /* 超长帧丢弃直到行尾 */
    {  /* 代码块开始 */
      if (ch == '\r' || ch == '\n')  /* 判断条件是否成立 */
      {  /* 代码块开始 */
        g_uart_drop_until_eol = 0u;  /* 赋值或更新变量 */
        g_uart_rx_len = 0u;  /* 赋值或更新变量 */
      }  /* 代码块结束 */
    }  /* 代码块结束 */
    else if (g_uart_line_ready == 0u)  /* 判断条件是否成立 */
    {  /* 代码块开始 */
      if (ch == '\r' || ch == '\n')  /* 判断条件是否成立 */
      {  /* 代码块开始 */
        if (g_uart_rx_len > 0u)  /* 判断条件是否成立 */
        {  /* 代码块开始 */
          g_uart_line[g_uart_rx_len] = '\0';  /* 赋值或更新变量 */
          g_uart_line_ready = 1u;  /* 赋值或更新变量 */
          g_uart_rx_len = 0u;  /* 赋值或更新变量 */
        }  /* 代码块结束 */
      }  /* 代码块结束 */
      else if (g_uart_rx_len < (UART_LINE_MAX - 1u))  /* 判断另一个条件是否成立 */
      {  /* 代码块开始 */
        g_uart_line[g_uart_rx_len++] = ch;  /* 赋值或更新变量 */
      }  /* 代码块结束 */
      else  /* 超长帧：清空并丢弃到换行，避免残留字符拼成假命令 */
      {  /* 代码块开始 */
        g_uart_rx_len = 0u;  /* 赋值或更新变量 */
        g_uart_drop_until_eol = 1u;  /* 赋值或更新变量 */
        if (g_uart_overflow_count != 255u)  /* 判断条件是否成立 */
        {  /* 代码块开始 */
          g_uart_overflow_count++;  /* 饱和计数 */
        }  /* 代码块结束 */
      }  /* 代码块结束 */
    }  /* 代码块结束 */

    (void)HAL_UART_Receive_IT(&huart2, &g_uart_rx_byte, 1u);  /* 调用函数或完成表达式 */
  }  /* 代码块结束 */
}  /* 代码块结束 */

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)  /* 定义或声明函数 */
{  /* 代码块开始 */
  if (huart->Instance == USART2)  /* 判断条件是否成立 */
  {  /* 代码块开始 */
    g_uart_rx_len = 0u;  /* 赋值或更新变量 */
    g_uart_line_ready = 0u;  /* 赋值或更新变量 */
    g_uart_drop_until_eol = 0u;  /* 赋值或更新变量 */
    (void)HAL_UART_Receive_IT(&huart2, &g_uart_rx_byte, 1u);  /* 调用函数或完成表达式 */
  }  /* 代码块结束 */
}  /* 代码块结束 */

/* -------------------------------------------------------------------------- */
/* Peripheral initialization                                                   */
/* -------------------------------------------------------------------------- */
void SystemClock_Config(void)  /* 定义或声明函数 */
{  /* 代码块开始 */
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};  /* 赋值或更新变量 */
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};  /* 赋值或更新变量 */

  HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1);  /* 调用函数执行对应操作 */

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;  /* 赋值或更新变量 */
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;  /* 赋值或更新变量 */
  RCC_OscInitStruct.HSIDiv = RCC_HSI_DIV1;  /* 赋值或更新变量 */
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;  /* 赋值或更新变量 */
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;  /* 赋值或更新变量 */
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)  /* 判断条件是否成立 */
  {  /* 代码块开始 */
    Error_Handler();  /* 调用函数或完成表达式 */
  }  /* 代码块结束 */

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1;  /* 赋值或更新变量 */
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;  /* 赋值或更新变量 */
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;  /* 赋值或更新变量 */
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;  /* 赋值或更新变量 */

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)  /* 判断条件是否成立 */
  {  /* 代码块开始 */
    Error_Handler();  /* 调用函数或完成表达式 */
  }  /* 代码块结束 */
}  /* 代码块结束 */

static void MX_ADC1_Init(void)  /* 定义或声明函数 */
{  /* 代码块开始 */
  ADC_ChannelConfTypeDef sConfig = {0};  /* 赋值或更新变量 */

  hadc1.Instance = ADC1;  /* 赋值或更新变量 */
  hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV2;  /* 赋值或更新变量 */
  hadc1.Init.Resolution = ADC_RESOLUTION_12B;  /* 赋值或更新变量 */
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;  /* 赋值或更新变量 */
  hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;  /* 赋值或更新变量 */
  hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;  /* 赋值或更新变量 */
  hadc1.Init.LowPowerAutoWait = DISABLE;  /* 赋值或更新变量 */
  hadc1.Init.LowPowerAutoPowerOff = DISABLE;  /* 赋值或更新变量 */
  hadc1.Init.ContinuousConvMode = DISABLE;  /* 赋值或更新变量 */
  hadc1.Init.NbrOfConversion = 1;  /* 赋值或更新变量 */
  hadc1.Init.DiscontinuousConvMode = DISABLE;  /* 赋值或更新变量 */
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;  /* 赋值或更新变量 */
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;  /* 赋值或更新变量 */
  hadc1.Init.DMAContinuousRequests = DISABLE;  /* 赋值或更新变量 */
  hadc1.Init.Overrun = ADC_OVR_DATA_PRESERVED;  /* 赋值或更新变量 */
  hadc1.Init.SamplingTimeCommon1 = ADC_SAMPLETIME_160CYCLES_5;  /* 赋值或更新变量 */
  hadc1.Init.SamplingTimeCommon2 = ADC_SAMPLETIME_160CYCLES_5;  /* 赋值或更新变量 */
  hadc1.Init.OversamplingMode = DISABLE;  /* 赋值或更新变量 */
  hadc1.Init.TriggerFrequencyMode = ADC_TRIGGER_FREQ_HIGH;  /* 赋值或更新变量 */
  if (HAL_ADC_Init(&hadc1) != HAL_OK)  /* 判断条件是否成立 */
  {  /* 代码块开始 */
    Error_Handler();  /* 调用函数或完成表达式 */
  }  /* 代码块结束 */

  sConfig.Channel = ADC_CHANNEL_0;  /* 赋值或更新变量 */
  sConfig.Rank = ADC_REGULAR_RANK_1;  /* 赋值或更新变量 */
  sConfig.SamplingTime = ADC_SAMPLINGTIME_COMMON_1;  /* 赋值或更新变量 */
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)  /* 判断条件是否成立 */
  {  /* 代码块开始 */
    Error_Handler();  /* 调用函数或完成表达式 */
  }  /* 代码块结束 */

  (void)HAL_ADCEx_Calibration_Start(&hadc1);  /* 调用函数或完成表达式 */
}  /* 代码块结束 */

static void MX_SPI1_Init(void)  /* 定义或声明函数 */
{  /* 代码块开始 */
  __HAL_RCC_SPI1_CLK_ENABLE();  /* 调用函数或完成表达式 */

  hspi1.Instance = SPI1;  /* 赋值或更新变量 */
  hspi1.Init.Mode = SPI_MODE_MASTER;  /* 赋值或更新变量 */
  hspi1.Init.Direction = SPI_DIRECTION_1LINE;  /* 赋值或更新变量 */
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;  /* 赋值或更新变量 */
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;  /* 赋值或更新变量 */
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;  /* 赋值或更新变量 */
  hspi1.Init.NSS = SPI_NSS_SOFT;  /* 赋值或更新变量 */
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_4; /* 16MHz 四分频得到 4MHz */  /* 中文注释：赋值或更新变量 */
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;  /* 赋值或更新变量 */
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;  /* 赋值或更新变量 */
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;  /* 赋值或更新变量 */
  hspi1.Init.CRCPolynomial = 7;  /* 赋值或更新变量 */
  hspi1.Init.CRCLength = SPI_CRC_LENGTH_DATASIZE;  /* 赋值或更新变量 */
  hspi1.Init.NSSPMode = SPI_NSS_PULSE_DISABLE;  /* 赋值或更新变量 */
  if (HAL_SPI_Init(&hspi1) != HAL_OK)  /* 判断条件是否成立 */
  {  /* 代码块开始 */
    Error_Handler();  /* 调用函数或完成表达式 */
  }  /* 代码块结束 */
}  /* 代码块结束 */

static void MX_USART2_UART_Init(void)  /* 定义或声明函数 */
{  /* 代码块开始 */
  __HAL_RCC_USART2_CLK_ENABLE();  /* 调用函数或完成表达式 */

  huart2.Instance = USART2;  /* 赋值或更新变量 */
  huart2.Init.BaudRate = UART_BAUDRATE;  /* 赋值或更新变量 */
  huart2.Init.WordLength = UART_WORDLENGTH_8B;  /* 赋值或更新变量 */
  huart2.Init.StopBits = UART_STOPBITS_1;  /* 赋值或更新变量 */
  huart2.Init.Parity = UART_PARITY_NONE;  /* 赋值或更新变量 */
  huart2.Init.Mode = UART_MODE_TX_RX;  /* 赋值或更新变量 */
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;  /* 赋值或更新变量 */
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;  /* 赋值或更新变量 */
  huart2.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;  /* 赋值或更新变量 */
  huart2.Init.ClockPrescaler = UART_PRESCALER_DIV1;  /* 赋值或更新变量 */
  huart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;  /* 赋值或更新变量 */
  if (HAL_UART_Init(&huart2) != HAL_OK)  /* 判断条件是否成立 */
  {  /* 代码块开始 */
    Error_Handler();  /* 调用函数或完成表达式 */
  }  /* 代码块结束 */

  if (HAL_UARTEx_SetTxFifoThreshold(&huart2, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)  /* 判断条件是否成立 */
  {  /* 代码块开始 */
    Error_Handler();  /* 调用函数或完成表达式 */
  }  /* 代码块结束 */
  if (HAL_UARTEx_SetRxFifoThreshold(&huart2, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)  /* 判断条件是否成立 */
  {  /* 代码块开始 */
    Error_Handler();  /* 调用函数或完成表达式 */
  }  /* 代码块结束 */
  if (HAL_UARTEx_DisableFifoMode(&huart2) != HAL_OK)  /* 判断条件是否成立 */
  {  /* 代码块开始 */
    Error_Handler();  /* 调用函数或完成表达式 */
  }  /* 代码块结束 */

  HAL_NVIC_SetPriority(USART2_IRQn, 0, 0);  /* 调用函数执行对应操作 */
  HAL_NVIC_EnableIRQ(USART2_IRQn);  /* 调用函数执行对应操作 */
}  /* 代码块结束 */

static void MX_GPIO_Init(void)  /* 定义或声明函数 */
{  /* 代码块开始 */
  GPIO_InitTypeDef GPIO_InitStruct = {0};  /* 赋值或更新变量 */

  __HAL_RCC_GPIOA_CLK_ENABLE();  /* 调用函数或完成表达式 */
  __HAL_RCC_GPIOB_CLK_ENABLE();  /* 调用函数或完成表达式 */

  /* PA0 电压采样、PA6 电流采样：必须配置为模拟输入，无上下拉。 */
  GPIO_InitStruct.Pin = GPIO_PIN_0 | GPIO_PIN_6;  /* 赋值或更新变量 */
  GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;  /* 赋值或更新变量 */
  GPIO_InitStruct.Pull = GPIO_NOPULL;  /* 赋值或更新变量 */
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);  /* 调用函数执行对应操作 */

  /* PB3 状态灯：低电平点亮，默认先熄灭。 */
  HAL_GPIO_WritePin(STATUS_LED_GPIO_PORT, STATUS_LED_PIN, GPIO_PIN_SET);  /* 调用函数执行对应操作 */
  GPIO_InitStruct.Pin = STATUS_LED_PIN;  /* 赋值或更新变量 */
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;  /* 赋值或更新变量 */
  GPIO_InitStruct.Pull = GPIO_NOPULL;  /* 赋值或更新变量 */
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;  /* 赋值或更新变量 */
  HAL_GPIO_Init(STATUS_LED_GPIO_PORT, &GPIO_InitStruct);  /* 调用函数执行对应操作 */

  /* USART2: PA2 TX, PA3 RX. PA1 is left as USART2 RTS alternate if routed on board. */
  GPIO_InitStruct.Pin = GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3;  /* 赋值或更新变量 */
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;  /* 赋值或更新变量 */
  GPIO_InitStruct.Pull = GPIO_NOPULL;  /* 赋值或更新变量 */
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;  /* 赋值或更新变量 */
  GPIO_InitStruct.Alternate = GPIO_AF1_USART2;  /* 赋值或更新变量 */
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);  /* 调用函数执行对应操作 */

  /* SPI1 MOSI：PA7 连接到 WS2812 数据输入。 */
  GPIO_InitStruct.Pin = LED_DATA_Pin;  /* 赋值或更新变量 */
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;  /* 赋值或更新变量 */
  GPIO_InitStruct.Pull = GPIO_NOPULL;  /* 赋值或更新变量 */
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;  /* 赋值或更新变量 */
  GPIO_InitStruct.Alternate = GPIO_AF0_SPI1;  /* 赋值或更新变量 */
  HAL_GPIO_Init(LED_DATA_GPIO_Port, &GPIO_InitStruct);  /* 调用函数执行对应操作 */
}  /* 代码块结束 */

void Error_Handler(void)  /* 定义或声明函数 */
{  /* 代码块开始 */
  __disable_irq();  /* 调用函数或完成表达式 */
  while (1)  /* 执行循环 */
  {  /* 代码块开始 */
    status_led_set(true);  /* 调用函数执行对应操作 */
    HAL_Delay(80u);  /* 调用函数执行对应操作 */
    status_led_set(false);  /* 调用函数执行对应操作 */
    HAL_Delay(80u);  /* 调用函数执行对应操作 */
  }  /* 代码块结束 */
}  /* 代码块结束 */

#ifdef USE_FULL_ASSERT  /* 代码行说明 */
void assert_failed(uint8_t *file, uint32_t line)  /* 定义或声明函数 */
{  /* 代码块开始 */
  (void)file;  /* 执行一条语句 */
  (void)line;  /* 执行一条语句 */
}  /* 代码块结束 */
#endif /* USE_FULL_ASSERT */  /* 中文注释：代码行说明 */
