# SoftAP 配网 httpd 任务创建失败（ESP_ERR_HTTPD_TASK）诊断与修复

## 元信息

- 状态：**已完成（2026-09-25 22:24，按配网故障范围收口）**。单缓冲固件连续两次启动 HTTPD 成功，显式停止 AP DHCP 的固件连续两次完成热点 DHCP、网页扫描、目标网络连接、STA IPv4、凭据保存及会话关闭；第二会话“寻找 IP”故障未复现。项目负责人确认以这两轮日志收尾。重启后自动连接、互联网可达、显示刷新与更多轮同条件内存回归未纳入本次通过项；旧 UDP PCB 遗留机制没有直接观测证据。
- 创建时间：2026-09-25 11:20（北京时间）
- 发现场景：节点 15 CP7 merged bin 烧录轮次（收口后遗留的范围外缺陷，不追溯节点 15 结论）
- 关联：`goals/20260922-1711-node15-integration-validation.md`（CP7 记录）、节点 10 `goals/20260920-2210-wifi-service.md`（配网功能原始验收）

## 现象

- merged bin 单文件烧录后（凭据被整段擦除属预期代价，`wifi_svc: service ready, auto_connect=1, has_credentials=0`），在 Settings 发起配网：SoftAP、DHCP、DNS responder 全部正常起来，唯独 `httpd_start` 返回 `ESP_ERR_HTTPD_TASK (0xb008)`。
- 冷启动后 18 s、46 s、182 s 三次复现，均在打开 Settings 发起配网时失败；非会话残留、非长时间泄漏（失败前后 free/largest 无单调下降）。

## 诊断证据（人工串口日志，2026-09-25）

```
wifi_prov: internal heap before httpd_start: free=26123 largest=18432
wifi_prov: httpd_start failed: ESP_ERR_HTTPD_TASK (0xb008), internal heap: free=26163 largest=18432
```

- 该日志来自本次新增的两行诊断（`start_http_server` 前后打印 `heap_caps_get_free_size/get_largest_free_block(MALLOC_CAP_INTERNAL)`，纯观测、不改行为）。

## 根因

- IDF 6.1 的 `httpd_os_thread_create`（`components/esp_http_server/src/port/esp32/osal.h:30`）走 `xTaskCreatePinnedToCoreWithCaps`；其实现（`components/freertos/esp_additions/idf_additions.c:50`）把 `stack_size` 按「字」解释：`heap_caps_malloc(stack_size * sizeof(StackType_t), INTERNAL|8BIT)`。
- 原配置 `config.stack_size = 5120` → 实际需要 **20,480 字节连续内部 RAM**；当时最大连续块只有 18,432，分配失败即 `ESP_ERR_HTTPD_TASK`。
- 节点 10 验收时同样的 20 KB 需求可满足；节点 11（Agent 任务栈）、14、15 陆续新增内部 RAM 消费者后，开机后（且已切到 sta+SoftAP 双模式、AP 缓冲占用内部 RAM）的连续块跌到 18 KB 档，属累积性内存压力回归，非单点引入。

## 修复

- `main/services/xiaomiao_wifi_provisioning.c`：`config.stack_size` 5120 → **3584**（×4 = 14,336 B 连续内部 RAM，可在 18 KB 洞内放下；14 KB 对单手机配网 handler 足够，IDF 默认 4096 也才等效 16 KB）。附注释说明 WithCaps 的乘 4 语义与本次实机数据。
- 保留两行 heap 诊断日志（成功路径各一次 LOGI，成本可忽略），便于后续回归对照。

## 验收（人工）

```powershell
idf.py build
idf.py -p COM5 flash
idf.py -p COM5 monitor
```

1. 进 Settings 开启配网：应出现 `internal heap before httpd_start: free=... largest=...` 且**不再有** `httpd_start failed`；手机可连 `Xiaomiao-XXXX` 热点并打开配网页。
2. 重新配网站点，确认凭据持久化、自动重连恢复正常（本轮预期代价的闭环）。
3. 若仍失败，把两行 heap 数字发回：说明连续块已跌破 14 KB，需要转向「配网期间释放内部 RAM」（如暂停 Agent 轮询）的方案，而非继续压栈。

## 第三层：四次握手失败（reason=15）判定实验

会话本身健康（httpd/DHCP/DNS 正常、heap 充足）、密码正确、AID 已分配后 4～6 s 超时——只剩「握手报文在空口/驱动层丢失」与「手机侧拒用当前凭据」两类可能。原按成本从低到高安排：

1. **换客户端**：用另一台手机或 Windows 笔记本连 `Xiaomiao-61AD`＋屏幕密码。若同样 reason=15，排除手机侧缓存/兼容因素，坐实设备侧空口问题。
2. **确认当前密码**：进入 Settings 配网页时抄下屏幕 8 位密码，与日志 `session ready` 时间点对应（同一会话内屏幕值即生效值，代码路径已复核一致）。
3. **观察规律**：记录 join→leave 的间隔是否恒为 4～6 s。恒定超时=EAPOL 丢失；即刻被踢=凭据/能力协商问题。

若 1 也失败，下一轮改动方向：把 `esp_wifi_set_bandwidth` 的返回值打日志（先证明 BW20 是否生效），并试验在配网会话期间强制 `WIFI_MODE_AP`（暂时关闭 STA 接口，消除 AP/STA 单射频调度的握手干扰窗口）——该改动会让配网页扫描结果不可用（STA 停），故仅在实验版验证。**实验 1 已被第四层的负责人对照观察取消，不再执行。**

## 第四层：owner 对照观察推翻「外部因素」，改判 RF 校准（2026-09-25 15:50）

负责人质疑成立：**merged bin 烧录之前联网与配网都是好的**，因此第三层的换手机实验取消，改按「本次刷机的副作用」定位。

