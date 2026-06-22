# esp-uart — 小智串口对接接收板

本工程为 **ESP32-S3 串口从机**，通过 UART 接收 [xiaozhi-esp32-2.2.4](../xiaozhi-esp32-2.2.4) 下发的**时间同步**与**设备控制**命令，并驱动本地 GPIO（灯、风扇、继电器等）。

---

## 系统架构

```text
┌─────────────────────┐         UART 115200          ┌─────────────────────┐
│  xiaozhi-esp32      │  GPIO17(TX) ──────────────►  │  esp-uart（本工程）  │
│  (小智主控)          │  GPIO21(RX) ◄──────────────  │  GPIO21(RX)         │
│                     │         GND 共地              │  GPIO17(TX)         │
│  - 语音/MCP 控设备   │                              │  - 解析命令          │
│  - 每秒发送 TIME    │                              │  - 驱动外设 GPIO     │
└─────────────────────┘                              └─────────────────────┘
         │                                                      │
         │ USB (COM33) 调试日志                                  │ GPIO → 灯/继电器…
         ▼                                                      ▼
    idf monitor                                            外接负载
```

---

## 硬件接线

### 两块 ESP32 之间（必接）

| 小智板 (bread-compact-wifi) | 接收板 (esp-uart) | 说明 |
|----------------------------|-------------------|------|
| GPIO **17** (TX) | GPIO **21** (RX) | 小智发送 → 本机接收 |
| GPIO **21** (RX) | GPIO **17** (TX) | 本机发送 → 小智接收（可选，用于回传 `OK`） |
| **GND** | **GND** | 必须共地 |

- 波特率：**115200**，8N1，无流控  
- UART 端口：双方均为 **UART1**  
- **不要使用** 板子丝印 **TX/RX（GPIO43/44）**：与 USB 转串口芯片共用，插 USB 时易乱码（小智 `config.h` 中亦有说明）

### 接收板外设（按 `main/device_ctrl.h` 修改）

| 逻辑设备 | 默认 GPIO | 典型负载 |
|----------|-----------|----------|
| lamp（灯） | 2 | LED / 继电器模块 |
| fan（风扇） | 1 | 风扇模块 |
| relay1 | 8 | 继电器 1 |
| relay2 | 9 | 继电器 2 |
| buzzer（蜂鸣器） | 10 | 有源蜂鸣器 |
| pump（水泵） | 11 | 水泵继电器 |

> 高电平 = 开（ON），低电平 = 关（OFF）。请根据模块触发电平（高有效/低有效）加反相或改代码。

---

## 串口协议

每条消息以 `\r\n` 结尾，一行一条。

### 1. 时间同步（小智 → 本机，只读日志）

| 消息 | 说明 |
|------|------|
| `TIME_UART_READY` | 小智上电约 3 秒后发送，表示串口就绪 |
| `TIME:2026-05-27 14:30:01` | 已联网对时，每秒一条 |
| `TIME:NOT_SYNCED` | 未对时 |

本机仅在调试终端打印，不控制 GPIO。

### 2. 设备控制（小智 → 本机，执行动作）

格式：

```text
CMD:<设备>_ON
CMD:<设备>_OFF
```

| 命令示例 | 动作 |
|----------|------|
| `CMD:LAMP_ON` / `CMD:LAMP_OFF` | 灯 |
| `CMD:FAN_ON` / `CMD:FAN_OFF` | 风扇 |
| `CMD:RELAY1_ON` / `CMD:RELAY1_OFF` | 继电器 1 |
| `CMD:RELAY2_ON` / `CMD:RELAY2_OFF` | 继电器 2 |
| `CMD:BUZZER_ON` / `CMD:BUZZER_OFF` | 蜂鸣器 |
| `CMD:PUMP_ON` / `CMD:PUMP_OFF` | 水泵 |

执行成功后，本机回复：

```text
OK
```

### 3. 中文测试命令（仅本机支持，便于串口助手调试）

| 命令 | 等价于 |
|------|--------|
| `开灯` / `关灯` | `CMD:LAMP_ON` / `CMD:LAMP_OFF` |
| `开风扇` / `关风扇` | `CMD:FAN_ON` / `CMD:FAN_OFF` |
| `开继电器1` / `关继电器1` | `CMD:RELAY1_ON` / `CMD:RELAY1_OFF` |
| `开继电器2` / `关继电器2` | `CMD:RELAY2_ON` / `CMD:RELAY2_OFF` |
| `开蜂鸣器` / `关蜂鸣器` | `CMD:BUZZER_ON` / `CMD:BUZZER_OFF` |
| `开水泵` / `关水泵` | `CMD:PUMP_ON` / `CMD:PUMP_OFF` |

