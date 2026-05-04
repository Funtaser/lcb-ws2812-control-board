**灯带控制板电控开发手册***  
 适用对象：电控主控开发人员、联调人员。*  
 *  
 控制板 MCU：STM32G030F6P6。*  
 *  
 灯带类型：WS2812。*  
![](data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAnEAAAACCAYAAAA3pIp+AAAABmJLR0QA/wD/AP+gvaeTAAAACXBIWXMAAA7EAAAOxAGVKw4bAAAANklEQVR4nO3OMQ2AABAAsSNBCkJfFEIwwIgHRiywEZJWQZeZ2ao9AAD+4lyruzq+ngAA8Nr1AOHsBegrsOrIAAAAAElFTkSuQmCC)  
**1. 固件功能概述**  
当前固件用于控制 WS2812 灯带，并通过 USART2 接收电控主控命令。电控侧主要通过串口发送文本命令完成灯带初始化、颜色控制、动画控制、状态查询和开发者模式测试。  
当前代码支持：  
| | | |  
|-|-|-|  
| **功能** | **当前状态** | **说明** |   
| WS2812 灯带控制 | 支持 | SPI1 MOSI / PA7 输出 |   
| 最大灯珠数量 | 300 颗 | WS2812_LED_MAX = 300 |   
| 默认灯珠数量 | 300 颗 | WS2812_LED_DEFAULT = 300 |   
| 串口控制 | 支持 | USART2，115200 8N1 |   
| 上电握手 | 支持 | 主控发送 OK 后进入正常控制状态 |   
| 静态颜色 | 支持 | COLOR R G B |   
| 内置动画 | 支持 | ANIM 0..4 |   
| 自定义动画入口 | 支持 | ANIM 4 |   
| 电压检测 | 支持 | PA0，120k / 10k 分压 |   
| 电流检测 | 支持 | PA6，20mΩ 采样电阻，20 倍放大 |   
| Type-C 调试供电识别 | 支持 | 电压 < 6V 不报低压 |   
| 低压 / 过压 / 过流保护提示 | 支持 | 滤波 + 滞回 + 连续确认 |   
| PB3 状态灯 | 支持 | 低电平亮，支持呼吸/常亮/故障闪烁 |   
| DEV 开发者模式 | 支持 | 可模拟故障和运行状态 |   
   
当前代码不支持：  
| | | |  
|-|-|-|  
| **功能** | **当前状态** | **说明** |   
| WS2812 DMA 异步刷灯 | 不支持 | 当前使用 HAL_SPI_Transmit() 阻塞发送 |   
| UART DMA 环形接收 | 不支持 | 当前使用 1 字节 UART 中断接收 |   
| VREFINT 反推 VDDA | 不支持 | 当前 ADC 换算使用固定 3300mV 参考 |   
| Flash 保存配置 | 不支持 | 上电恢复默认值 |   
| 主控心跳掉线检测 | 不支持 | 收到 OK 后不会自动回到等待状态 |   
   
![](data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAnEAAAACCAYAAAA3pIp+AAAABmJLR0QA/wD/AP+gvaeTAAAACXBIWXMAAA7EAAAOxAGVKw4bAAAANUlEQVR4nO3OMQ2AABAAsSPBCj7fFwtCmJHAjAU2QtIq6DIzW7UHAMBfnGt1V8fHEQAA3rsexOkF3va0dq8AAAAASUVORK5CYII=)  
**2. 硬件接口说明**  
**2.1 串口接口**  
| | | |  
|-|-|-|  
| **灯带控制板** | **功能** | **电控主控连接** |   
| PA2 | USART2_TX | 接主控 RX |   
| PA3 | USART2_RX | 接主控 TX |   
| GND | 地 | 与主控 GND 共地 |   
   
串口电平：  
3.3V TTL  
   
不要接 RS232 电平。主控和灯带控制板必须共地。  
**2.2 WS2812 灯带接口**  
| | | |  
|-|-|-|  
| **功能** | **引脚 / 外设** | **说明** |   
| WS2812 DATA | PA7 / SPI1 MOSI | SPI 模拟 WS2812 时序 |   
| 灯带电源 | 5V | 外部 5V 供电 |   
| 灯带地 | GND | 必须与 MCU 共地 |   
   
当前代码使用 SPI1 驱动 WS2812：  
SPI1 MOSI: PA7  
 SPI 频率: 4MHz  
 颜色顺序: GRB  
 编码方式: 0 -> 1000, 1 -> 1110  
   
**2.3 采样接口**  
| | | |  
|-|-|-|  
| **功能** | **引脚** | **参数** |   
| 母线电压采样 | PA0 / ADC_CHANNEL_0 | 120k / 10k 分压 |   
| 灯带电流采样 | PA6 / ADC_CHANNEL_6 | 20mΩ 采样电阻，20 倍放大 |   
   