- 机制：`idf.py merge-bin` 生成的 raw 镜像覆盖 `0x0～0x39FFFF`，其中包含 `0x9000` 的 NVS 分区。NVS 里除了凭据还有 **PHY RF 校准数据**（`CONFIG_ESP_PHY_CALIBRATION_AND_DATA_STORAGE=y`，默认值，实测于 `build/config/sdkconfig.h`）。整段擦写把两者一起清掉。
- 本机没有外部 32k 晶振（`CONFIG_RTC_CLK_SRC_INT_RC=1`，实测），擦除后的重新校准质量本就不稳定；校准偏差落在「802.11 关联能过（`join, AID=1`）、EAPOL 四次握手收不到 → 4～6 s 超时 reason=15」这个区间，与全部实机现象吻合，也解释了为什么代码路径（节点 10 已验收、本次未改）没变却突然不通。
- 静态证据：`sdkconfig.defaults` 从未声明 PHY 校准项（一直吃默认值）；`build/*.bin` 时间戳显示 merged 镜像（14:13）与当前 app（15:38）不是同一轮产物，但 reason=15 与镜像内容无关，故不作为主线。
- 实验（唯一改动：`sdkconfig.defaults` 增加 `CONFIG_ESP_PHY_RF_CAL_FULL=y`，每次开机做完整 RF 校准，注释标注 EXPERIMENT ONLY）：
  ```powershell
  idf.py build
  idf.py -p COM5 flash      # 只擦 app 分区，不再整段擦 NVS
  idf.py -p COM5 monitor
  ```
  预期：Settings 开配网出现 `session ready`，手机连屏幕密码**不再 reason=15**，能进配网页。同轮顺带采集 `largest8bit` 三行（二层泄漏诊断已在这份源码里）：会话开始两行＋`session resources released, internal heap now: ...`。
- 判定分支：若完整校准后配网恢复 → 根因为「merge-bin@0x0 擦掉 PHY 校准＋无 32k 晶振」，处置方向回到发布形态（merged 镜像不覆盖 NVS／保留 `CONFIG_ESP_PHY_RF_CAL_FULL`／或规定擦 NVS 后需重启一次再配网），与本 Goal 二层泄漏问题分开收口；若仍 reason=15 → 回到第三层备选方向（BW20 返回值日志＋会话期临时 `WIFI_MODE_AP`）。

### 第四层实验第一轮结果（16:35，实验未真正执行，另含两条推翻性数据）

人工日志（ELF 为本轮 app-only 重烧）关键行：

```
I (1147) phy_init: Saving new calibration data due to checksum failure or outdated calibration data, mode(0)
I (30571) wifi_prov: internal heap before httpd_start: free=26123 largest=18432 largest8bit=6400
I (30601) wifi_prov: session ready, ssid='Xiaomiao-61AD'
I (54956) wifi:station: fe:4b:43:92:2b:c6 join, AID=1, bgn, 20
I (59014) wifi:station: fe:4b:43:92:2b:c6 leave, AID = 1, reason = 15
```

1. **校准实验没跑成（我的失误）**：`mode(0)` 说明仍是 PARTIAL。`sdkconfig.defaults` 只在本地 `sdkconfig` 不存在时播种；仓库根目录的 `sdkconfig`（git 忽略，内含 Agent 地址 `192.168.1.31`）早已写入 `CONFIG_ESP_PHY_RF_CAL_PARTIAL=y / RF_CAL_FULL is not set / CALIBRATION_MODE=0`，我在 defaults 增的那行被完全忽略。已直接把本地 `sdkconfig` 改为 `CONFIG_ESP_PHY_RF_CAL_FULL=y`＋`CONFIG_ESP_PHY_CALIBRATION_MODE=2`（defaults 保留同一条并补注释说明播种规则）。
2. **`bgn, 20` ⇒ BW20 本轮确实生效**（上一轮是 `40U`），但手机仍在 join 后 4.06 s `reason=15`。第三层保留疑点①（带宽强制未生效）就此排除，不再是调查方向。
3. **`largest8bit=6400` 而 `httpd_start` 成功 ⇒ 第二层「`INTERNAL|8BIT` largest 才是闸门」的推断被否定**：按该闸门，3584×4=14,336 B 必然分配失败，与实机 `session ready` 矛盾。二层的数字账（首会话成功／第二会话 `free=22291 largest=18432` 失败）目前没有任何 heap 指标能自洽解释，需要改用「失败时打印 IDF 自己的分配失败原因」或 `heap_caps_print_heap_info` 全量导出重新取证，不再靠单点最大块推断。
4. 新线索：`Saving new calibration data due to checksum failure or outdated calibration data` 在**每次开机**都出现，说明 NVS 里的校准存档每次都被判为无效并重写——与「校准质量不稳」假设同向，也值得单独记一笔（若 NVS 写入本身有问题，会同时影响凭据与 Settings 持久化）。

下一步（仍是同一实验，这次确保配置生效）：`idf.py -p COM5 flash monitor`，看开机行是否变成 `mode(2)`，再看手机连接是否仍 `reason=15`。

## 第五层：无凭据时驱动自发 STA 连接尝试（2026-09-25 17:10 提出，**18:05 实机否定**，见下一节）

第四层的校准假设被第二轮实机否定：本轮 `phy_init` 不再打印 `Saving new calibration data ... mode(0)`（配置已生效），手机仍然 `join, AID=1, bgn, 20` → 恰好在 4.06 s 后 `leave, reason = 15`。负责人再次强调「老问题，就是连不上」，据此把方向从射频转向**会话期间的射频争用**，并在日志时间轴上找到证据：

```
W (19131) wifi:Haven't to connect to a suitable AP now!
I (19132) wifi:mode : sta (ac:67:b2:45:61:ac) + softAP (ac:67:b2:45:61:ad)
```

两行同一毫秒——切 APSTA 的瞬间，Wi-Fi 驱动自己发起了一次 STA 连接尝试。机制链条（源码可核对）：

