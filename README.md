# LCB 灯带控制板固件工程

本工程是基于 **STM32G030F6P6** 的 WS2812 灯带控制板固件。控制板通过 USART2 接收电控主控命令，驱动 WS2812 灯带执行静态颜色、内置动画、自定义动画，并通过 ADC 检测母线电压和灯带电流。

> 电控对接、串口协议、状态灯含义、DEV 开发者模式、ANIM 4 自定义动画流程等详细内容，请阅读：[`doc/lcb_ecu_development_manual.md`](doc/lcb_ecu_development_manual.md)。  
> README 只作为工程入口和构建维护说明，避免与开发手册重复维护。

---

## 1. 工程定位

| 项目 | 说明 |
|---|---|
| MCU | STM32G030F6P6 |
| 灯带 | WS2812，5V，三线制 |
| 灯带数据脚 | PA7 / SPI1_MOSI |
| 电控通信 | USART2，115200 8N1 |
| 状态指示灯 | PB3，低电平亮 |
| 电压采样 | PA0，120k / 10k 分压 |
| 电流采样 | PA6，20mΩ 采样电阻，20 倍放大 |
| 最大灯珠数 | 300，宏定义可改 |

当前固件支持的对外功能以开发手册为准：[`doc/lcb_ecu_development_manual.md`](doc/lcb_ecu_development_manual.md)。

---

## 2. 工程目录

```text
lcb/
├── Core/
│   ├── Inc/                         # 工程头文件
│   └── Src/
│       ├── main.c                   # 主应用逻辑：灯效、串口、ADC保护、状态灯
│       ├── stm32g0xx_hal_msp.c      # HAL MSP 初始化
│       ├── stm32g0xx_it.c           # 中断入口
│       └── system_stm32g0xx.c       # 系统时钟相关
├── Drivers/                         # STM32 HAL / CMSIS 驱动
├── doc/
│   └── lcb_ecu_development_manual.md # 电控开发手册，协议和联调说明看这里
├── Dev.mk                           # 个人开发辅助 Makefile
├── Makefile                         # CubeMX / STM32 工程自动生成 Makefile
├── lcb.ioc                          # CubeMX 工程配置
├── STM32G030XX_FLASH.ld             # 链接脚本
└── startup_stm32g030xx.s             # 启动文件
```

维护原则：

```text
README.md：只放工程入口、构建、烧录、维护规则。
doc/lcb_ecu_development_manual.md：放电控协议、命令、状态、联调流程。
Core/Src/main.c：固件主逻辑，命令或动画变化后同步更新开发手册。
```

---

## 3. 环境配置

### 3.1 Makefile / GCC 开发环境

当前工程默认使用 CubeMX 生成的 Makefile 构建，推荐安装：

```text
arm-none-eabi-gcc
make
openocd
```

可选工具：

```text
picocom / minicom / screen    # 串口调试
JLinkExe                      # J-Link 烧录时使用
```

### 3.2 转换为 MDK-ARM / Keil 工程

如果电控队员或其他开发者习惯使用 Keil MDK，可以用 `lcb.ioc` 重新生成 MDK 工程。建议先备份当前工程，尤其是 `Core/Src/main.c` 和 `doc/` 目录。

推荐流程：

```text
1. 打开 STM32CubeMX。
2. 选择 File -> Open Project，打开工程根目录下的 lcb.ioc。
3. 进入 Project Manager -> Project。
4. 将 Toolchain / IDE 改为 MDK-ARM。
5. 确认芯片仍为 STM32G030F6P6。
6. 进入 Project Manager -> Code Generator。
7. 建议勾选 Generate peripheral initialization as a pair of .c/.h files per peripheral，便于 Keil 工程管理。
8. 点击 GENERATE CODE。
9. 生成完成后，打开 MDK-ARM 目录或工程根目录中的 .uvprojx 文件。
10. 在 Keil 中编译工程。
```

转换后检查项：

```text
1. Core/Src/main.c 是否仍为当前维护版本。
2. Core/Src/stm32g0xx_it.c 中 USART2 中断入口是否存在。
3. Core/Inc/stm32g0xx_hal_conf.h 中 SPI、UART、ADC、GPIO 等 HAL 模块是否开启。
4. Keil 工程中是否包含 startup_stm32g030xx.s。
5. Keil 工程中目标芯片是否为 STM32G030F6Px 系列。
6. 编译后 Flash / RAM 占用是否正常。
```

注意：