**2.4 板载状态灯**  
| | | |  
|-|-|-|  
| **功能** | **引脚** | **有效电平** |   
| 状态灯 | PB3 | 低电平亮 |   
   
逻辑：  
PB3 = 0：亮  
 PB3 = 1：灭  
   
![](data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAnEAAAACCAYAAAA3pIp+AAAABmJLR0QA/wD/AP+gvaeTAAAACXBIWXMAAA7EAAAOxAGVKw4bAAAANElEQVR4nO3OQQmAABRAsSdYxKa/jL0MIR7FCt5E2BJsmZmt2gMA4C+Otbqr8+sJAACvXQ85SAYUQNBTfQAAAABJRU5ErkJggg==)  
**3. 串口通信协议**  
**3.1 串口参数**  
波特率：115200  
 数据位：8  
 停止位：1  
 校验位：无  
 流控：无  
 格式：115200 8N1  
   
**3.2 命令格式**  
本控制板使用 ASCII 文本命令。  
每条命令必须以换行结束，以下格式均支持：  
\n  
 \r  
 \r\n  
   
推荐电控侧统一发送：  
\r\n  
   
命令不区分大小写。固件会自动将小写字母转换为大写。  
例如以下命令等价：  
ok  
 OK  
 Ok  
   
**3.3 单行长度限制**  
当前串口命令行最大长度：  
63 字符 + 结尾 \0  
   
如果一行超过限制，固件会丢弃该行直到遇到换行符，避免超长命令导致缓冲区污染。  
![](data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAnEAAAACCAYAAAA3pIp+AAAABmJLR0QA/wD/AP+gvaeTAAAACXBIWXMAAA7EAAAOxAGVKw4bAAAANElEQVR4nO3OQQmAUBBAwSf8GGLWDWFDY3ixgjcRZhLMNjNHdQYAwF9cq1rV/vUEAIDX7gcRXAQ2s/16gwAAAABJRU5ErkJggg==)  
**4. 上电流程与主控初始化**  
**4.1 控制板上电默认状态**  
控制板上电后默认进入：  
APP_WAIT_FOR_HOST  
   
表现：  
| | |  
|-|-|  
| **部位** | **表现** |   
| WS2812 灯带 | 红色扫描点 + 短拖尾 |   
| PB3 状态灯 | 慢呼吸 |   
   
含义：  
控制板已运行，正在等待电控主控发送 OK 握手。  
   
**4.2 电控推荐初始化流程**  
电控主控建议按下面顺序初始化：  
1. 初始化 UART  
 2. 等待灯带控制板上电稳定  
 3. 发送 PING 测试通信  
 4. 收到 PONG 后发送 OK 完成握手  
 5. 发送 COUNT 设置实际灯珠数量  
 6. 发送 BRI 设置亮度  
 7. 发送 COLOR 或 ANIM 进入目标灯效  
 8. 周期性发送 STAT 查询状态  
   
推荐启动命令示例：  
PING  
 OK  
 COUNT 72  
 BRI 80  
 ANIM 2  
 STAT  
   
可能返回：  
PONG  
 OK  
 OK COUNT  
 OK BRI  
 OK ANIM RAINBOW  
 24000 800 0 0  
   
![](data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAnEAAAACCAYAAAA3pIp+AAAABmJLR0QA/wD/AP+gvaeTAAAACXBIWXMAAA7EAAAOxAGVKw4bAAAANklEQVR4nO3OMQ2AABAAsSNBACP6MMH6NpGACyywEZJWQZeZ2aszAAD+4l6rrTq+ngAA8Nr1AL+6BElk4wV6AAAAAElFTkSuQmCC)  
**5. 普通命令总表**  
| | | | |  
|-|-|-|-|  
| **命令** | **作用** | **是否需要先 ** **OK** | **成功返回** |   
| PING | 测试通信 | 否 | PONG |   
| OK | 主控握手 | 否 | OK |   
| STAT | 查询电压、电流、故障 | 否 | 纯数据 4 字段 |   
| OFF | 关闭灯带 | 是 | OK OFF |   
| COUNT <1..300> | 设置灯珠数量 | 是 | OK COUNT |   
| BRI <0..255> | 设置全局亮度 | 是 | OK BRI |   
| COLOR <R> <G> <B> | 设置静态颜色 | 是 | OK COLOR |   
| ANIM <0..4> | 设置动画 | 是 | 见动画表 |   
   
未发送 OK 前，如果发送普通控制命令，例如：  
COLOR 255 0 0  
   
会返回：  
ERR NEED_OK  
   
