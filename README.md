# 华为 E8372h-155 USB 联网工具（M4 Mac mini）

将华为随身 WiFi 从安装光盘模式切换为 macOS 自带 ECM 驱动可识别的 USB 网卡。工具不修改网络服务顺序、IP、路由、DNS、Wi-Fi、VPN 或设备固件。

目标环境：M4 Mac mini、macOS 15.1.1、E8372h-155，固件 `21.335.03.02.233`。首版只主动切换 `12d1:1f01`；这台设备实测切换后为 `12d1:14db`。其他型号没有验证。

## 本机验证状态（2026-10-25）

| 项目 | 结果 |
|---|---|
| 原生 arm64 构建、代码签名及关键判定测试 | 通过 |
| USB 手动切换 | 通过；AppleUserECM 驱动，新增 en9，地址 192.168.8.170 |
| 管理页 | 绑定 en9 的 GET 请求返回 HTTP 200 |
| USB DNS 与 HTTPS | 通过；套接字绑定 en9，向设备 DNS 查询，并直接连接结果 IP，TLS 校验通过，Apple 网站返回 200 |
| NoMachine | 切换前后同一会话保持；192.168.0.34:4000 ↔ 192.168.0.69，路由仍经 en0 |
| 原网络服务 | 现有配置及相对顺序未变；系统新增 HUAWEI_MOBILE DHCP 服务 |
| Wi-Fi / Shadowrocket | 保持启用；默认路由仍为 utun4 |
| 10 分钟稳定性 | 通过；16:59–17:09，11 轮 DNS/HTTPS 全部成功，NoMachine 对端路由始终为 en0 |
| LaunchAgent 自动任务 | 已按用户确认启用；当前用户事件监听已注册，首次检查正常退出，退出码 0，无循环重启 |
| 安装/卸载/重新安装 | 实测通过；卸载期间 USB 网卡保持就绪，最终已恢复自动任务 |
| 三次物理插拔、重新登录、睡眠唤醒 | 待用户配合验证 |
| 关闭 Wi-Fi / 暂停 VPN 的独立隔离测试 | 未执行，遵守当前远程连接约束 |

**绑定 USB 的请求已通过，不代表默认的全部应用流量已改走 USB，也不代替关闭 Wi-Fi/VPN 后的隔离验收。** `--status` 中 `internet=not_tested` 表示该只读命令不发送网络请求；外网结果由 `check.py` 单独给出。

IP 和 `en9` 是这次实测结果，不写死在切换程序中。每次运行会重新沿 USB 设备树识别接口与实际地址。

## 构建、检查与手动切换

在本项目目录运行：

```sh
sh build.sh
sh test.sh
sh status.sh
./build/hilink-switch --switch
```

- `--status` 或无参数：只读检查。
- `--switch`：只切换一次；最多等待 60 秒，已经就绪则不发送请求。
- `--launchd`：供后台任务调用，不需要手动使用。
- 程序为本机编译的 arm64 可执行文件，采用临时代码签名，无需华为旧安装器。

状态依次可能为 `cdrom`、`absent`、`waiting_driver`、`waiting_interface`、`waiting_address`、`address_ready`。短暂的 `absent` 只是重新枚举过程，不是成功标志。

退出码：0=地址与链路就绪；1=系统查询或准备失败；2=参数错误；3=未发现设备；4=多个候选设备/接口；5=尚未就绪、超时或不支持；75=另一切换实例运行中或无法取得锁。后台在登录时未发现设备会正常退出。

程序只卸载目标华为设备所属的卷。可写卷卸载失败即停止。只读光盘只有“忙”错误才允许强制卸载。本机直接调用 DiskArbitration 返回 `kDAReturnNotPrivileged`，因此在该错误下调用系统 `diskutil unmountDisk` 正常卸载同一目标磁盘；不使用 sudo、不调整权限。

## 网络证据与远程连接保护

```sh
python3 snapshot.py artifacts/before.json
python3 check.py --output artifacts/check.jsonl
python3 check.py --duration 600 --output artifacts/stability.jsonl
python3 snapshot.py artifacts/after.json
```

文件名必须尚不存在，避免覆盖原始证据。原始快照和运行记录位于 Git 忽略的 `artifacts/`，文件权限为 600；不要对外发布含本地网络信息的原始快照。

`snapshot.py` 记录配置、服务顺序、路由、DNS、NoMachine 连接及 USB 状态，不会恢复或修改任何设置。受限执行环境中若系统拒绝路由查询，应在已授权的普通本机终端运行，不能把查询失败当成检查通过。