- `main/services/xiaomiao_wifi_service.c:442-461` `station_apply_config()`：无凭据（或传 NULL）分支把 STA 配成**空 SSID + `threshold.authmode = WIFI_AUTH_OPEN`**，在驱动语义里就是「加入任意开放热点」；初始化路径 `:1191` 在 `esp_wifi_start()` 之前就把这份配置推进了驱动。
- 配网会话把模式切到 APSTA 会重启 STA 接口，驱动随即按上面这份配置开始自己的连接尝试（`Haven't to connect to a suitable AP now!`）。ESP32 单射频，APSTA 下 STA 的扫描/连接尝试会把射频从 AP 信道上拽走，手机四次握手的 EAPOL 帧正好丢在窗口里 ⇒ 固定 ~4 s 超时、reason=15，与输入的密码无关。
- 为什么「刷 merged bin 之前是好的」：`station_apply_config` 的空配置分支自 `28ffac3`（节点 10）就存在，但节点 10 验收配网时机器里**有**凭据——STA 会连回路由器并稳定在信道上，AP 跟随它，不跳频。merged bin 擦掉 NVS 之后，这是项目第一次在「NVS 全空」状态下开配网，此分支才被触发。不是射频退化，是**从未被实机走过的代码路径**。
- 与第三层已排除的两条（带宽、缓存密码）不冲突；也与本 Goal 一/二层（httpd 栈与泄漏）无关。

修复（`main/services/xiaomiao_wifi_provisioning.c:643-663`）：在 `esp_wifi_set_mode(WIFI_MODE_APSTA)` 成功后立刻 `esp_wifi_disconnect()`，取消驱动自发的这次尝试，让整个会话期间射频留在 AP 信道上；返回值非 OK 时打一行 INFO（说明当时没有在飞的尝试），便于实机区分。会话结束后仍由 Service 负责恢复保存的网络，所以这里不需要 STA 做任何事。

验收（人工，一次烧录同时回收多层数据）：

```powershell
idf.py -p COM5 flash monitor
```

1. Settings 开配网，手机连屏幕密码：**应能保持关联并打开配网页**（不再出现 join 后约 4 s 的 `leave, reason = 15`）。
2. 若仍 reason=15，看日志里 `Haven't to connect to a suitable AP now!` 是否再次出现在会话中段（说明单次取消不够，需要会话期持续压制 STA），以及新加的 `no station attempt to cancel: ...` 行返回值。
3. 若能连通，继续走配网提交，验证凭据持久化与自动重连（一层验收步骤 2 的闭环）。
4. 顺带采集 `internal heap ... largest8bit=...` 与 `session resources released, internal heap now: ...` 行（二层泄漏的新取证数据）。

## 第五层被否定，改为向驱动取证（2026-09-25 18:05）

第三轮实机（负责人：「配网还是失败」）：`esp_wifi_disconnect()` 已生效（我们新增的 `no station attempt to cancel` INFO **没有打印**，说明返回 `ESP_OK`，即当时确有尝试在飞并被取消），但手机仍然 `join, AID=1, bgn, 20`（30389）→ `leave, reason = 15`（36426，间隔 6.04 s，比上一轮 4.06 s 更长）。`Haven't to connect to a suitable AP now!` 仍在 `mode : sta + softAP` 前一行（12710/12711）。结论：**STA 自发连接尝试不是 reason=15 的原因**，第五层假设否定。代码保留该次取消，理由改成「无凭据时会话期间不该让机器加入陌生开放热点」（安全性，与本次故障无关），注释已按实机证据改写。

同轮排除：密码长度不是变量（`XIAOMIAO_WIFI_AP_PASSWORD_BUF=9` ⇒ 恰好 8 位数字，`xiaomiao_wifi_service.h:101`；UI 与 `esp_wifi_set_config` 读同一 `s_ap_password`）。至此「栈/带宽/校准/STA 抢射频/密码长度」全部被实机否定，四层推断连续失败，说明**不能再靠现象外推**。

取证改造（`xiaomiao_wifi_provisioning.c`，标注临时诊断）：

1. AP 配置写入后读回并打印真正生效的参数：`AP in effect: authmode=... channel=... ssid_len=... pwd_len=... max_conn=... pmf_required=...`（只打长度，不打密码）。
2. 会话期间把 `wifi` 与 `wpa` 两个标签提到 `ESP_LOG_DEBUG`（提到会话入口处、切 APSTA 之前，好让模式切换本身也被记录；会话停止与启动失败回滚路径都恢复 `INFO`），让握手自己说明四次握手：能否看到手机 EAPOL M2 抵达、是否 MIC 校验失败重发 M3、还是根本没收到帧。这两种形态分别指向「凭据/MIC」与「空口丢失」，是本轮唯一能分开它们的观测。

DEBUG 日志有两层编译期前提。第一层：本机 `CONFIG_LOG_VERSION_1=y`、`CONFIG_LOG_DYNAMIC_LEVEL_CONTROL=y`，`esp_log_level.h:57` 令 `LOG_LOCAL_LEVEL = CONFIG_LOG_MAXIMUM_LEVEL`、`ESP_LOG_ENABLED` 按它做编译期裁剪；本地 `sdkconfig` 已置 `CONFIG_LOG_MAXIMUM_LEVEL_DEBUG=y` + `CONFIG_LOG_MAXIMUM_LEVEL=4`（默认运行级仍为 INFO）。第二层：`wpa_debug.h:52/152` 在没有 `DEBUG_PRINT` 时把 `wpa_printf` 整个定义为空；`supplicant_opt.h:12` 仅在 `CONFIG_ESP_WIFI_DEBUG_PRINT=y` 时定义 `DEBUG_PRINT`。此前漏掉第二层，本轮人工日志只有 `wifi` DEBUG 而没有 `wpa` 行，不能据此推断 M2 未到。曾直接编辑被忽略的本地 `sdkconfig` 为 `CONFIG_ESP_WIFI_DEBUG_PRINT=y`，但人工构建时 CMake 重新配置后该项又变回关闭（`sdkconfig` 和 `build/config/sdkconfig.json` 均为 false），所以本轮仍未取到握手日志。下一次由人工通过 `idf.py menuconfig` 设置，并在构建后检查生成配置；不把该诊断开关写进 `sdkconfig.defaults`。该开关可能输出密钥十六进制数据，回传时只摘取握手状态文字行，不发送原始完整 DEBUG 日志。