![](data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAnEAAAACCAYAAAA3pIp+AAAABmJLR0QA/wD/AP+gvaeTAAAACXBIWXMAAA7EAAAOxAGVKw4bAAAANElEQVR4nO3OQQmAABRAsad4EjtY9fewnUms4E2ELcGWmTmrKwAA/uLeqrU6vp4AAPDa/gDzWAM6QQXRdAAAAABJRU5ErkJggg==)  
**6. 普通命令详细说明**  
**6.1 **PING  
用途：测试串口通信是否正常。  
发送：  
PING  
   
返回：  
PONG  
   
该命令不需要先发送 OK。  
**6.2 **OK  
用途：主控握手，通知灯带控制板主控已连接。  
发送：  
OK  
   
返回：  
OK  
   
执行效果：  
1. 模式切换到 APP_IDLE  
 2. 清空 WS2812 灯带缓存  
 3. 发送一帧全黑数据  
 4. PB3 状态灯变为低亮常亮  
   
**6.3 **STAT  
用途：查询当前电压、电流和故障状态。  
发送：  
STAT  
   
返回格式：  
<电压mV> <电流mA> <真实故障> <有效故障>  
   
示例：  
24000 800 0 0  
   
字段说明：  
| | | |  
|-|-|-|  
| **字段序号** | **示例** | **含义** |   
| 1 | 24000 | 当前母线电压，单位 mV |   
| 2 | 800 | 当前灯带电流，单位 mA |   
| 3 | 0 | 真实 ADC 判断出的故障状态 |   
| 4 | 0 | 当前最终生效的故障状态，可能被 DEV 模式覆盖 |   
   
故障编号：  
| | |  
|-|-|  
| **编号** | **含义** |   
| 0 | 正常 |   
| 1 | 低压 |   
| 2 | 过压 |   
| 3 | 过流 |   
   
Type-C / USB 调试供电识别：  
如果电压 < 6000mV 且故障为 0，表示当前被识别为 Type-C/USB 调试供电，不报低压。  
   
**6.4 **COUNT  
用途：设置实际使用的灯珠数量。  
格式：  
COUNT <数量>  
   
范围：  
1 ~ 300  
   
示例：  
COUNT 72  
   
成功返回：  
OK COUNT  
   
错误返回：  
ERR COUNT 1..300  
   
执行效果：  
1. 修改当前有效灯珠数量  
 2. 如果动画计数超过新灯珠数量，则清零动画计数  
 3. 清空灯带并刷新一帧全黑  
   
注意：默认灯珠数量为 300。电控侧建议在初始化时根据实际灯带长度主动发送 COUNT。  
**6.5 **BRI  
用途：设置全局亮度。  
格式：  
BRI <亮度>  
   
范围：  
0 ~ 255  
   
示例：  
BRI 80  
   
成功返回：  
OK BRI  
   
错误返回：  
ERR BRI 0..255  
   
亮度说明：  
| | |  
|-|-|  
| **值** | **含义** |   
| 0 | 全灭 |   
| 80 | 当前默认亮度 |   
| 255 | 最大亮度 |   
   
建议联调初期使用：  
BRI 30 ~ 100  
   
不要一开始拉满亮度，避免灯带电流过大导致压降或误触发过流。  
**6.6 **COLOR  
用途：设置静态 RGB 颜色。  
格式：  
COLOR <R> <G> <B>  
   
参数范围：  
R/G/B 均为 0 ~ 255  
   
示例：  
COLOR 255 0 0  
   
表示红色。  
常用示例：  
COLOR 255 0 0      红色  
 COLOR 0 255 0      绿色  
 COLOR 0 0 255      蓝色  
 COLOR 255 255 255  白色  
 COLOR 0 0 0        全灭  
   
成功返回：  
OK COLOR  
   
错误返回：  
ERR COLOR R G B  
   
执行效果：  
1. 模式切换为 APP_STATIC_COLOR  
 2. 清除动画相位  
 3. 将所有有效灯珠设置为指定颜色  
 4. 发送一帧灯带数据  
 5. PB3 状态灯变为中亮常亮  
   
**6.7 **ANIM  
用途：启动内置动画或自定义动画。  
格式：  
ANIM <编号>  
   
支持动画：  
| | | |  
|-|-|-|  
| **命令** | **效果** | **成功返回** |   
| ANIM 0 | 关闭动画，全灭 | OK ANIM OFF |   
| ANIM 1 | 红色追逐动画 | OK ANIM RED_CHASE |   
| ANIM 2 | 彩虹动画 | OK ANIM RAINBOW |   
| ANIM 3 | 红色呼吸动画 | OK ANIM RED_BREATH |   
| ANIM 4 | 自定义动画 1 | OK ANIM CUSTOM_1 |   
   
错误返回：  
ERR ANIM 0..4  
   
**6.8 **OFF  
用途：关闭灯带。  
发送：  
OFF  
   