`check.py` 针对本项目的远程使用场景检查：

1. 到 NoMachine 对端 192.168.0.69 的路由仍指向 en0，en0 仍为 192.168.0.34/24 且激活。
2. 通过 macOS `IP_BOUND_IF` 将每个测试套接字绑定到动态识别的 USB 接口，同时绑定该接口的 IPv4 地址。
3. 向 DHCP 下发的路由器访问 HTTP；部分华为管理页不支持 HEAD，会返回 405，这表示链路可达，管理页 GET 已另行实测为 200。
4. 向 DHCP 下发的 DNS 服务器发送 A 查询，直接使用其结果完成 HTTPS，避免系统代理 DNS 的 Fake IP。优先检查 Apple 中国网站，失败时尝试百度。
5. 记录接口、本地/远端地址、DNS 与 HTTPS 结果；不使用环境代理，TLS 证书正常校验。

持续检查每分钟一次，不做测速或大文件下载。任何一轮失败即以非零状态退出，并保留此前记录。`check.py` 的固定 NoMachine 保护地址只适用于本项目当前网络；换用其他网络时先核对保护条件，不直接删掉检查。

## 安装当前用户的自动切换

仅在手动 USB 检查通过，并明确接受仍待完成的隔离验收项后执行：

```sh
sh install.sh
```

安装器会重新构建、确认地址就绪并运行一次 USB 通信检查；失败时不注册后台任务。请以当前登录用户运行，不要使用 sudo。

安装内容：

- 程序：`~/Library/Application Support/HiLinkSwitch/hilink-switch`
- 用户启动配置：`~/Library/LaunchAgents/local.hilink-switch.plist`
- 日志：`~/Library/Logs/hilink-switch.log`

LaunchAgent 仅监听 `12d1:1f01` 的插入事件，并在用户登录时检查一次。任务完成后退出，没有持续轮询或 KeepAlive 循环。只有当前用户已登录时工作；后台运行权限若被用户关闭，自动触发会停止。

```sh
launchctl print "gui/$(id -u)/local.hilink-switch"
tail -n 40 "$HOME/Library/Logs/hilink-switch.log"
```

该任务按事件运行，因此 `state = not running`、`last exit code = 0`、`watching = 1` 表示检查已正常结束且在监听下一次插入，并非故障。

安装与后台任务均不调整网络优先级。若以后需要 USB 优先于 Wi-Fi，应在具备恢复条件的窗口仅调整新增华为服务，保持 en0 和其他原有服务的配置与相对顺序；本项目脚本不会自动执行这一步。

## 卸载与异常处理

```sh
sh uninstall.sh
```

卸载器检查安装归属后停止用户任务，只移除本项目的程序、标记和启动配置，保留源码、日志及系统自动创建的网络服务。因为本工具没有主动修改网络配置，无需恢复 en0、Wi-Fi 或 VPN 设置。设备已经处于网络模式时可继续使用，重新插拔后不再自动切换。

- 停在光盘模式：查看错误；可关闭正在访问 HiLink 的程序，再手动重试。
- 卷卸载失败：停止切换，不强制卸载可写存储卡。
- 新设备没有网卡：记录 USB 描述符和驱动匹配信息，不安装旧 kext 或改变系统安全设置。
- 网卡有但无地址：检查 HUAWEI_MOBILE 服务的 DHCP 状态；程序不会自行写入静态 IP。
- 外网检查失败：区分移动信号、DNS、目标站点和 VPN 路由。单个公网 IP 不可达不能证明 USB 不可用；本次 1.1.1.1:443 未连通，而 USB 上的域名解析与 Apple HTTPS 已通过。
- NoMachine 保护检查失败：停止后续网络调整，优先保留原有连接。

未完成的物理插拔、隔离、登录和唤醒验收，需要在用户能恢复现场连接时安排。不得将模拟状态测试写成物理测试通过。

## 来源与许可

源自 [function0xMarki/Huawei_HiLinkSwitch_4G-Routers](https://github.com/function0xMarki/Huawei_HiLinkSwitch_4G-Routers)，固定提交 `f4f0aac8d33a152cf60db53cecd555daa1d6dae1`，保留 MIT 许可证。原始文件的 Git blob 哈希已逐项核对；本仓库基线提交为 `1c20fdd`。

本地适配主要增加状态分层、实际 ECM/网卡/IP 判定、当前设备范围与互斥、卸载兼容处理，以及远程连接保护下的检查与安装流程。上游在 Intel Mac 的结果不作为本机验证依据。