`wpa` 标签为什么值得单列：IDF 6.1 的 AP 侧四次握手不在 prebuilt 库里，`components/wpa_supplicant/CMakeLists.txt` 用源码编译 `src/ap/wpa_auth.c`、`src/eapol_auth/eapol_auth_sm.c`，其 `wpa_printf(MSG_DEBUG, ...)` 经 `include/utils/wpa_debug.h:24` 统一映射到标签 `"wpa"`。关键行：`wpa_auth.c:806 "WPA: Received EAPOL-Key from <MAC>"`（M2 抵达）、`wpa_auth.c:1221 "WPA: Send EAPOL(version=.. secure=.. mic=.. ...)"`（M1/M3 发送）、`wpa_auth.c:1913 "invalid MIC in msg 2/4 of 4-Way Handshake"`。最后那行是 **INFO 级**，但此前 `CONFIG_ESP_WIFI_DEBUG_PRINT` 关闭时同样被编译掉；打开该开关后的握手状态行才能用于判断 M2 是否到达及 MIC 是否失败。prebuilt `libnet80211.a` 里也确实存在 `... M3 MIC failed` 一类字符串，驱动侧 DEBUG 可用。

此前计划采集握手状态行；最新实机已显示关联响应发送失败，先查此阶段。不要回传完整 WPA DEBUG 日志；它会输出本次热点口令及密钥材料。

同时保留一个**不需要重新烧录、现在就能做**的对照：拿一台从未配过 `Xiaomiao-61AD` 的设备（笔记本最方便）连一次屏幕密码。它不是「怀疑你手机坏」，而是这台手机对该 SSID 存过历史凭据，而在设备侧连续四层排除之后，「M2 携带的 PMK 与屏幕密码不一致」与「M2 根本没到」在现有日志里形态相同；AP 侧 reason=15 对这两种情况的处理就是重发 M3 直到超时——本轮 6.04 s 与上轮 4.06 s 的差异恰好说明发生了若干次重发。对照组能把这两类一次分开。

第四轮取证验收（人工）：以下四条命令**逐条执行**，每条结束并回到 PowerShell 提示符后再输入下一条。

1. 执行 `idf.py menuconfig`，在 Component config → Wi-Fi 中启用并保存「Print debug messages from WPA Supplicant」。
2. 执行 `idf.py build`，等待编译结束。
3. 执行 `Select-String -Path build/config/sdkconfig.h -Pattern 'CONFIG_ESP_WIFI_DEBUG_PRINT'`，确认输出是 `#define CONFIG_ESP_WIFI_DEBUG_PRINT 1`；没有这一行就停止，不烧录。
4. 只在第 3 步通过后执行 `idf.py -p COM5 flash monitor`。

- 使用普通 `idf.py flash`：它会烧录本项目所需镜像，但不写 NVS；**不要**再从 `0x0` 烧录 merged bin 或整片刷写（那会再擦一次 NVS）。
- 首次编译会比上一轮慢（`CONFIG_LOG_MAXIMUM_LEVEL` 变了，等于全量重配），`build/` 会重新生成大量目标，属正常。
- Settings 开配网 → 手机连屏幕密码；只摘取不含密钥的关联状态文字行，不回传完整 DEBUG 日志或密钥十六进制行。
- 已由人工确认：`AP in effect: authmode=3 channel=1 ssid_len=13 pwd_len=8 max_conn=1 pmf_required=0`（`Xiaomiao-61AD` 实际为 13 字节），手机 join 后约 4 秒仍以 reason=15 离开。打开 `CONFIG_ESP_WIFI_DEBUG_PRINT` 后再看 `wpa` 握手状态文字行：若有 `Received EAPOL-Key`，继续判读 MIC／PMK；若仍没有，先核对该编译开关是否在本轮构建生效，再判断握手停在哪一步。

## 最新实机证据：关联响应发送失败（2026-09-25 19:18 复核）

人工提供的 WPA DEBUG 日志中，手机多次发起关联请求；驱动记录 `join success` 后立即出现 `wifi:m f assoc rsp l=122`、`wpa: esp_send_assoc_resp_failed: send failed`，随后客户端以 `reason=1` 离开。未见 EAPOL 四次握手开始。这一轮的失败点在 AP 发送关联响应，不能再按旧日志的 `reason=15` 归因为热点密码或四次握手错误。旧轮 `reason=15` 的具体机制仍待确认，也不能证明它与本轮必为同一原因。

对照本机 ESP-IDF 6.1 `components/wpa_supplicant/esp_supplicant/src/esp_hostap.c:433-451`，`reply` 的分配失败会打印另一条错误；本轮打印的 `send failed` 仅在 `esp_wifi_send_mgmt_frm_internal(reply)` 返回非零时出现。该函数在 `esp_wifi_driver.h:324` 仅有声明，当前安装的组件源码未见实现，暂时无法判定底层返回的错误码及 RF、队列或内存中的具体原因。

同轮 `httpd_start` 前 `largest8bit=6144`，之后为 496 字节；此前未打开 WPA DEBUG 的实机轮次也曾出现关联响应发送异常。内存压力是需要对照验证的线索，不是已证实的根因；WPA DEBUG 额外开销也不能单独解释此前的异常。下一次受控对照应保持同一设备、AP 配置和手机，只跳过 HTTP 服务并观察关联。该对照已由人工执行，结果见下节。完整 DEBUG 日志包含本次口令和密钥材料，不再传播；关闭并重开配网会话后临时口令会轮换。

### 单变量诊断固件（2026-09-25 19:23，人工验证完成）

本轮临时将 `main/services/xiaomiao_wifi_provisioning.c` 的 HTTP 服务跳过：保留现有 AP 模式、参数、DNS、定时器和 WPA DEBUG 配置。该固件**不能提供配网页，也不能完成 Wi-Fi 凭据提交**，只用于一次热点关联对照。人工结果到达后，该诊断开关与分支已从源码移除，HTTP 服务启动路径恢复。