返回：  
OK OFF  
   
执行效果：  
1. 模式切换为 APP_IDLE  
 2. 清空灯带缓存  
 3. 发送一帧全黑  
 4. PB3 状态灯变为低亮常亮  
   
![](data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAnEAAAACCAYAAAA3pIp+AAAABmJLR0QA/wD/AP+gvaeTAAAACXBIWXMAAA7EAAAOxAGVKw4bAAAANUlEQVR4nO3OYQ1AABSAwY8JoIGqr4Z6Eoiggn9mu0twy8wc1RkAAH9xbdVa7V9PAAB47X4A9C4EIsmYmgsAAAAASUVORK5CYII=)  
**7. 动画说明**  
**7.1 等待主控动画**  
触发条件：上电后未收到 OK。  
效果：  
红色扫描点 + 两级短拖尾  
   
帧间隔：  
60ms  
   
**7.2 **ANIM 1 ** 红色追逐**  
效果：  
红色主点 + 两级红色拖尾循环运动  
   
帧间隔：  
40ms  
   
**7.3 **ANIM 2 ** 彩虹动画**  
效果：  
整条灯带显示彩虹渐变，并随时间滚动  
   
帧间隔：  
30ms  
   
**7.4 **ANIM 3 ** 红色呼吸**  
效果：  
整条灯带红色亮度周期性上升/下降  
   
帧间隔：  
20ms  
   
**7.5 **ANIM 4 ** 自定义动画**  
当前默认效果：  
蓝色扫描点 + 两级蓝色拖尾  
   
帧间隔宏：  
#define CUSTOM_ANIM_1_PERIOD_MS 50u  
   
电控队员需要修改自定义动画时，主要改这个函数：  
static void custom_animation_1_task(uint32_t now, bool *should_update)  
   
编写规则：  
1. 不要在动画函数中使用 HAL_Delay()  
 2. 不要在动画函数中直接调用 ws2812_show()  
 3. 使用 now - g_last_anim_ms 控制帧间隔  
 4. 使用 ws2812_clear() / ws2812_set_pixel() / ws2812_set_all() 修改灯带缓存  
 5. 一帧准备完成后将 *should_update = true  
 6. 只写 0 ~ g_led_count-1 范围内的灯珠  
   
新增更多动画时建议流程：  
1. 在 AppMode_t 中增加新的 APP_ANIM_CUSTOM_X  
 2. 在 mode_to_str() 中增加模式字符串  
 3. 在 ANIM 命令解析中映射新的编号  
 4. 在 animation_task() 中增加分支  
 5. 新建或复用自定义动画 task 函数  
   
![](data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAnEAAAACCAYAAAA3pIp+AAAABmJLR0QA/wD/AP+gvaeTAAAACXBIWXMAAA7EAAAOxAGVKw4bAAAANUlEQVR4nO3OMQ2AABAAsSNhwgJOUPcjIpnRgQU2QtIq6DIze3UGAMBf3Gu1VcfXEwAAXrseaJEEL8XMiYMAAAAASUVORK5CYII=)  
**8. 电压、电流与保护逻辑**  
**8.1 电压采样**  
硬件参数：  
PA0 ADC_CHANNEL_0  
 上分压电阻：120k  
 下分压电阻：10k  
 分压倍率：13  
   
代码中还有软件校准：  
#define VBUS_CAL_REAL_MV 25190u  
 #define VBUS_CAL_ADC_MV  25220u  
   
含义：  
电表实测 25.190V，ADC 原始换算约 25.220V，代码按 25190/25220 做比例校准。  
   
ADC 多次采样：  
每次通道切换后丢弃第 1 次样本，之后取 16 次平均。  
   
电压滤波：  
IIR 整数滤波，系数约 1/4。  
   
**8.2 Type-C / USB 调试供电识别**  
当前规则：  
| | |  
|-|-|  
| **电压范围** | **判定** |   
| U < 6000mV | Type-C / USB 调试供电，不报低压 |   
| 6000mV <= U < 19800mV | 低压 |   
| U > 25600mV | 过压 |   
   
低压退出条件：  
U > 20300mV 或 U < 6000mV  
   
也就是说，如果从电池低压切换到 USB 调试供电，固件会在连续确认后退出低压故障。  
**8.3 电流采样**  
硬件参数：  
PA6 ADC_CHANNEL_6  
 采样电阻：20mΩ  
 放大倍数：20  
   
换算关系：  
I(mA) = Vadc(mV) * 1000 / (20 * 20)  
   
即：  
I(mA) = Vadc(mV) * 2.5  
   
**8.4 故障编号**  
| | |  
|-|-|  
| **编号** | **故障** |   
| 0 | 无故障 |   
| 1 | 低压 |   
| 2 | 过压 |   
| 3 | 过流 |   
   
