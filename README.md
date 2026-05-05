# LCB WS2812 灯带控制板工程

本仓库是基于 **STM32G030F6P6** 的 LCB WS2812 灯带控制板固件工程，包含 STM32CubeMX 配置、`main.c` 主逻辑、Makefile 构建入口、个人开发辅助脚本和电控开发手册。

> README 只作为**工程索引、目录、构建烧录入口和维护约定**。  
> 串口协议、命令返回、状态灯、DEV 开发者模式、保护阈值、ANIM 4 自定义动画和电控联调流程，统一维护在：[`doc/lcb_ecu_development_manual.md`](doc/lcb_ecu_development_manual.md)。

---

## 1. 快速入口

| 你要做什么 | 看哪里 |
|---|---|
| 看电控接线、串口协议、命令表 | [`doc/lcb_ecu_development_manual.md`](doc/lcb_ecu_development_manual.md#3-串口通信协议) |
| 看上电握手和主控初始化流程 | [`doc/lcb_ecu_development_manual.md`](doc/lcb_ecu_development_manual.md#4-上电流程与主控初始化) |
| 看 `PING` / `OK` / `STAT` / `COUNT` / `BRI` / `COLOR` / `ANIM` / `OFF` | [`doc/lcb_ecu_development_manual.md`](doc/lcb_ecu_development_manual.md#5-普通命令总表) |
| 看 `STAT` 返回字段和故障编号 | [`doc/lcb_ecu_development_manual.md`](doc/lcb_ecu_development_manual.md#63-stat) |
| 看 WS2812 动画和 `ANIM 4` 自定义入口 | [`doc/lcb_ecu_development_manual.md`](doc/lcb_ecu_development_manual.md#7-动画说明) |
| 看电压、电流、低压、过压、过流保护逻辑 | [`doc/lcb_ecu_development_manual.md`](doc/lcb_ecu_development_manual.md#8-电压电流与保护逻辑) |
| 看 PB3 状态灯含义 | [`doc/lcb_ecu_development_manual.md`](doc/lcb_ecu_development_manual.md#10-pb3-状态灯说明) |
| 看 DEV 开发者模式 | [`doc/lcb_ecu_development_manual.md`](doc/lcb_ecu_development_manual.md#11-dev-开发者模式) |
| 看电控侧状态机和解析建议 | [`doc/lcb_ecu_development_manual.md`](doc/lcb_ecu_development_manual.md#12-电控侧推荐状态机) |
| 看联调测试流程和常见问题 | [`doc/lcb_ecu_development_manual.md`](doc/lcb_ecu_development_manual.md#15-联调测试流程) |
| 看主程序源码 | [`Core/Src/main.c`](Core/Src/main.c) |
| 看 CubeMX 配置 | [`lcb.ioc`](lcb.ioc) |
| 编译工程 | [`Makefile`](Makefile) / [`Dev.mk`](Dev.mk) |
| 烧录、复位、串口监视 | [`Dev.mk`](Dev.mk) |
| 看硬件开源资料 | [OSHWHub / 立创开源硬件平台](https://oshwhub.com/funtaser/project_jbyzhaat) |

---

## 2. 项目概况

| 项目 | 说明 |
|---|---|
| MCU | STM32G030F6P6 |
| 灯带 | WS2812，5V，三线制 |
| 灯带数据脚 | PA7 / SPI1_MOSI |
| 电控通信 | USART2，115200 8N1 |
| 状态指示灯 | PB3，低电平点亮 |
| 电压采样 | PA0，24V 母线分压采样 |
| 电流采样 | PA6，灯带电流采样 |
| 最大灯珠数 | 300，见 `WS2812_LED_MAX` |
| 默认灯珠数 | 300，见 `WS2812_LED_DEFAULT` |
| 默认亮度 | 80 / 255，见 `DEFAULT_BRIGHTNESS` |

当前固件支持 WS2812 静态颜色、内置动画、自定义动画入口、串口文本命令、ADC 电压/电流检测、保护状态提示、Type-C / USB 调试供电识别、PB3 状态灯和 DEV 开发者模式。具体行为不要在 README 中重复维护，以开发手册和 `main.c` 为准。

---

## 3. 仓库结构

```text
lcb/
├── Core/
│   ├── Inc/                          # 工程头文件
│   └── Src/
│       ├── main.c                    # 主应用逻辑：灯效、串口、ADC 保护、状态灯
│       ├── stm32g0xx_hal_msp.c       # HAL MSP 初始化
│       ├── stm32g0xx_it.c            # 中断入口
│       └── system_stm32g0xx.c        # 系统时钟相关
├── Drivers/                          # STM32 HAL / CMSIS 驱动
├── doc/
│   └── lcb_ecu_development_manual.md # 电控开发手册
├── Dev.mk                            # 个人开发辅助 Makefile
├── Makefile                          # CubeMX / STM32 工程自动生成 Makefile
├── lcb.ioc                           # STM32CubeMX 工程配置
├── README.md                         # 工程入口说明
├── STM32G030XX_FLASH.ld              # 链接脚本
└── startup_stm32g030xx.s             # 启动文件
```

---

## 4. 文档分工

| 文件 | 定位 | 应该放什么 | 不应该放什么 |
|---|---|---|---|
| `README.md` | 仓库入口 | 项目简介、文件索引、构建烧录、维护规范、发布检查 | 大段协议表、状态灯细节、保护阈值、动画实现细节 |
| `doc/lcb_ecu_development_manual.md` | 电控开发手册 | 接线、串口协议、命令、返回格式、状态灯、DEV、保护逻辑、联调流程 | 编译器安装细节、烧录器命令合集、仓库目录说明 |
| `Core/Src/main.c` | 固件行为源头 | 实际固件逻辑和当前参数 | 面向电控队员的长篇解释 |
| `lcb.ioc` | CubeMX 配置源头 | 外设、时钟、引脚配置 | 协议说明 |

维护原则：

1. `main.c` 是固件行为的最终依据。
2. 面向电控队员的行为说明优先写入开发手册。
3. README 只保留索引和构建维护信息；能链接手册的内容不在 README 中重复展开。
4. 修改命令、返回格式、状态灯、DEV、保护阈值、动画编号后，必须同步更新开发手册。
5. 修改构建、烧录、目录结构、硬件开源链接后，必须同步更新 README。

---

## 5. 环境配置

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

---

## 6. 编译

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

## 7. 烧录与调试

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

电控侧串口命令和冒烟测试步骤见：[`doc/lcb_ecu_development_manual.md`](doc/lcb_ecu_development_manual.md#15-联调测试流程)。

---

## 8. Keil / MDK-ARM 工程生成

如果需要使用 Keil MDK，可以通过 `lcb.ioc` 重新生成 MDK 工程。生成前建议先提交 git 或备份以下文件：

```text
Core/Src/main.c
Core/Src/stm32g0xx_it.c
Core/Inc/stm32g0xx_hal_conf.h
doc/
README.md
Dev.mk
```

转换步骤：

```text
1. 打开 STM32CubeMX。
2. 选择 File -> Open Project。
3. 打开工程根目录下的 lcb.ioc。
4. 进入 Project Manager -> Project。
5. 将 Toolchain / IDE 改为 MDK-ARM。
6. 确认芯片仍为 STM32G030F6P6。
7. 进入 Project Manager -> Code Generator。
8. 建议勾选 Generate peripheral initialization as a pair of .c/.h files per peripheral。
9. 点击 GENERATE CODE。
10. 打开生成的 .uvprojx 文件。
11. 在 Keil 中编译工程。
```

转换后重点检查：

```text
1. Core/Src/main.c 是否仍为当前维护版本。
2. Core/Src/stm32g0xx_it.c 中 USART2 中断入口是否存在。
3. Core/Inc/stm32g0xx_hal_conf.h 中 SPI、UART、ADC、GPIO 等 HAL 模块是否开启。
4. Keil 工程中是否包含 startup_stm32g030xx.s。
5. Keil 工程中目标芯片是否为 STM32G030F6Px 系列。
6. 编译后 Flash / RAM 占用是否正常。
```

注意：转成 MDK 后，README 中的 `make` / `Dev.mk` 烧录命令不再适用；烧录入口改为 Keil 的 Download / Debug。

---

## 9. 硬件与固件开源分工

硬件设计文件发布在 OSHWHub / 立创开源硬件平台：

```text
https://oshwhub.com/funtaser/project_jbyzhaat
```

硬件平台通常用于发布：

```text
1. 原理图
2. PCB
3. BOM
4. Gerber 文件
5. 3D 模型
```

本仓库保存：

```text
1. STM32G030 固件源码
2. CubeMX 配置
3. Makefile / Dev.mk 构建烧录入口
4. 电控开发文档
```

---

## 10. 代码维护约定

### 10.1 CubeMX 生成边界

建议：

```text
1. 保留 USER CODE BEGIN / USER CODE END 区域。
2. 不直接修改 Drivers/ 下的 HAL 库文件。
3. Dev.mk 作为个人开发入口，不替代自动生成 Makefile。
4. CubeMX 重新生成前先提交 git。
```

### 10.2 修改后同步文档

修改内容与同步目标：

| 修改内容 | 同步文件 |
|---|---|
| 串口命令、参数范围、返回格式 | `doc/lcb_ecu_development_manual.md` |
| `STAT` 字段、故障编号、状态灯含义 | `doc/lcb_ecu_development_manual.md` |
| DEV 开发者模式行为 | `doc/lcb_ecu_development_manual.md` |
| ANIM 编号、自定义动画流程 | `doc/lcb_ecu_development_manual.md` |
| 保护阈值、ADC 采样策略 | `doc/lcb_ecu_development_manual.md` |
| 构建、烧录、目录结构、硬件链接 | `README.md` |
| README 中新增导航目标 | 确认手册对应章节存在 |

---

## 11. 发布前检查清单

每次发布或提交前建议执行：

```bash
make clean
make
make -f Dev.mk flash
```

然后按开发手册中的联调流程检查：[`doc/lcb_ecu_development_manual.md`](doc/lcb_ecu_development_manual.md#15-联调测试流程)。

最低发布检查：

```text
1. 程序能编译并烧录。
2. 上电后固件能运行。
3. 串口能通信。
4. 灯带能显示静态颜色。
5. 动画能运行。
6. STAT 能返回。
7. README 只保留入口/构建/维护信息。
8. 协议和联调细节已同步到开发手册。
```

---

## 12. 许可证

```text
固件源码：MIT License
文档：MIT License
硬件设计文件：以立创开源硬件平台项目页面声明的许可证为准
```
