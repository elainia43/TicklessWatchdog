# TicklessWatchdog

面向特色化领域的实践 2 - 嵌入式软件实验十期末大作业源码工程。

## 题目

基于 QEMU 的 RTOS Tickless Idle 与软件看门狗机制设计与实现

## 分组信息

| 姓名 | 学号 |
|---|---|
| 雷蕾 | 2023211435 |
| 张玉祯 | 2023211406 |
| 赵开妍 | 2023211408 |
| 万庆如 | 2023211344 |

## 项目功能

本工程基于前序实验的 RTOS 内核继续扩展，主要新增两个机制：

1. Tickless Idle 调度决策框架
   - idle task 运行时检查系统是否真正空闲。
   - 根据最近的软件定时器到期时间计算可空闲窗口。
   - 记录进入次数、理论可跳过 tick 数和最大空闲窗口。
   - 在 QEMU 环境下保留原 SysTick 节拍，保证前序实验回归测试稳定。

2. 软件看门狗
   - 支持任务注册看门狗对象。
   - 支持启动、喂狗、停止、删除和超时计数查询。
   - 在系统 tick 中周期扫描活跃看门狗。
   - 超时后记录异常次数，不直接 panic，便于测试继续运行。

## 主要改动文件

```text
kern/include/k_config.h    新增 Tickless 和 Watchdog 配置项
kern/include/k_kern.h      新增 Tickless 统计接口、Watchdog 结构体和 API
kern/k_time.c              新增最近定时器查询、Tickless 统计、Watchdog 扫描入口
kern/k_task.c              idle task 接入 tickless_idle_enter()
kern/k_watchdog.c          软件看门狗模块实现
kern/test/tests.c          新增 Tickless Idle 和软件看门狗测试
```

## 编译环境

工程使用实验包自带工具链：

```text
GNU Make
Arm GNU Toolchain 13.2.1
xPack QEMU Arm 7.2.5
```

推荐将工程放在纯英文路径下，避免中文路径影响 Make、GCC 或 QEMU。

## 编译步骤

在 PowerShell 中执行：

```powershell
cd D:\em2-labs\em2-lab-pack\Lab10-tickless-watchdog

$tools='D:\em2-labs\em2-lab-pack\tools'
$env:PATH="$tools\make;$tools\arm-gnu-toolchain-13.2.Rel1-mingw-w64-i686-arm-none-eabi\bin;$tools\xpack-qemu-arm-7.2.5-1\bin;$env:PATH"

make clean
make all
```

编译成功后会在 `build` 目录生成：

```text
Lab10-tickless-watchdog.elf
Lab10-tickless-watchdog.hex
Lab10-tickless-watchdog.bin
Lab10-tickless-watchdog.lst
Lab10-tickless-watchdog.map
```

## QEMU 运行步骤

现场演示推荐直接运行：

```powershell
.\demo-run.ps1
```

或者双击：

```text
demo-run.bat
```

脚本会自动配置工具链路径、执行 `make clean` 和 `make all`，然后停在启动 QEMU 前。给老师展示时按一次回车即可进入 QEMU 运行输出。

手动运行命令如下：

```powershell
& "D:\em2-labs\em2-lab-pack\tools\xpack-qemu-arm-7.2.5-1\bin\qemu-system-gnuarmeclipse.exe" `
  -M STM32F4-Discovery `
  -kernel "D:\em2-labs\em2-lab-pack\Lab10-tickless-watchdog\build\Lab10-tickless-watchdog.elf" `
  -nographic `
  -serial null `
  -serial mon:stdio
```

退出 QEMU：

```text
Ctrl + A，然后按 X
```

## 测试说明

新增测试会在原 Lab1-Lab9 回归测试基础上额外执行：

```text
=== start tickless idle test ===
=== start software watchdog test ===
```

关键输出示例：

```text
tickless stats: enter ... suppressed ... max_window ...
watchdog feed path expired=0
watchdog stop path expired=0
watchdog timeout path expired=2
=== all test done ===
```

看到 `=== all test done ===` 且没有 `[ FAILED ]` 或 `Failed Assertion`，说明测试通过。

本地验证日志位于：

```text
D:\em2-labs\results\logs\Lab10-tickless-watchdog.log
```

## 说明

本工程选择 QEMU 虚拟平台进行验证，不依赖实体开发板。Tickless Idle 部分实现的是调度决策和统计框架；在真实硬件上可以继续把该框架接入 SysTick 重装载、低功耗指令或 RTC 唤醒机制。