人工验证结果：同一手机在跳过 HTTP 的诊断固件上完成 WPA 四次握手，出现 `WPA_PTK entering state PTKINITDONE`、`AP client associated, internal heap: free=28011 largest8bit=8192`，DHCP 分配 `192.168.4.2`，随后持续 DNS 请求；没有报告关联响应发送失败，手机显示已连接。不能上网或弹出配网页是 HTTP 服务缺席、热点本身不转发互联网的预期结果。这说明当前关联失败依赖于 HTTP 服务的存在或其启动带来的资源／时序变化，但尚未证明是内存本身导致。

### 恢复 HTTP 与显示内存对照（2026-09-25 21:32，配网主流程实机通过）

静态检查发现 `main/main.c` 当前开机为显示常驻两个 160×128 RGB565 全屏 DMA 缓冲区，每块 `40960` 字节；第三块已在实机因内存不足降级。为给 HTTP 与 Wi-Fi 留出空间，显示改为 LVGL 支持的单全屏缓冲模式，少申请一块 `40960` 字节的内部 DMA RAM；保留同一全屏渲染模式和异步 flush 回调。HTTP 服务启动恢复原路径，配网页应重新可用。该改动只是针对内存压力的可检验修复候选，尚未证明底层发送失败的具体机制；单缓冲可能降低刷新吞吐，需实机观察。

人工本轮执行 `idf.py -p COM5 flash monitor`，在首个配网会话用同一手机连热点并提交目标网络。原验收还包括检查 `1 full-screen DMA buffers`、`internal heap after httpd_start`、重启后 `has_credentials=1` 及显示刷新表现；本次回传片段未覆盖这些项目，继续列为待补证据。

人工本轮日志（21:40 提供）已显示配网页可用：扫描完成并列出 10 个网络，`trial connect started, ssid='CMCC-U2Tx'`；STA WPA 四次握手完成（`Key negotiation completed`），于 119656 ms 获得 `192.168.1.3`，119658 ms 打印 `credentials saved, provisioning can finish`，119970 ms 打印 `provisioning session closed after success`。这证明**恢复 HTTP 且少用一块显示 DMA 缓冲后，首会话配网主流程成功**。这组干预强烈支持内部 RAM 余量不足参与先前的关联响应失败，但用户未提供本轮 `largest8bit` 数值，驱动内部错误码仍未知，不能将具体底层机制写成已证实。断电／重启后凭据读回及自动连接、显示刷新表现、第二会话是否还发生约 3.8 KB 内存留存均未验证。

本轮关闭 AP 时仍见 `hostap_deinit: failed to post SIG_TASK_DEL, skipping WPA3 API lock wait`。IDF 6.1 的 `esp_hostap.c:223-231` 仅对 WPA3／兼容模式初始化 WPA3 任务，但 `esp_hostap.c:346-364` 在退出 AP 时仍尝试删除该任务；本轮 WPA2 热点日志也显示 `g_wpa3_hostap_auth_api_lock not found`。该错误由未启动的 WPA3 辅助任务导致，随后仍执行 `hostapd_cleanup()`，不影响本轮配网成功，也不能作为第二会话内存留存的证据。取证用 WPA 运行时日志级别已在配网入口恢复为 INFO；本地 `CONFIG_ESP_WIFI_DEBUG_PRINT` 保留，以便后续仍能看到 `hostap_deinit` 的 ERROR，但不再将 `wpa` 提到 DEBUG。**这一日志清理改动尚未人工重新构建／烧录**，当前板上固件仍可能打印密钥。

### 第二会话换网观察（2026-09-25 21:54，网页阶段待确认）

人工日志显示第一轮会话启动时 `httpd_start` 前内部 free=66183、后 free=60319，用户停止后 free=73975，并重新连回保存的 `CMCC-U2Tx`、取得 `192.168.1.3`。第二轮会话启动时 HTTPD 再次成功（前 free=63275、后 free=57279、`largest8bit=36864`），因此旧的「第二会话 `0xb008` 无法启动网页服务」在当前单缓冲固件上没有复现；两轮会话的时刻和 STA 状态不同，不能把 free 差值直接认定为泄漏量。

第二轮中同一手机两次 `join`，分别有 `AP client associated` 和 `largest8bit=36864`；约 18 秒后分别以 `reason=3` 离开。ESP-IDF 6.1 的 `esp_wifi_types_generic.h:123` 将 3 定义为 `WIFI_REASON_AUTH_LEAVE`（因离开而解除认证），该码本身不能确定断开的发起方。负责人补充：手机输入本次热点密码后显示连接一会儿，随后自动切到其他 Wi-Fi。热点 WPA 密码验证因此已通过，不能解释成目标网络密码错误。日志也没有第二轮 `DHCP server assigned IP`、`scan done`、`trial connect started`、目标网络关联或凭据提交；缺少 DHCP 分配日志可能是手机复用旧租约，也可能尚未拿到 IP，需看手机 Wi-Fi 详情。第二轮前后的 `HTTP_CLIENT` 超时／中断属于 PC Agent 请求，不能从这些行归因目标 Wi-Fi。下一步在手机关联热点期间确认是否拿到 `192.168.4.x` 地址、是否出现“无互联网仍保持连接”选项，并手动打开 `http://192.168.4.1/`；按 IP、页面、扫描、提交四层定位。此步骤无需改码或烧录。

### 第二会话停在获取 IP：AP DHCP 生命周期修复（2026-09-25 22:05，已实机验证）

负责人补充手机端直接观察：第二次连接 `Xiaomiao-61AD` 时一直显示“寻找 IP”，随后连接失败并切换到其他 Wi-Fi；手机界面没有可查看的 IP。这纠正了上一节「可能复用旧租约／无互联网而切网」的开放判断。结合第二会话日志两次 `AP client associated`、均无 `DHCP server assigned IP`，当前失败点是热点 DHCP 阶段，尚未进入配网页或目标网络试连。