**8.5 故障优先级**  
进入故障的优先级：  
过流 > 过压 > 低压  
   
如果已经在低压状态，又检测到过压或过流，高优先级故障可以覆盖低优先级故障。  
**8.6 连续确认与滞回**  
当前代码不是单次采样立即触发，而是：  
| | | | | |  
|-|-|-|-|-|  
| **故障** | **进入阈值** | **退出阈值** | **进入确认次数** | **退出确认次数** |   
| 低压 | 6000mV <= U < 19800mV | U > 20300mV 或 U < 6000mV | 3 次 | 5 次 |   
| 过压 | U > 25600mV | U < 25300mV | 2 次 | 5 次 |   
| 过流 | I > 4000mA | I < 3600mA | 2 次 | 5 次 |   
   
ADC 任务周期：  
20ms  
   
所以大致响应时间：  
| | |  
|-|-|  
| **动作** | **时间** |   
| 严重故障进入 | 约 40ms |   
| 低压进入 | 约 60ms |   
| 故障退出 | 约 100ms |   
   
![](data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAnEAAAACCAYAAAA3pIp+AAAABmJLR0QA/wD/AP+gvaeTAAAACXBIWXMAAA7EAAAOxAGVKw4bAAAANElEQVR4nO3OQQmAABRAsSdYxKY/jMFMIZ7ECt5E2BJsmZmt2gMA4C+Otbqr8+sJAACvXQ85QgYXd/O+eQAAAABJRU5ErkJggg==)  
**9. 故障时灯带表现**  
| | |  
|-|-|  
| **故障** | **WS2812 表现** |   
| 低压 | 低亮度红色慢闪，2s 周期，亮 500ms |   
| 过压 | 低亮度红色快闪，100ms 翻转 |   
| 过流 | 低亮度红色快闪，100ms 翻转 |   
   
故障红色亮度：  
#define FAULT_RED_BRIGHTNESS 18u  
   
注意：当前固件只做灯效和状态提示，没有控制 MOS 管、继电器或电源 EN 脚来真正切断灯带电源。  
![](data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAnEAAAACCAYAAAA3pIp+AAAABmJLR0QA/wD/AP+gvaeTAAAACXBIWXMAAA7EAAAOxAGVKw4bAAAANklEQVR4nO3OMQ2AABAAsSNhRAF6EPYDLhGADSywEZJWQZeZ2aszAAD+4l6rrTq+ngAA8Nr1AIWsBDYDm5cLAAAAAElFTkSuQmCC)  
**10. PB3 状态灯说明**  
PB3 为低电平亮。  
状态优先级：  
故障 > 串口命令确认 > 普通运行状态  
   
状态表：  
| | | |  
|-|-|-|  
| **状态** | **PB3 显示方式** | **含义** |   
| 过流 | 高速硬闪，60ms 亮 / 60ms 灭 | 严重故障 |   
| 过压 | 三连短闪 + 长停顿 | 高压故障 |   
| 低压 | 超慢呼吸，周期约 3.2s | 供电不足 |   
| 收到完整串口命令 | 满亮 120ms | 命令接收确认 |   
| 等待主控 OK | 慢呼吸，周期约 1.8s | 控制板运行，等待握手 |   
| 动画运行 | 快呼吸，周期约 0.8s | 正在执行动画 |   
| 静态颜色 | 中亮常亮，约 45% | 正在显示 COLOR |   
| 已连接空闲 | 低亮常亮，约 15% | 已握手，待命 |   
   
![](data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAnEAAAACCAYAAAA3pIp+AAAABmJLR0QA/wD/AP+gvaeTAAAACXBIWXMAAA7EAAAOxAGVKw4bAAAANElEQVR4nO3OQQmAABRAsSdYxKa/jL0MIR7FCt5E2BJsmZmt2gMA4C+Otbqr8+sJAACvXQ85SAYUQNBTfQAAAABJRU5ErkJggg==)  
**11. DEV 开发者模式**  
**11.1 用途**  
DEV 开发者模式用于联调时模拟故障和运行状态，不需要真实制造低压、过压、过流条件。  
正式电控程序中不要自动发送 DEV 命令。  
**11.2 自动超时**  
开发者模式开启后，5 分钟自动关闭。  
超时后控制板发送：  
DEV AUTO_OFF  
   
