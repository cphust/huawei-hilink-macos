# 验证记录

[English](VALIDATION.md) | **简体中文** · [使用指南](../README.zh-CN.md)

本记录区分保留的硬件证据、此前项目记录，以及准备 GitHub 上传时重新完成的检查。以下时间采用原始环境的 `America/Los_Angeles` 时区，2026-10-25 当天为 `UTC−07:00`。

## 原始验证环境

| 项目 | 内容 |
| --- | --- |
| 电脑 | M4 Mac mini |
| macOS | 15.1.1，系统版本号 `24B2091` |
| 设备 / 已记录固件 | 华为 E8372h-155 / `21.335.03.02.233` |
| 构建环境 | arm64、Apple clang 16.0.0、Command Line Tools 提供的 macOS SDK |
| 源码基线 | 本地导入 `1c20fdd`，上游 `f4f0aac8d33a152cf60db53cecd555daa1d6dae1` |
| 已验证实现 | `791c5ce` |
| 远程连接 | NoMachine 通过原 `en0` 路径连接；记录中的 USB 检查期间 Wi-Fi、VPN 保持启用 |

本次 GitHub 准备仅修改文档，源码和脚本保持为 `791c5ce` 的实现。设备/固件信息沿用已有项目记录；本次文档整理未查询或修改设备固件。

## 保留的硬件证据：2026-10-25

原始文件保存在本机被 Git 忽略的 `artifacts/` 中，不随仓库分发。下列文件名用于说明摘要依据；克隆仓库不会获得这些文件。

| 证据 | 观察结果 | 能证明的范围 |
| --- | --- | --- |
| `before-switch.json`，16:53:51 | `12d1:1f01`、`state=cdrom`，无驱动/接口/IPv4 | 设备实际初始状态 |
| `manual-switch-2.log`，16:55:55–16:56:00 | 直接 DiskArbitration 卸载被拒绝；同一目标的 `diskutil unmountDisk` 成功；发送一次请求；经过 `absent`、`waiting_address`，最终 `address_ready` | 一次真实硬件切换成功，包含权限回退路径 |
| `after-switch-2.json`，16:56:00 | `12d1:14db`、`AppleUserECM`、`en9`、可用 IPv4 | 已观察到网络模式、ECM 驱动及接口/地址 |
| `stability-10min.jsonl`，16:59:21–17:09:21 | 11 轮 JSONL；均为 `address_ready`；直接 USB DNS 和 HTTPS 成功，HTTPS 均为 `HTTP/1.1 200 OK`；对端路由持续为 `en0` | 约 10 分钟内重复的 USB 绑定通信成功 |
| `before-switch.json` 与 17:10:35 的 `final-network.json` | 原网络服务对象不变；原相对顺序保留；新增一个 `HUAWEI_MOBILE` 服务；原 NoMachine 连接记录行不变 | 两次快照之间已记录的原配置和会话保持 |
| 同一组前后快照 | NoMachine 对端路由仍为 `en0`，默认路由仍为 `utun4` | USB 检查成功不代表默认应用流量已改走 USB |

稳定性记录明确写有 `isolation_test=pending_wifi_and_vpn_remain_enabled`。这些记录证明其他网络保持启用时，绑定 USB 接口的请求可用；不能据此认定 USB 独立隔离、吞吐量或长期可靠性已经验证。

## 此前项目记录

实现提交 `791c5ce` 中的旧 README 还记录了：

- 管理页 `GET` 返回 HTTP 200；自动脚本使用 `HEAD`，该端点可能返回 405。
- 当前用户的 LaunchAgent 已安装，并观察到任务空闲、退出码为 0、事件监听有效。
- 安装、卸载、重新安装完成；移除期间 USB 网卡保持就绪，最终恢复了自动任务。
- Wi-Fi 和 Shadowrocket 保持启用。

这些是此前报告的结果，不是本次 GitHub 准备时重做的操作。后台任务已安装且空闲，不能单独证明未来插入、登录或唤醒事件一定正常。

## 本次 GitHub 准备重新完成的检查：2026-10-25

| 检查 | 结果 | 范围 |
| --- | --- | --- |
| `sh test.sh` | 通过 | 重建、策略测试程序、非法参数退出码、Shell 语法、plist 语法、Python 编译 |
| 启用 `-Wall -Wextra -Werror` 的 arm64 构建 | 通过 | 使用本机 macOS SDK 的原生编译 |
| `codesign --verify --strict --verbose=2 build/hilink-switch` | 通过 | 本地临时签名有效，不代表 Developer ID 签名或公证 |
| `sh status.sh` | 退出 0，`address_ready`、`AppleUserECM` | 实时只读设备/接口/地址查询，不包含外网测试 |
| 保留 JSON/JSONL 核对 | 确认上述观察结果 | 历史证据复核，不是新一轮硬件试验 |

`tests/test_policy.c` 覆盖状态判定（含设备消失和候选歧义）、零地址/链路本地 IPv4 拒绝、只读卷能否强制卸载的策略，以及锁竞争和重新获取。传入强制卸载策略的超时/权限条件为构造输入，不是 USB 阻塞或真实卸载失败的端到端试验。测试入口还确认非法命令行参数返回 2。

本次文档准备未执行手动切换、安装/卸载、网络服务调整、Wi-Fi/VPN 调整、固件操作或网络探测。

## 待完成的验收

| 测试 | 状态 | 建议证据 |
| --- | --- | --- |
| 三次物理拔插 | 未完成 | 每次的状态/日志过程及绑定 USB 的通信结果 |
| 退出后重新登录 | 未完成 | 登录后的任务加载/监听和设备状态 |
| 睡眠唤醒 | 未完成 | 网卡/地址恢复及新的通信检查 |
| 关闭 Mac Wi-Fi 和 VPN 后 USB 独立隔离 | 未完成 | 事先准备恢复访问，验证真实应用路由和 USB 通信 |
| 长时间运行或吞吐量 | 未完成 | 明确时长和负载，记录失败、延迟和连接保持情况 |
| 其他 Mac/系统/设备/固件组合 | 未验证 | 每组环境分别保留证据 |
| Intel 构建 | 当前脚本不提供 | 需要另行适配并验证 |

可能中断远程访问的测试应在具备恢复路径时执行。诊断保护条件必须符合真实部署；不要为了通过检查，将其他电脑改成示例 IP。

## 重复执行不改变设备/网络配置的检查

在原始 Mac 的项目目录运行：

```sh
sh test.sh
codesign --verify --strict --verbose=2 build/hilink-switch
sh status.sh
```

前两条命令不要求设备就绪；第三条报告实际就绪状态，可能返回文档说明的非零状态码。硬件和网络试验请按[使用指南](../README.zh-CN.md)操作，先核对部署保护条件，并将新的原始证据保存在本机。不能用策略测试代替物理验收。

[English](VALIDATION.md) | **简体中文** · [使用指南](../README.zh-CN.md)