```text
1. CubeMX 重新生成工程可能覆盖部分文件，修改前先提交 git 或备份。
2. USER CODE BEGIN / USER CODE END 标记不要删除。
3. README、doc/ 开发手册、Dev.mk 不属于 Keil 必需文件，但建议保留在工程目录中。
4. 转成 MDK 后，README 中的 make / Dev.mk 烧录命令不再适用，烧录入口改为 Keil 的 Download / Debug。
5. 串口协议和电控对接方式不因转换到 MDK 而改变，仍以 doc/lcb_ecu_development_manual.md 为准。
```

---

## 4. 编译

使用工程自动生成的 Makefile：

```bash
make clean
make
```

常见输出：

```text
build/lcb.elf
build/lcb.hex
build/lcb.bin
```

也可以使用个人辅助 Makefile：

```bash
make -f Dev.mk build
make -f Dev.mk clean
make -f Dev.mk rebuild
```

---

## 5. 烧录与调试

默认烧录：

```bash
make -f Dev.mk flash
```

指定烧录器：

```bash
# ST-Link
make -f Dev.mk flash PROGRAMMER=stlink

# CMSIS-DAP / DAPLink
make -f Dev.mk flash PROGRAMMER=cmsis-dap ADAPTER_SPEED=500

# J-Link
make -f Dev.mk flash PROGRAMMER=jlink
```

复位：

```bash
make -f Dev.mk reset
```

启动 OpenOCD 调试服务器：

```bash
make -f Dev.mk debug
```

串口监视：

```bash
make -f Dev.mk serial PORT=/dev/ttyUSB0 BAUD=115200
```

查看当前 `Dev.mk` 配置：

```bash
make -f Dev.mk show
```

---

## 6. 最小联调入口

电控侧详细联调流程见：[`doc/lcb_ecu_development_manual.md`](doc/lcb_ecu_development_manual.md)。

快速冒烟测试可以使用以下命令：

```text
PING
OK
COUNT 60
BRI 80
COLOR 255 0 0
ANIM 2
STAT
```

注意：命令需要带换行符，推荐 `\r\n`。

---

## 7. 代码维护约定

### 7.1 不要随意改动自动生成文件

CubeMX / STM32CubeIDE 重新生成工程时，可能覆盖部分文件。建议：

```text
1. 保留 USER CODE BEGIN / USER CODE END 区域。
2. 不直接修改 Drivers/ 下的 HAL 库文件。
3. Dev.mk 作为个人开发入口，不替代自动生成 Makefile。
```

### 7.2 修改串口协议后必须同步开发手册

如果修改以下内容，必须同步更新：

```text
doc/lcb_ecu_development_manual.md
```

需要同步的变更包括：

```text
1. 新增或删除命令。
2. 修改命令参数范围。
3. 修改返回格式。
4. 修改 STAT 字段。
5. 修改 DEV 开发者模式行为。
6. 修改状态灯含义。
7. 修改 ANIM 编号或动画含义。
```

### 7.3 自定义动画维护入口

自定义动画的具体操作流程不写在 README 中，统一维护在：

```text
doc/lcb_ecu_development_manual.md
```

代码入口位于：

```text
Core/Src/main.c
```

搜索关键字：

```text
ANIM 4
custom_animation
自定义动画
```

---

## 8. 常见工程问题

### 8.1 烧录后没有立即运行

可以尝试：

```bash
make -f Dev.mk reset
```

如果仍无反应，手动断电重上电。部分板子烧录器复位链路不完整时，需要冷启动。

### 8.2 串口没有返回

优先检查：

```text
1. TX/RX 是否交叉连接。
2. GND 是否共地。
3. 波特率是否为 115200。
4. 命令是否带换行符。
5. 主控串口是否为 3.3V TTL，不是 RS232。
```

### 8.3 灯珠数量变多后异常

优先检查硬件侧：

```text
1. 5V 电源电流是否足够。
2. 灯带尾端是否压降过大，必要时中间/尾端补电。
3. MCU 和灯带是否可靠共地。
4. DATA 线是否过长，建议 DIN 串 220Ω~470Ω。
5. 3.3V MCU 直推 5V WS2812 可能边界不足，正式板建议加 74AHCT1G125 / 74HCT1G125。
```

---

## 9. 版本边界

当前工程 README 不展开描述协议细节。以下内容均以开发手册为准：

```text
1. 串口命令表。
2. STAT 返回格式。
3. 故障编号。
4. PB3 状态灯含义。
5. DEV 开发者模式。
6. ANIM 4 自定义动画流程。
7. 电控侧状态机。
8. 联调和异常处理流程。
```

开发手册路径：

```text
doc/lcb_ecu_development_manual.md
```