**11.3 DEV 命令总表**  
| | | |  
|-|-|-|  
| **命令** | **作用** | **是否需要 ** **DEV ON** |   
| DEV HELP | 查看帮助 | 否 |   
| DEV ON | 开启开发者模式 | 否 |   
| DEV OFF | 关闭开发者模式 | 否 |   
| DEV STATUS | 查询开发者状态 | 否 |   
| DEV ADC | 查询 ADC 状态，等同 STAT | 否 |   
| DEV FAULT NONE | 清除模拟故障 | 是 |   
| DEV FAULT LOW | 模拟低压 | 是 |   
| DEV FAULT OVERV | 模拟过压 | 是 |   
| DEV FAULT OVERI | 模拟过流 | 是 |   
| DEV MODE NONE | 恢复真实运行模式 | 是 |   
| DEV MODE WAIT | 模拟等待主控 | 是 |   
| DEV MODE IDLE | 模拟空闲 | 是 |   
| DEV MODE COLOR | 模拟静态颜色 | 是 |   
| DEV MODE ANIM1 | 模拟动画 1 | 是 |   
| DEV MODE ANIM2 | 模拟动画 2 | 是 |   
| DEV MODE ANIM3 | 模拟动画 3 | 是 |   
| DEV MODE ANIM4 | 模拟自定义动画 | 是 |   
   
如果没有先开启开发者模式，发送需要 DEV ON 的命令会返回：  
ERR DEV OFF  
   
**11.4 开启 / 关闭开发者模式**  
开启：  
DEV ON  
   
返回：  
OK DEV ON  
   
关闭：  
DEV OFF  
   
返回：  
OK DEV OFF  
   
关闭后会清除模拟故障和强制运行状态。  
**11.5 模拟故障**  
模拟低压：  
DEV ON  
 DEV FAULT LOW  
   
返回：  
OK DEV ON  
 OK DEV FAULT UNDERVOLTAGE  
   
模拟过压：  
DEV FAULT OVERV  
   
返回：  
OK DEV FAULT OVERVOLTAGE  
   
模拟过流：  
DEV FAULT OVERI  
   
返回：  
OK DEV FAULT OVERCURRENT  
   
清除模拟故障：  
DEV FAULT NONE  
   
返回：  
OK DEV FAULT NONE  
   
**11.6 模拟运行状态**  
DEV ON  
 DEV MODE WAIT  
 DEV MODE IDLE  
 DEV MODE COLOR  
 DEV MODE ANIM1  
 DEV MODE ANIM2  
 DEV MODE ANIM3  
 DEV MODE ANIM4  
   
常用返回：  
OK DEV MODE WAIT_HOST  
 OK DEV MODE IDLE  
 OK DEV MODE COLOR  
 OK DEV MODE ANIM1  
 OK DEV MODE ANIM2  
 OK DEV MODE ANIM3  
 OK DEV MODE ANIM4 CUSTOM  
   
恢复真实运行模式：  
DEV MODE NONE  
   
返回：  
OK DEV MODE REAL  
   
**11.7 **DEV STATUS  
发送：  
DEV STATUS  
   
返回示例：  
DEV=ON REAL_FAULT=NONE EFFECTIVE_FAULT=OVERVOLTAGE REAL_MODE=WAIT_HOST EFFECTIVE_MODE=WAIT_HOST FORCE_MODE=NO  
   
字段说明：  
| | |  
|-|-|  
| **字段** | **含义** |   
| DEV | 开发者模式是否开启 |   
| REAL_FAULT | 真实 ADC 判断出的故障 |   
| EFFECTIVE_FAULT | 当前最终生效的故障，可能被 DEV 覆盖 |   
| REAL_MODE | 真实运行模式 |   
| EFFECTIVE_MODE | 当前最终生效模式，可能被 DEV 覆盖 |   
| FORCE_MODE | 是否强制覆盖运行模式 |   
   
![](data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAnEAAAACCAYAAAA3pIp+AAAABmJLR0QA/wD/AP+gvaeTAAAACXBIWXMAAA7EAAAOxAGVKw4bAAAANUlEQVR4nO3OMQ2AABAAsSNBCUrfDqrYGVDAgAU2QtIq6DIzW7UHAMBfHGt1V+fXEwAAXrseHCQGBEuErVgAAAAASUVORK5CYII=)  
**12. 电控侧推荐状态机**  
系统上电  
   ↓  
 初始化 UART  
   ↓  
 发送 PING  
   ↓  
 收到 PONG？  
   ├─ 否：重试 / 报通信异常  
   └─ 是：  
        ↓  
      发送 OK  
        ↓  
      收到 OK？  
        ├─ 否：重试 / 报握手失败  
        └─ 是：  
             ↓  
           发送 COUNT  
             ↓  
           发送 BRI  
             ↓  
           发送 COLOR 或 ANIM  
             ↓  
           周期性发送 STAT  
   
推荐周期：  
| | |  
|-|-|  
| **操作** | **建议周期** |   
| PING 重试 | 200ms ~ 500ms |   
| OK 重试 | 200ms ~ 500ms |   
| STAT 查询 | 500ms ~ 1000ms |   
   