静态复核本机 ESP-IDF 6.1：`esp_wifi_set_mode(WIFI_MODE_STA)` 触发的 `WIFI_EVENT_AP_STOP` 通过事件循环异步交给 `wifi_default_action_ap_stop()`；它仅在 AP netif 指针尚在时调用 `esp_netif_action_stop()`。原正常停止与启动失败回滚均在 `set_mode(STA)` 后立即 `esp_netif_destroy_default_wifi()`，该销毁先清除默认 Wi-Fi netif 指针；`esp_netif_destroy()` 的 lwIP 路径移除 netif 后调用 `dhcps_delete()`，对已启动 DHCP server 只标记 `DELETE_PENDING`，并不立即关闭 UDP/67。`dhcps_start()` 对 `udp_bind()` 的返回值也未检查，故下一会话出现“DHCP server started”日志不足以证明新服务真的能收到 DHCP 请求。该调用链能够解释第二会话找不到 IP，但尚无实机或抓包证据证明本机确实遗留了旧 UDP PCB。

修复：在两条销毁路径中先对仍存在的 AP netif 调用 `esp_netif_dhcps_stop()`，再切回 STA、销毁 AP netif；已停止状态视为正常，其他错误打印警告。IDF 的该函数在 lwIP 任务中同步执行，运行中的 `dhcps_stop()` 会释放 UDP PCB、租约与定时器。没有插入延时，也未改变 STA 凭据与重连流程。

原定人工验收为连续两次连接热点、获取 DHCP 地址、打开配网页、扫描并提交不同目标网络，同时比较 `session resources released` 的内存快照和检查新增 DHCP 停止警告。以下实机日志已覆盖这两轮配网；显示刷新另行验证。

### 实机回归：连续两次换网完成（2026-09-25 22:13）

负责人提供新固件串口日志，两轮均走完热点 DHCP、网页扫描、目标 STA 连接与凭据保存：

- 第一轮：手机 76242 ms 关联，76918 ms 获热点地址 `192.168.4.2`；84545 ms 扫描列出 10 个网络；试连 `CMCC-5pu4`，118468 ms STA 获 `192.168.1.3`，随后 `credentials saved`、`provisioning session closed after success`。
- 第二轮：手机 164477 ms 关联，164962 ms 再获热点地址 `192.168.4.2`；170716 ms 扫描列出 10 个网络；试连 `CMCC-U2Tx`，184984 ms STA 获 `192.168.1.3`，随后再次保存凭据并成功关闭会话。两次热点启动均有 `session ready`，无 `httpd_start failed` 或新增的 DHCP 停止警告。
- 两次会话停止后的内部 free 分别为 70619、73535 字节，第二次未比第一次更低；但两轮 STA、HTTP 请求等运行状态不同，不能据此证明没有内存留存。两轮 `largest8bit` 均为 36864 字节。

因此，**连续两次配网的实机验收通过**，此前第二次卡在“寻找 IP”的现象已消失。这证明修复后的行为符合预期；没有直接观测旧 UDP PCB，旧服务遗留仍是基于 IDF 调用链的机制推断。`/generate_204` 与 `/favicon.ico` 的 404 日志只表示手机探测端点或浏览器图标未提供，不妨碍本轮网页扫描与提交。`hostap_deinit` 的 WPA3 任务清理错误仍出现，但两轮配网成功，不能把该行解释为本次失败。目标 STA 获局域网 IP 不等于互联网连通；重启后的自动连接、显示刷新及更多轮内存回归仍待验证。

## 边界与未验证范围

- 本轮为释放内部 RAM 调整了显示缓冲数量，未改分区或配网协议；显示刷新性能待实机复验。
- 内部 RAM 基线压力变大的完整账单（哪些消费者各占多少）未审计，本修复只解配网路径的燃眉；若后续其他大栈任务（如恢复三缓冲）再遇同类失败，应立项做内部 RAM 预算审计。
- RF 校准、带宽及 STA 抢射频等旧假设已被前几轮实机对照否定；关联失败的底层错误码仍不可见。
- 配网连续两次换网已由人工日志验证；重启持久化、互联网连通、显示刷新与更多轮同条件内存回归仍待验证（项目规则：构建烧录由人工执行）。

## 收尾结论

2026-09-25 22:24，项目负责人确认直接按本次配网故障修复收尾。验收范围为连续两次完整配网、第二次热点 DHCP 获取 IP、目标 STA 获取 IPv4 和凭据保存；人工日志均已覆盖。`httpd_start` 旧失败和第二次“寻找 IP”在新固件上未复现。其余未验证项按上节保留，不作为这次配网修复的通过证据。

## 实施记录