---

## 与小智工程的联动

小智工程路径：`B:\ESP32_project\ESP32_CLion\xiaozhi-esp32-2.2.4`

| 小智文件 | 作用 |
|----------|------|
| `main/boards/common/uart_time_sender.h` | 定时发送 `TIME:...` |
| `main/boards/common/uart_peer_sender.h` | MCP 操作时发送 `CMD:...` |
| `main/boards/common/lamp_controller.h` | 开/关灯时发送 `CMD:LAMP_ON/OFF` |
| `main/boards/common/gpio_controller.h` | 风扇/继电器等发送 `CMD:FAN_ON` 等 |
| `main/boards/bread-compact-wifi/config.h` | UART 引脚：GPIO17/21 |

对小智说「开灯」「关风扇」等，小智会：

1. 控制**本板**对应 GPIO（如小智灯在 GPIO18）  
2. 经 UART 向 **esp-uart 接收板** 发送 `CMD:...`，驱动外接设备  

**两个工程均需重新编译烧录。**

---

## 工程目录说明

```text
esp-uart/
├── main/
│   ├── esp-uart.c       # 入口：初始化设备 + 串口 + 接收任务
│   ├── uart_comm.c/h    # UART1 收发、按行解析协议
│   └── device_ctrl.c/h  # GPIO 设备表、命令执行
├── README.md            # 本文档
└── sdkconfig            # ESP-IDF 配置
```

---

## 编译与烧录

### 环境要求

- ESP-IDF **v5.5.x**（与 xiaozhi 一致）  
- 目标芯片：**esp32s3**

### 命令

```powershell
# 进入 ESP-IDF 环境后
cd B:\ESP32_project\ESP32_CLion\esp-uart
idf.py set-target esp32s3
idf.py build
idf.py -p COM33 flash monitor
```

> 将 `COM33` 换成你电脑上 **USB 调试口** 的实际端口号（与 xiaozhi 烧录口可能不同，两块板各有一个 COM 口）。

### Cursor / VS Code

在 `.vscode/settings.json` 中已配置 `idf.portWin`、`idf.monitorPort` 等，可使用 ESP-IDF 扩展的 **Build / Flash / Monitor** 任务。

---

## 调试示例

监视器正常启动后，应看到类似日志：

```text
I (270) uart_comm: UART1 peer link: TX=GPIO17 RX=GPIO21 115200 baud (xiaozhi)
I (270) uart_comm: listening for xiaozhi TIME messages...
I (270) device_ctrl: devices ready (lamp/fan/relay1/relay2/buzzer/pump)
```

小智联网对时后：

```text
I (xxx) uart_comm: xiaozhi UART ready, waiting for TIME:...
I (xxx) uart_comm: xiaozhi time -> 2026-05-27 14:30:01
```

语音开灯后：

```text
I (xxx) device_ctrl: lamp ON
```

---

## 常见问题

### 1. 监视器没有任何日志

- 确认打开的是**本板** USB 对应的 COM 口，不是小智的口  
- 波特率 **115200**  
- 按 RST 复位后再看

### 2. 有时间但没有 `lamp ON`

- 检查 GPIO17↔21 交叉接线与 GND  
- 确认小智已烧录带 `uart_peer_sender.h` 的版本  
- 用串口助手向接收板直接发 `CMD:LAMP_ON` 或 `开灯` 验证 GPIO

### 3. 曾出现 Guru Meditation（已修复）

不要直接调用 `usb_serial_jtag_read_bytes()`；本工程仅通过 **UART1 + GPIO17/21** 与小智通信，日志走 USB 调试口即可。

### 4. 想改引脚或增加设备

1. 修改 `main/device_ctrl.h` 中 GPIO 宏  
2. 在 `main/device_ctrl.c` 的 `s_devices[]` 增加条目  
3. 小智侧在 `compact_wifi_board.cc` 的 `InitializeTools()` 增加 `GpioController`，名称会映射为 `CMD:<名称大写>_ON`

---

## 协议扩展示例