![](data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAnEAAAACCAYAAAA3pIp+AAAABmJLR0QA/wD/AP+gvaeTAAAACXBIWXMAAA7EAAAOxAGVKw4bAAAANUlEQVR4nO3OMQ2AABAAsSNBCkJfE1pYGfHAiAU2QtIq6DIzW7UHAMBfnGt1V8fXEwAAXrse4dwF6o2O55YAAAAASUVORK5CYII=)  
**13. 电控侧返回值解析建议**  
**13.1 正常返回**  
| | |  
|-|-|  
| **返回** | **含义** |   
| PONG | PING 通信正常 |   
| OK | 握手成功 |   
| OK COUNT | 灯珠数量设置成功 |   
| OK BRI | 亮度设置成功 |   
| OK COLOR | 静态颜色设置成功 |   
| OK ANIM OFF | 动画关闭成功 |   
| OK ANIM RED_CHASE | 红色追逐启动成功 |   
| OK ANIM RAINBOW | 彩虹动画启动成功 |   
| OK ANIM RED_BREATH | 红色呼吸启动成功 |   
| OK ANIM CUSTOM_1 | 自定义动画启动成功 |   
| OK OFF | 灯带关闭成功 |   
   
**13.2 错误返回**  
| | | |  
|-|-|-|  
| **返回** | **原因** | **处理建议** |   
| ERR NEED_OK | 未握手就发送控制命令 | 先发送 OK |   
| ERR COUNT 1..300 | COUNT 参数错误 | 检查灯珠数量范围 |   
| ERR BRI 0..255 | BRI 参数错误 | 检查亮度范围 |   
| ERR COLOR R G B | COLOR 参数错误 | 检查 RGB 参数数量和范围 |   
| ERR ANIM 0..4 | ANIM 编号错误 | 使用 0~4 |   
| ERR UNKNOWN | 未知命令 | 检查命令名和换行符 |   
| ERR DEV OFF | DEV 模式未开启 | 先发送 DEV ON |   
| ERR DEV CMD | DEV 子命令错误 | 发送 DEV HELP 查看帮助 |   
   
![](data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAnEAAAACCAYAAAA3pIp+AAAABmJLR0QA/wD/AP+gvaeTAAAACXBIWXMAAA7EAAAOxAGVKw4bAAAANUlEQVR4nO3OMQ2AABAAsSPBCj7fFwtCmJHAjAU2QtIq6DIzW7UHAMBfnGt1V8fHEQAA3rsexOkF3va0dq8AAAAASUVORK5CYII=)  
**14. 常见问题**  
**14.1 发送 **COLOR ** 或 **ANIM ** 返回 **ERR NEED_OK  
原因：  
控制板还处于等待主控状态，没有收到 OK。  
   
处理：  
先发送 OK，收到 OK 后再发送普通控制命令。  
   
**14.2 **STAT ** 返回电压小于 6000，但故障为 0**  
这是正常现象，表示当前被识别为 Type-C / USB 调试供电。  
例如：  
5000 120 0 0  
   
含义：  
当前电压约 5V，未报低压。  
   
**14.3 **STAT ** 中第三字段和第四字段不同**  
示例：  
24000 800 0 3  
   
含义：  
真实硬件没有故障，但 DEV 模式强制了过流故障。  
   
处理：  
DEV OFF  
   
**14.4 发送 **DEV FAULT LOW ** 返回 **ERR DEV OFF  
原因：  
开发者模式未开启。  
   
处理：  
DEV ON  
 DEV FAULT LOW  
   
**14.5 灯珠数量较多时灯效异常**  
可能原因：  
1. 5V 供电电流不足  
 2. 灯带压降过大  
 3. DATA 线过长或没有串联电阻  
 4. 当前刷灯使用阻塞 SPI，长灯带时主循环会变慢  
   
硬件建议：  
1. 主控、灯带、灯带控制板必须可靠共地  
 2. 联调初期先使用 BRI 30~100，不要满亮度  
   
**14.6 OK 后灯带没有完全熄灭**  
当前 OK 会调用 app_set_mode(APP_IDLE, true)，并发送全黑帧。如果仍然有残留，优先检查：  
1. 当前 COUNT 是否小于实际灯带数量，COUNT 之外的灯珠不会被刷新到  
 2. WS2812 数据线是否受干扰  
 3. 5V 供电是否稳定  
 4. 灯带第一颗芯片是否损坏或 DIN 信号质量差  
   
![](data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAnEAAAACCAYAAAA3pIp+AAAABmJLR0QA/wD/AP+gvaeTAAAACXBIWXMAAA7EAAAOxAGVKw4bAAAANUlEQVR4nO3OMQ2AABAAsSNBCUrfD6LYGNDAgAU2QtIq6DIzW7UHAMBfHGt1V+fXEwAAXrseHDAF/orRG+cAAAAASUVORK5CYII=)  
**15. 联调测试流程**  
**15.1 最小通信测试**  
PING  
   