- 2026-09-25 22:24 -- 项目负责人确认以连续两次完整配网日志直接收尾，本 Goal 状态改为已完成；重启后自动连接、互联网可达、显示刷新及更多轮内存回归保留为未验证范围。
- 2026-09-25 22:13 -- 负责人提供新固件两轮配网日志：两次手机均获 `192.168.4.2`，网页扫描、目标 STA IPv4 获取、凭据保存和配网关闭全部完成；第二轮“寻找 IP”故障未复现。HTTPD 两轮均启动，停止后 free 70619→73535 字节；旧 UDP PCB 遗留机制仍无直接证据，重启自动连接、互联网连通和显示表现待验证。
- 2026-09-25 22:05 -- 手机端补充“第二次连接始终寻找 IP”，将故障点定位到热点 DHCP 阶段；按本机 IDF 6.1 的异步 AP_STOP 与 DHCP 资源释放调用链，在正常停止及启动失败回滚时先显式停止 DHCP 再销毁 AP netif。源码及 IDF 静态核对完成；构建、烧录和第二会话实机验证待人工。
- 2026-09-25 11:20 -- 依据人工 heap 日志与 IDF 6.1 源码定位根因（WithCaps 栈按字×4 分配、20480 B > largest 18432 B），实施 `stack_size=3584` 修复并加两行诊断日志。状态：待人工复验。
- 2026-09-25 11:55 -- 人工复验：httpd 修复**实机通过**。重烧后 `internal heap before httpd_start: free=26119 largest=18432`（连续块仍不足 20 KB，旧值必失败），`session ready, ssid='Xiaomiao-61AD'`，全程无 `httpd_start failed`。手机两次 join 后 reason=15（四次握手密码错误）离开——为节点 10 已确认的既有行为（SSID 固定＋每次会话随机密码，手机缓存旧密码所致），需手机侧忘记该热点后用屏幕当前密码重连。验收步骤 1 通过；步骤 2（完成配网、凭据持久化、恢复网络）待用户下次会话操作确认。状态：修复验证通过，会话闭环待人工完成。（注：本条目中「手机缓存旧密码」的归因已被 13:30 条目实机证据否定，仅作历史记录保留。）
- 2026-09-25 12:40 -- 第二层问题暴露：首会话经 10 分钟空闲超时正常释放（`session resources released`）后，**第二次开启配网再次 `0xb008`**，日志 `free=22291 largest=18432`——与首会话启动时（`free=26119 largest=18432`）相比 free 净少 3828 B、plain largest 未变，说明 ① 一轮会话闭合泄漏约 3.8 KB 内部 RAM，② plain `INTERNAL` largest 不是真正的分配闸门（可能含 IRAM-only 块，httpd 栈需要 `INTERNAL|8BIT`）。诊断升级：`start_http_server` 前后与释放行均补打 `largest8bit`，量化泄漏并确认闸门。降栈 3584 只对开机首会话有效，泄漏根因待下一轮日志定位（嫌疑：会话资源释放路径中未回收的分配）。人工临时通道：断电重启、在首个配网会话内完成配网。状态：修复部分有效，泄漏排查进行中。
- 2026-09-25 13:30 -- 第三层问题独立成立：断电重启后在**首会话**（`free=26015 largest=18432`、`session ready`，httpd 正常）中，人工确认已输入**屏幕所示正确密码**，手机（`fe:4b:43:92:2b:c6`）仍两次 `join, AID=1` → 4～6 s 后 `leave, reason = 15`。此前「手机缓存旧密码」的解释（11:55 条目）被本轮证据否定，遂将其降级为待排除项。静态复核 AP 建立路径（`xiaomiao_wifi_provisioning.c:605-684`）结论：会话期间无 STA 扫描/重连竞争（`has_credentials=0` 时 Service 不发 `esp_wifi_connect`，`station_connect_now` 受 `s_saved.valid` 门控）；密码为 8 位纯数字（`generate_ap_password`），无特殊字符/长度问题；显示与应用取自同一 `s_ap_password`。疑点保留两处：② 第二次 join 报 `bgn, 40U`，与 `esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW20)`（:672，返回值被 `(void)` 吞掉）不符，说明 BW20 未真正生效或调用失败；③ `join→leave` 固定 4～6 s 且 AID 已分配，是 EAPOL 帧在空口上丢失的形态，而非密码错误的微秒级 Deauth。reason=15 与本 Goal 的栈/泄漏两层为**相互独立的缺陷**，需专列实验定位（见下节）。