新增设备 `heater`（GPIO12）时：

**esp-uart — `device_ctrl.c`：**

```c
{ "heater", GPIO_NUM_12, false },
```

**小智 — `compact_wifi_board.cc`：**

```cpp
static GpioController heater("heater", GPIO_NUM_12);
```

语音/MCP 调用 `self.heater.turn_on` 时，小智会自动发送 `CMD:HEATER_ON`。

---

## 华为云 IoTDA（MQTT）

### 连接参数

将华为云控制台下载的 `DEVICES-CONNECT-KEY-*.txt` 填入 `main/huawei_iot_config.h`（可参考 `huawei_iot_config.h.example`）。当前工程已配置：

| 项 | 值 |
|----|-----|
| 设备 ID | `6a1530d9b73c0c36b081fc53` |
| 接入域名 | `1e4fc192af.iot-mqtts.cn-north-4.myhuaweicloud.com` |
| 端口 | **8883**（MQTTS） |

> `huawei_iot_config.h` 已加入 `.gitignore`，避免密钥泄露。若连接失败，请在控制台重新生成 **clientId** 后更新该文件。

### 配置 Wi-Fi

```powershell
idf.py menuconfig
# → WiFi Configuration → 填写 WIFI_SSID / WIFI_PASSWORD
```

### 物模型属性 `lamp`（服务 `skills`）

设备向平台上报（控制台「物模型数据」会刷新）：

- Topic：`$oc/devices/{device_id}/sys/properties/report`
- 载荷示例：

```json
{
  "services": [
    {
      "service_id": "skills",
      "properties": { "lamp": 1 }
    }
  ]
}
```

上报时机：MQTT 连接成功后上报当前灯状态；之后每次本地灯状态变化（串口 `CMD:LAMP_ON/OFF`、云端命令等）自动再上报。

云端下发属性（控制台改灯 / 属性设置）：

- 订阅：`$oc/devices/{device_id}/sys/properties/set/#`
- 载荷示例：

```json
{
  "services": [
    {
      "service_id": "skills",
      "properties": { "lamp": 1 }
    }
  ]
}
```

`service_id` 在 `huawei_iot_config.h` 的 `HUAWEI_IOT_SERVICE_ID` 中配置，须与产品模型一致。

### 云端下发命令控制 lamp（物模型 `lamp_control`）

控制台路径：**设备 → 命令 → `lamp_control` → 下发**。

与产品模型 **skills** 服务一致：

| 项 | 名称 | 类型 | 说明 |
|----|------|------|------|
| 命令 | `lamp_control` | — | 控制灯 |
| 下发参数 | `lamp-stat` | int | **1=开，0=关** |
| 响应参数 | `lamp-control` | int | **1=开，0=关** |
| 属性 `lamp` | int | 上报 **1=开，0=关** |

下发示例：

```json
{
  "command_name": "lamp_control",
  "paras": {
    "lamp-stat": 1
  }
}
```

设备响应示例：

```json
{
  "result_code": 0,
  "response_name": "COMMAND_RESPONSE",
  "paras": {
    "lamp-control": 1
  }
}
```

仍兼容旧格式：`LAMP_ON` / `LAMP_OFF`、`paras.device` 等。

也支持纯文本：`CMD:LAMP_ON`、`开灯` 等（与串口协议相同）。

执行成功后设备回复 `result_code: 0`，并自动 **属性上报** 最新 `lamp` 状态（物模型数据页会同步）。

### 云端下发属性（与命令二选一）

订阅：`$oc/devices/{device_id}/sys/properties/set/#`

```json
{
  "services": [
    { "service_id": "skills", "properties": { "lamp": 0 } }
  ]
}
```

### 日志

```text
I (xxx) huawei_iot: Wi-Fi got IP
I (xxx) huawei_iot: MQTT connected to Huawei Cloud
I (xxx) device_ctrl: lamp ON
```

---

## 版本记录

| 日期 | 说明 |
|------|------|
| 2026-05 | 华为云 IoTDA MQTTS 接入 |
| 2026-05 | 初始版本：UART 对接小智时间 + CMD 设备控制 |

---

## 参考

- 小智 UART 发送：`xiaozhi-esp32-2.2.4/main/boards/common/uart_time_sender.h`  
- 小智接线注释：`xiaozhi-esp32-2.2.4/main/boards/bread-compact-wifi/config.h`