期望：  
PONG  
   
**15.2 握手测试**  
OK  
   
期望：  
OK  
   
现象：  
灯带全灭，PB3 低亮常亮。  
   
**15.3 基本灯效测试**  
COUNT 72  
 BRI 80  
 COLOR 255 0 0  
   
期望：  
OK COUNT  
 OK BRI  
 OK COLOR  
   
现象：  
前 72 颗显示红色。  
   
**15.4 动画测试**  
ANIM 1  
 ANIM 2  
 ANIM 3  
 ANIM 4  
   
期望返回：  
OK ANIM RED_CHASE  
 OK ANIM RAINBOW  
 OK ANIM RED_BREATH  
 OK ANIM CUSTOM_1  
   
**15.5 状态查询测试**  
STAT  
   
期望类似：  
24000 800 0 0  
   
**15.6 Type-C 调试供电测试**  
只接 USB / Type-C 调试供电时发送：  
STAT  
   
期望类似：  
5000 100 0 0  
   
重点：第三字段和第四字段应为 0，不应报低压。  
**15.7 DEV 故障测试**  
DEV ON  
 DEV FAULT LOW  
 STAT  
 DEV FAULT OVERV  
 STAT  
 DEV FAULT OVERI  
 STAT  
 DEV OFF  
   
期望：  
第四字段会随 DEV FAULT 改变，DEV OFF 后恢复真实状态。  
   
![](data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAnEAAAACCAYAAAA3pIp+AAAABmJLR0QA/wD/AP+gvaeTAAAACXBIWXMAAA7EAAAOxAGVKw4bAAAANUlEQVR4nO3OMQ2AABAAsSNhQAQ60PcrIhnxgQU2QtIq6DIze3UGAMBf3Gu1VcfXEwAAXrseS14EKxPCORkAAAAASUVORK5CYII=)  
**16. 当前版本边界与注意事项**  
1. 当前 WS2812 刷灯是阻塞 SPI 发送。  
300 颗灯时每帧数据较长，刷灯期间主循环会短暂阻塞。  
   
1. 当前 UART 是 1 字节中断接收，不是 DMA 环形缓冲。  
正常命令频率下可用，但不建议电控高频连续刷大量命令。  
   
1. 当前 ADC 使用固定 3300mV 作为参考电压。  
如果 MCU VDDA 波动，电压换算会跟着产生误差。  
   
1. 当前故障只做显示和状态反馈。  
不会真正切断灯带 5V 电源。  
   
1. 开发者模式不应进入正式电控流程。  
DEV 命令只用于联调和测试。  
   
![](data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAnEAAAACCAYAAAA3pIp+AAAABmJLR0QA/wD/AP+gvaeTAAAACXBIWXMAAA7EAAAOxAGVKw4bAAAAM0lEQVR4nO3KsQ0AIRAEsUW6Qij1KvnevhMSYmKQ7GiCGd09k3wBAOAVf+2o4wYAwE1qAdYuAy151mgcAAAAAElFTkSuQmCC)  
**17. 附录：完整命令表**  
PING  
 OK  
 STAT  
 OFF  
 COUNT <1..300>  
 BRI <0..255>  
 COLOR <R> <G> <B>  
 ANIM <0..4>  
   
 DEV HELP  
 DEV ON  
 DEV OFF  
 DEV STATUS  
 DEV ADC  
 DEV FAULT NONE  
 DEV FAULT LOW  
 DEV FAULT OVERV  
 DEV FAULT OVERI  
 DEV MODE NONE  
 DEV MODE WAIT  
 DEV MODE IDLE  
 DEV MODE COLOR  
 DEV MODE ANIM1  
 DEV MODE ANIM2  
 DEV MODE ANIM3  
 DEV MODE ANIM4  
   
![](data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAnEAAAACCAYAAAA3pIp+AAAABmJLR0QA/wD/AP+gvaeTAAAACXBIWXMAAA7EAAAOxAGVKw4bAAAANUlEQVR4nO3OQQmAABRAsSdYxZ4/mJjEsxE8W8GbCFuCLTOzVXsAAPzFuVZ3dXw9AQDgtesBxPEF3bv7x0IAAAAASUVORK5CYII=)  
**18. 电控最简接入示例**  
初始化：  
PING  
 OK  
 COUNT 72  
 BRI 80  
   
显示红色：  
COLOR 255 0 0  
   
启动彩虹：  
ANIM 2  
   
关闭灯带：  
OFF  
   
查询状态：  
STAT  
   
联调模拟过流：  
DEV ON  
 DEV FAULT OVERI  
 STAT  
 DEV OFF  
   