- 2026-09-25 15:50 -- 负责人对照观察（刷 merged bin 之前联网正常）推翻「外部因素/手机侧」假设，第三层的换手机实验取消；改判第四层：merge-bin@0x0 连 `0x9000` NVS 一起擦掉，其中含 PHY RF 校准存档，本机 `CONFIG_RTC_CLK_SRC_INT_RC=1`（无外部 32k 晶振）⇒ 重新校准偏差导致 EAPOL 丢失、reason=15。唯一改动：`sdkconfig.defaults` 增 `CONFIG_ESP_PHY_RF_CAL_FULL=y`（实验用，注释标明）。本轮只烧 app 分区（`idf.py -p COM5 flash`），不再擦 NVS。状态：RF 校准实验待人工执行。
- 2026-09-25 16:35 -- 第一轮实验**未真正生效**：日志 `mode(0)` 证明仍是部分校准，根因是我改的是 `sdkconfig.defaults`，而它只在不存在的本地 `sdkconfig` 时播种；本地 `sdkconfig` 已有 `RF_CAL_PARTIAL=y`。已改本地 `sdkconfig`：`CONFIG_ESP_PHY_RF_CAL_FULL=y`＋`CONFIG_ESP_PHY_CALIBRATION_MODE=2`，并在 defaults 注释里写清播种规则。同轮两条推翻性结论：`bgn, 20` 表示 BW20 已生效但手机仍 4.06 s `reason=15`（带宽方向排除）；`largest8bit=6400` 却 `httpd_start` 成功（二层的 8BIT-largest 闸门推断被否定，取证方式要改）。新线索：每次开机都 `Saving new calibration data due to checksum failure or outdated calibration data`。状态：等第二轮同配置重跑给出校准结论。
- 2026-09-25 17:10 -- 第二轮实机（负责人：「还是一样老问题啊，就是连不上」）否定第四层：校准配置已生效（`phy_init` 不再打印 `Saving new calibration data ... mode(0)`），手机仍 `join, AID=1, bgn, 20` → 4.06 s `leave, reason = 15`。改判第五层：日志时间轴显示 `Haven't to connect to a suitable AP now!` 与 `mode : sta + softAP` 同毫秒，即切 APSTA 时驱动自发 STA 连接尝试；根因是 `xiaomiao_wifi_service.c:442-461` 无凭据分支把 STA 配成空 SSID＋OPEN 阈值（驱动读作「连任意开放热点」），单射频被反复拽离 AP 信道 ⇒ EAPOL 丢失。此分支自 `28ffac3` 存在但项目首次在 NVS 全空状态开配网（merged bin 擦除后才出现），与负责人「刷机前正常」的对照完全自洽。修复：`xiaomiao_wifi_provisioning.c` 在 `esp_wifi_set_mode(APSTA)` 成功后 `esp_wifi_disconnect()` 取消该尝试（非 OK 打一行 INFO 便于实机分辨）。第四层配置暂留 `RF_CAL_FULL`（它已被证明不是变量，避免本轮再动 sdkconfig）。状态：待人工验证第五层修复。
- 2026-09-25 18:05 -- 第三轮实机（负责人：「配网还是失败」）**否定第五层**：`esp_wifi_disconnect()` 返回 `ESP_OK`（新增的那行 INFO 没打印＝当时确有尝试在飞并被取消），手机仍 `join, AID=1, bgn, 20` → 6.04 s 后 `leave, reason = 15`（比上轮 4.06 s 长，说明 AP 侧把 M3 重发了若干次）；`Haven't to connect to a suitable AP now!` 依旧紧挨在 `mode : sta + softAP` 前一行。至此 reason=15 已有四层假设被实机连续否定（缓存旧密码／带宽强制／RF 校准／STA 抢射频），密码长度也已排除（`XIAOMIAO_WIFI_AP_PASSWORD_BUF=9` ⇒ 恰好 8 位数字）。方法论纠正：**停止外推，改为向驱动取证**——`xiaomiao_wifi_provisioning.c` 在 AP 配置生效后读回并打印真实参数（authmode/channel/ssid_len/pwd_len/max_conn/pmf_required，只打密码长度），会话期间 `esp_log_level_set("wifi", ESP_LOG_DEBUG)`，会话停止与启动回滚路径都恢复 INFO；`esp_wifi_disconnect()` 保留，但注释里的因果声明改成安全理由（会话期间不加入陌生开放热点），与本次故障无关。待人工：重新烧录一轮，附 `AP in effect` 行与 join 前后的 `D (....) wifi:`／AM/WPA 行；同时可用一台从未配过 `Xiaomiao-61AD` 的设备做零成本对照。状态：第四轮取证待人工。
- 2026-09-25 18:50 -- 补齐取证的编译期前提（自查发现，未烧录即修正）：`CONFIG_LOG_MAXIMUM_LEVEL=3` 会让 `ESP_LOGD` 在编译期被裁掉（IDF 6.1 `esp_log_level.h:57/73`：`LOG_LOCAL_LEVEL` 默认取 `CONFIG_LOG_MAXIMUM_LEVEL`），届时 `esp_log_level_set(..., DEBUG)` 无效、本轮白烧。本地 `sdkconfig` 改为 `CONFIG_LOG_MAXIMUM_LEVEL_DEBUG=y`＋`CONFIG_LOG_MAXIMUM_LEVEL=4`（默认运行级仍 INFO；不进 `sdkconfig.defaults`）。代码侧：DEBUG 提级移到 `session_start` 入口（切 APSTA 之前），并同时提 `wpa` 标签——IDF 6.1 的 AP 侧握手由**源码编译**的 `components/wpa_supplicant/src/ap/wpa_auth.c` 承担，`wpa_printf` 统一走标签 `"wpa"`（`include/utils/wpa_debug.h:24`），`Received EAPOL-Key`/`Send EAPOL(...)` 均为 DEBUG；另注意 `wpa_auth.c:1913 invalid MIC in msg 2/4` 是 INFO 级、前三轮本应可见却从未出现。AP 参数读回保留在 BW20 之后。状态：第四轮取证待人工（命令与判读标准见上节）。
- 2026-09-25 17:03 -- 人工执行 `idf.py -p COM5 flash monitor` 时，构建在 `xiaomiao_wifi_provisioning.c:728` 失败：ESP-IDF 6.1 的 `wifi_ap_config_t` 没有 `pmf` 成员。对照本机 `esp_wifi_types_generic.h`，AP 的 PMF 配置字段是 `pmf_cfg`，其中 `required` 表示是否强制 PMF。诊断日志改为读取 `applied.ap.pmf_cfg.required` 并同步修正日志预期；仅完成源码和头文件静态核对，重新构建、烧录及握手取证均待人工。
- 2026-09-25 17:11 -- 人工重烧后提供串口日志：AP 配置读回为 `authmode=3 channel=1 ssid_len=13 pwd_len=8 max_conn=1 pmf_required=0`，httpd 成功启动；手机 42675 ms 关联、46691 ms 以 reason=15 断开。日志有 `wifi` DEBUG，却没有 `wpa` 行。复核 ESP-IDF 6.1 的 `supplicant_opt.h` 与 `wpa_debug.h`：`CONFIG_ESP_WIFI_DEBUG_PRINT` 关闭会把 `wpa_printf` 宏编译为空，此前仅提高 `CONFIG_LOG_MAXIMUM_LEVEL` 与运行时标签级别不够。已在本地忽略的 `sdkconfig` 打开该开关；可能输出密钥十六进制，下轮只回传握手状态文字行。重新构建与手机握手结果待人工。
- 2026-09-25 17:13 -- 为同一轮取证补充 httpd 启动后与手机关联瞬间的内部 RAM 快照（free、largest8bit），用于判断握手期间是否还存在明显内存压力；仅新增两行无密钥日志。构建、烧录及实机结果待人工。
- 2026-09-25 17:22 -- 人工再次构建烧录的日志仍无 `wpa` 行。检查构建结果：本地 `sdkconfig` 被 CMake 重新配置为 `# CONFIG_ESP_WIFI_DEBUG_PRINT is not set`，`build/config/sdkconfig.json` 为 false，说明此前手工置 `y` 没有进入固件；没有握手 DEBUG 证据，不能推断 M2 是否到达。内存新证据：`httpd_start` 前 `largest8bit=6144`，后为 704 字节；该压力与握手失败的因果关系尚未证实。下轮改为人工在 `idf.py menuconfig` 中启用并保存，先构建、核对生成的 `sdkconfig.h` 含宏，再烧录；未命中宏则不重复烧录。
- 2026-09-25 17:34 -- 人工输入时将 `idf.py build` 与后续 PowerShell 条件表达式合并成一行，`idf.py` 把后者当成参数并报用法错误，尚未完成本轮构建。已把验收命令改成逐条执行的四步，并把配置检查改为单独的 `Select-String` 输出核对。
