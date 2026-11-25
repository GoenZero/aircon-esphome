# aircon-esphome

一台三菱 霧ヶ峰 MSZ-GV2519-W 专用的 ESPHome 固件：M5Stack NanoC6（ESP32-C6，4 MB）接在空调的 CN105 口上，通过 Home Assistant 和自带网页控制空调。

只编入这台空调有的功能，不带任何通用设备的多余部分。固件和仓库里没有任何密码。

## 组成

| 部分 | 版本 |
|---|---|
| ESPHome | `requirements.txt` 里固定的版本 |
| 空调通信组件 | [echavet/MitsubishiCN105ESPHome](https://github.com/echavet/MitsubishiCN105ESPHome)，`aircon.yaml` 里固定到具体提交 |
| 硬件参考 | [Serin-Labs/serin-cn105](https://github.com/Serin-Labs/serin-cn105) 的 NanoC6 接线（串口 GPIO1/2，彩灯 GPIO20、供电 GPIO19） |

## 这台空调有什么（三菱说明书 jg79d213h01）

- 模式：制冷、除湿、制热（遥控器上没有自动、送风，所以不编入）
- 风速：自动、静、弱、強，另有「パワフル」；除湿另有弱 / 標準 / 強三档强度
- 上下风向：自动、1–5、摆动；左右风向是手动拨片（不编入）
- 温度：制冷 / 制热 16–31 ℃、1 ℃ 一档
- 自动功能：除霜（之前有预热）、内部清洁
- 没有：i-see、换气、空气净化、双设定点

## 实体

固件里用英文名（网页显示英文）；Home Assistant 里显示中文名，在 HA 实体设置里改。

| 固件名 | HA 显示 | 说明 |
|---|---|---|
| （空调本体） | 空调 | 模式、风速、摆风、温度、室温、运行动作（制热中、除霜中…）由 HA 翻译 |
| Vertical Vane | 上下风向 | 选项本身是中文：自动、1（最上）…5（最下）、摆动 |
| Compressor Frequency | 压缩机频率 | 停机时报 0 Hz |
| Outdoor Temperature | 室外温度 | |
| Input Power | 输入功率 | |
| Energy | 累计用电量 | |
| Runtime | 累计运行时长 | |
| Connection Uptime | 本次连接时长 | |
| Target Humidity | 目标湿度 | 观察中：看是否对应除湿三档 |
| Stage | 运行阶段 | 值为中文 |
| Sub Mode | 运行子模式 | 正常 / 暖机 / 除霜 / 预热 / 待机 / 已关机 |
| Auto Sub Mode | 自动子模式 | 观察中 |
| Error Code | 故障代码 | 无故障 / 故障码 0xNN，子码 0xMM |
| Functions / Read Functions | 功能设定 / 读取功能设定 | 只读，不能改空调内部设置 |
| Operating | 运行中 | 压缩机运行位 |
| WiFi Signal / IP Address / SSID / ESPHome Version / Uptime | 无线信号 / 网络地址 / 无线网络 / 固件版本 / 控制器开机时长 | |

另有：蓝牙代理（帮 HA 接收附近的蓝牙设备）；状态灯平时不亮，和空调的连接断开约 35 秒后亮红灯。

不编入：改写空调设置的入口、左右风向、按键实体、加密和密码、网页界面打包（界面从 ESPHome 官网加载）。

## 编译

推送到 `main` 或提交 PR 时，GitHub Actions 自动编译两个配置，产物在 Actions 的 Artifacts 里；打 `v*` 标签时发布到 Releases。每次编译都会核对分区表和设备上的一致、程序能放进一个程序槽，结果写在运行摘要里。

| 配置 | 用途 |
|---|---|
| `aircon.yaml` | 正式固件 |
| `aircon-setup.yaml` | 首次安装用：正式固件 + 两个一次性按钮（擦除备用程序槽、清空设置存储区），用完换成正式固件 |

本地编译：`pip install -r requirements.txt` 后 `esphome compile aircon.yaml`。

## 首次安装（从其他固件换过来，不留残留）

1. 在设备网页（OTA）上传 `aircon-setup.ota.bin`，设备从另一个程序槽启动。此时仍用之前存下的 Wi-Fi。
2. 按「Erase Standby Slot」：整块擦掉另一个槽里的旧固件。
3. 按「Wipe Settings」：清空整个设置存储区（包括 Wi-Fi），设备重启。
4. 约 1 分钟后设备开出热点「aircon」（无密码）。手机连上，在弹出页面选家里的 Wi-Fi、填密码。
5. 在设备网页上传 `aircon.ota.bin`，写进刚擦干净的槽。

完成后两个程序槽里都是本固件，设置存储区里只有本固件的数据。

## 更新

- ESPHome 每月一个大版本加若干修复版；出了第一个修复版（x.1）再升。Dependabot 会自动提升级 PR，编译通过再合并。
- 空调组件的提交手动更新。一次只换一个版本。
- 升级：在设备网页上传新的 `aircon.ota.bin`。Wi-Fi 设置会保留。

## 回退

- 功能异常：网页上传上一版的 `aircon.ota.bin`（Releases 里有）。
- 启动失败：ESPHome 会进入安全模式，仍可无线上传。
- 连不上网：连设备热点重新填 Wi-Fi。
- 都不行：按住设备按键（GPIO9）接 USB，进入下载模式重刷。
