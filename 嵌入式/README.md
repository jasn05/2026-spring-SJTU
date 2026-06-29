# S800 智能联网时钟

- 学生：姜圣基
- 学号：523170910011
- 标识：JIANGSJ
- 开发板：TM4C1294 LaunchPad + S800
- 工具：Keil MDK 5、TivaWare、Python Tkinter、pyserial

## 运行方式

1. 使用 Keil 打开 `S800_NetworkClock.uvprojx`，编译并烧录。
2. 安装上位机依赖：`python -m pip install -r requirements.txt`
3. 启动上位机：`python pc_app.py`
4. 选择开发板对应 COM 口，点击“连接”。

## 上位机已实现

- 自动扫描 COM、连接、断开、后台读取、1 Hz 心跳和延迟显示。
- 状态栏显示连接、FORMAT、MODE、ALARM、延迟。
- 8 位七段数码管、小数点、8 位 LED 和 10 个虚拟按键镜像。
- 覆盖基础协议命令的控制面板、参数组合和大小写演示。
- 带时间戳、方向和颜色分类的日志，支持导出。
- 串口占用、断开、异常帧、超长命令和 ERROR 应答保护。
- E1 网络对时：PC 从 NTP 获取 UTC 时间，转换为北京时间 UTC+8 后下发，USER1 触发同样流程。
- E2 天气获取：PC 默认使用上海市闵行区坐标，从 Open-Meteo 获取温度/天气并下发，USER2 触发 5 秒短显。
- E3 自动昼夜模式：PC 根据经纬度计算当天日出日落，自动下发 `*SET:MODE DAY/NIGHT`。
- E4 数据可视化看板：持久化记录闹钟、对时、按键、模式、天气、专注、错误等事件，并用图表展示统计结果。
- E5 音乐蜂鸣器：利用 Timer 软件翻转 GPIO 模拟方波，播放“起风了”简谱旋律，PC 可下发开始/停止命令。

## 正式事件

```text
#PONG <uptime_s>
#EVT:DISP <8字符> <dpHex>
#EVT:LED <hex2>
#EVT:KEY <NAME>
#EVT:MODE DAY/NIGHT
#EVT:ALARM
#EVT:ALARM_OFF
```

板端暂时保留旧 `STATE,...` 状态行，上位机也兼容解析。

## 扩展命令

```text
*SET:SYNC OK
*SET:WEATHER <temp2> SUN/RAIN/CLOUD/HOT
*SET:MODE DAY
*SET:MODE NIGHT
*SET:MUSIC WIND
*SET:MUSIC STOP
```

`*SET:SYNC OK` 会让板端用 LED4 短时指示对时成功。
`*SET:WEATHER` 会保存天气，USER2 按下后数码管短显 5 秒。
`*SET:MODE DAY/NIGHT` 用于昼夜显示切换；PC 上位机可按日出日落自动下发。
`*SET:MUSIC WIND/STOP` 用于播放或停止“起风了”蜂鸣器旋律。

## S800 板端 LED 定义

| LED | 位值 | 基础含义 | 扩展复用 |
| --- | --- | --- | --- |
| LED1 | 0x01 | 系统心跳，约 0.5 秒翻转一次 | NIGHT 模式下保留心跳 |
| LED2 | 0x02 | 闹钟已启用 | 无 |
| LED3 | 0x04 | 闹钟正在响铃 | 无 |
| LED4 | 0x08 | 编辑模式 | 网络对时成功后短时点亮 |
| LED5 | 0x10 | 串口收发活动 | 无 |
| LED6 | 0x20 | FORMAT RIGHT 状态 | 天气晴 SUN 指示 |
| LED7 | 0x40 | DISPLAY OFF 状态 | 天气雨 RAIN 指示 |
| LED8 | 0x80 | 流水显示内容超过 8 位 | 天气高温 HOT 指示 |

以上 8 位 LED 全部使用，至少覆盖系统心跳、闹钟状态、编辑模式、串口收发活动四类基础状态。

## 建议验收测试

1. 连接后观察每秒出现 `*PING` 和 `#PONG`，状态栏显示延迟。
2. 点击 FORMAT、MODE、DISPLAY 按钮，确认板端和镜像同步。
3. 点击十个虚拟按键，确认效果与物理按键一致。
4. 发送 `*SET:MSG Hello.S800123`，确认流水方向、小数点和镜像。
5. 发送 `*SET:ALARM HOUR MIN SEC hh mm ss`，确认响铃和 LED。
6. 发送错误命令、断开 USB、占用串口，确认 GUI 不崩溃。
7. 点击“网络对时”，确认日期时间下发，LED4 短时点亮。
8. 点击“获取天气”，再按 USER2，确认数码管短显天气且 PC 镜像跟随。
9. 勾选“自动昼夜”，确认状态栏显示 `AUTO：DAY/NIGHT` 和日出日落时间，板端 MODE 自动同步。
10. 点击“打开看板”，确认事件类型柱状图、24 小时分布图和最近事件表正常显示，可导出 CSV。
