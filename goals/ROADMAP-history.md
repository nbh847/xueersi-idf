# ROADMAP 历史记录

`ROADMAP.md` 在 2026-09-20 重构时，把逐条的施工流水与验证流水迁到本文件，ROADMAP 只保留当前状态、当前开发节点、有序开发节点与下一步。

**本文件只记录“发生了什么、结论是什么、证据在哪”，不复制证据原文。** 串口日志分析、口径差异、证据类型差异、未验证范围等细则都写在各自 Goal 文档中，按每条右侧的路径查阅。如需还原重构前的完整原文，查 `git log -p ROADMAP.md` 的本次重构前提交。

## 施工记录

- 2026-09-25 22:25 -- 负责人确认节点 15 功能开发及配网回归可收尾并提交。15A～15D 的既有验收结论保持不变；独立配网 Goal 以连续两次热点 DHCP、网页扫描、目标 STA 获取 IPv4 与凭据保存的人工日志标记完成。清理已被否定的强制 RF 全校准实验配置，保留重启自动连接、互联网可达及单缓冲刷新表现的未验证边界。→ `goals/20260922-1701-assets-filesystem-chinese-font.md`、`goals/20260925-1120-wifi-provisioning-httpd-task-fix.md`

- 2026-09-25 22:13 -- 人工回传新固件连续两次配网日志：手机两次获热点 `192.168.4.2`，HTTPD、扫描、目标 Wi-Fi 连接、STA IPv4 与凭据保存均完成，第二次“寻找 IP”故障未复现。两次关闭后内部 free 为 70619／73535 字节，未见单调下降；旧 UDP PCB 遗留机制未被直接观测。重启自动连接、互联网可达与显示表现待验证。→ `goals/20260925-1120-wifi-provisioning-httpd-task-fix.md`

- 2026-09-25 22:05 -- 负责人确认第二次连接热点始终显示“寻找 IP”，此前“可能因无互联网切网”的猜测不再适用。对照本机 IDF 6.1 源码，AP netif 在异步 AP_STOP 前销毁可能留下 DHCP UDP/67；正常停止与启动失败回滚均改为先同步停止 DHCP。机制与症状吻合，构建烧录、第二会话获取 IP、网页换网和重复开合均待人工验证。→ `goals/20260925-1120-wifi-provisioning-httpd-task-fix.md`

- 2026-09-25 21:54 -- 人工再次实测单缓冲固件：第二会话 HTTPD 成功启动，最大内部 8-bit 连续块 36,864 字节；手机两次完成热点关联，约 18 秒后 `reason=3` 离开。负责人补充手机连热点约 18 秒后自动切到其他 Wi-Fi；日志没有第二轮 DHCP 分配、页面扫描或目标网络 `trial connect started`，故不能判为新网络凭据失败，先核对手机是否拿到 `192.168.4.x`；PC Agent HTTP_CLIENT 超时亦非目标网络试连证据。待确认手机手动访问 `http://192.168.4.1/` 的页面现象；当前不改代码。→ `goals/20260925-1120-wifi-provisioning-httpd-task-fix.md`

- 2026-09-25 21:42 -- 恢复 HTTPD、显示降为单块 40,960 字节 DMA 缓冲后，人工实机日志确认首会话扫描、目标网络四次握手、STA 取得 `192.168.1.3`、凭据保存和配网会话成功关闭。底层发送失败机制仍未证明；关闭 WPA2 AP 时的 WPA3 任务删除错误已静态追至 IDF 未初始化 WPA3 任务却仍调用删除；后续仍执行 AP 清理，该日志不证明第二会话泄漏。已把配网入口的 `wifi`／`wpa` 运行时日志固定为 INFO，以免继续输出密钥，同时保留编译期开关以观察 WPA ERROR；日志清理改动未重新构建烧录；重启持久化、第二会话与显示回归待人工。→ `goals/20260925-1120-wifi-provisioning-httpd-task-fix.md`

- 2026-09-25 21:32 -- 人工对照：诊断固件跳过 HTTPD 后，同一手机完成 WPA 握手并获热点 IP `192.168.4.2`；不能上网／弹出配网页符合诊断版本预期。HTTPD 的资源或时序变化参与此前关联响应失败，内存机制仍待确认。现已恢复 HTTPD，显示改为单块 40,960 字节全屏 DMA 缓冲以释放内部 RAM；源码静态检查完成，构建、真实配网与显示回归待人工。→ `goals/20260925-1120-wifi-provisioning-httpd-task-fix.md`

- 2026-09-25 19:23 -- 为区分 HTTP 服务占用与 AP 关联响应发送失败，在配网模块加入临时 `PROV_DIAG_SKIP_HTTPD=1`：保留 AP/DNS/定时器，跳过网页服务并打印诊断模式与堆快照。源码静态复核完成；编译、烧录及同机手机关联对照待人工。诊断版不能完成网页配网，取证后需恢复正常模式。→ `goals/20260925-1120-wifi-provisioning-httpd-task-fix.md`

- 2026-09-25 19:18 -- 人工 WPA DEBUG 日志把最新配网失败定位到 AP 发送关联响应：多次 `join success` 后立刻 `esp_send_assoc_resp_failed: send failed`、`reason=1`，尚未进入四次握手。本机 ESP-IDF 6.1 源码只可追至 `esp_wifi_send_mgmt_frm_internal()` 非零返回，驱动内部原因待查；`httpd_start` 后最大内部 8-bit 连续块仅 496 字节，内存关联尚未证实。下一步计划同机对照 HTTP 服务启动前后关联结果，未实施。→ `goals/20260925-1120-wifi-provisioning-httpd-task-fix.md`

- 2026-09-25 17:22 -- 最新实机仍在热点关联后 reason=15 离开；httpd 启动使内部 8-bit RAM 最大连续块从 6144 降至 704 字节，但尚不能据此确认内存是根因。构建产物确认 `CONFIG_ESP_WIFI_DEBUG_PRINT` 被 CMake 重新配置为关闭，WPA 日志仍未启用；下轮改为人工 `menuconfig` 启用、构建后核对生成配置，再烧录。→ `goals/20260925-1120-wifi-provisioning-httpd-task-fix.md`

- 2026-09-25 17:11 -- 修正诊断编译缺口：新固件已能构建和启动，AP 参数读回正常，手机仍在关联约 4 秒后 reason=15 断开。缺少 `wpa` 日志的原因是 `CONFIG_ESP_WIFI_DEBUG_PRINT` 关闭会把 `wpa_printf` 编译为空；已在本地忽略的 `sdkconfig` 打开该取证开关，并于 17:13 补充 httpd 启动后与手机关联时的内部 RAM 快照，下一轮握手日志待人工。→ `goals/20260925-1120-wifi-provisioning-httpd-task-fix.md`

- 2026-09-25 -- 第三轮实机**否定第五层**（上一条目的根因假设不成立）：`esp_wifi_disconnect()` 返回 `ESP_OK`（新增 INFO 行未打印＝确有尝试被取消），手机仍 `join, AID=1, bgn, 20` → 6.04 s 后 `leave, reason = 15`。至此 reason=15 的四层假设（缓存密码／带宽／RF 校准／STA 抢射频）与密码长度全部被实机否定；该取消调用保留但注释改成安全理由。方法论纠正：不再由现象外推，改为让握手自己作证——会话入口把 `wifi`＋`wpa` 提到 DEBUG、AP 配置生效后读回打印真实参数（只打密码长度）。同时修掉一个会让本轮白烧的编译期前提：IDF 6.1 `LOG_LOCAL_LEVEL` 默认取 `CONFIG_LOG_MAXIMUM_LEVEL`，本地 `sdkconfig` 的 3（INFO）会裁掉所有 `ESP_LOGD`，已置 `CONFIG_LOG_MAXIMUM_LEVEL_DEBUG=y`＋`=4`（默认运行级仍 INFO，不进 `sdkconfig.defaults`）；AP 侧握手在 IDF 6.1 属源码编译的 `components/wpa_supplicant`，调试行走 `wpa` 标签，且 `wpa_auth.c:1913 invalid MIC in msg 2/4` 是 INFO 级、前三轮从未出现。第四轮取证待人工。→ `goals/20260925-1120-wifi-provisioning-httpd-task-fix.md`

- 2026-09-25 -- reason=15 定位到第五层并落地修复（RF 校准假设被第二轮实机否定）：`Haven't to connect to a suitable AP now!` 与 `mode : sta + softAP` 同毫秒 ⇒ 配网切 APSTA 时驱动自发 STA 连接尝试；根因是 Service 无凭据分支把 STA 配成「空 SSID＋OPEN 阈值」。单射频被拽离 AP 信道导致四次握手超时，与密码、带宽、校准均无关。该分支自 `28ffac3` 存在，但项目首次在 NVS 全空状态开配网（merged bin 擦除所致），故表现为「刷机后才坏」。修复：`xiaomiao_wifi_provisioning.c` 在 `set_mode(APSTA)` 成功后 `esp_wifi_disconnect()`。同轮记录：`CONFIG_ESP_PHY_RF_CAL_FULL` 已生效但不是变量（暂留）；`largest8bit=6400` 却 `httpd_start` 成功，否定二层单指标闸门推断。待人工验证。→ `goals/20260925-1120-wifi-provisioning-httpd-task-fix.md`

- 2026-09-25 -- RF 校准实验第一轮判为**无效**并纠正：日志 `phy_init ... mode(0)` 证明 `sdkconfig.defaults` 的新选项没被采纳（它只播种不存在的本地 `sdkconfig`），已直接改本地 `sdkconfig` 为 `CONFIG_ESP_PHY_RF_CAL_FULL=y`＋`CALIBRATION_MODE=2` 待第二轮。同轮排除带宽因素（`bgn, 20` 仍 `reason=15`），并否定二层「`INTERNAL|8BIT` largest 是闸门」的推断（`largest8bit=6400` 却 `httpd_start` 成功）；新线索：每次开机校准存档都被判 checksum 无效。→ `goals/20260925-1120-wifi-provisioning-httpd-task-fix.md`

- 2026-09-25 -- reason=15 改判为本次刷机的副作用（第四层，取代上一条目 Proposed 的换手机实验）：负责人对照「刷 merged bin 之前联网正常」⇒ merged raw 镜像覆盖 `0x0～0x39FFFF` 连带擦掉 `0x9000` NVS 里的 PHY RF 校准存档；本机 `CONFIG_RTC_CLK_SRC_INT_RC=1`（无外部 32k 晶振），重新校准偏差正好是「关联成功＋EAPOL 丢失」形态。实验改动唯一：`sdkconfig.defaults` 增 `CONFIG_ESP_PHY_RF_CAL_FULL=y`，本轮只烧 app 分区不擦 NVS，结果待人工复验。→ `goals/20260925-1120-wifi-provisioning-httpd-task-fix.md`

- 2026-09-25 -- 配网第三层缺陷独立成立（旧「缓存密码」归因作废）：重启后首个健康会话（`free=26015`、`session ready`）内输入正确屏幕密码，手机仍两次 `join, AID=1` → 4～6 s `leave, reason=15`。静态复核排除 STA 扫描竞争（无凭据时 Service 不发起连接）与密码字符/长度问题；保留疑点：`bgn, 40U` 说明 BW20 强制未生效（返回值被吞）。判定实验（换客户端、记录超时规律）与实验方向（会话期间临时 `WIFI_MODE_AP`）已写入 Goal 末节，未改码。→ `goals/20260925-1120-wifi-provisioning-httpd-task-fix.md`

- 2026-09-25 -- 配网第二会话复现 `0xb008`，确认每轮会话闭合净漏约 3.8 KB 内部 RAM（`free` 26119→22291、plain largest 不变），降栈修复仅对开机首会话有效；诊断升级为 `INTERNAL|8BIT` largest＋释放行 heap 快照，泄漏定位待下一轮日志。人工临时通道：重启后首个会话内配网（后被同日第三层缺陷阻断）。→ `goals/20260925-1120-wifi-provisioning-httpd-task-fix.md`

- 2026-09-25 -- 配网 httpd 修复实机验证通过：重烧后 `session ready, ssid='Xiaomiao-61AD'`、无 `httpd_start failed`（同轮 `largest=18432` 仍小于旧需求 20,480 B，证明 3584→14,336 B 的降栈正对症）。手机两次 join 后 reason=15 离开为节点 10 已确认的旧密码缓存既有行为，需手机忘记热点后用屏幕当前密码重连完成配网。（该 reason=15 归因后被同日更重的证据否定，见上方最新条目。）→ `goals/20260925-1120-wifi-provisioning-httpd-task-fix.md`

- 2026-09-25 -- 配网 `ESP_ERR_HTTPD_TASK (0xb008)` 定位并修复（待人工复验）：人工 heap 日志给出 `free=26123 largest=18432`，结合 IDF 6.1 源码确认 `xTaskCreatePinnedToCoreWithCaps` 把 httpd `stack_size` 按字×4 分配连续内部 RAM——原 5120 实需 20,480 B，大于当时最大连续块。修复：`xiaomiao_wifi_provisioning.c` 栈降为 3584（14,336 B）并保留两行 heap 诊断日志；属节点 11/14/15 内部 RAM 累积压力导致的对节点 10 功能的回归，与节点 15 结论无关。同轮日志 `has_credentials=0` 证实 merged 单文件烧录清 NVS 为预期代价。→ `goals/20260925-1120-wifi-provisioning-httpd-task-fix.md`

- 2026-09-25 -- **节点 15 完成收口**：CP7 merged bin 通过——`idf.py merge-bin` 产出 `build/merged-binary.bin`（0x3a0000，恰好覆盖至 assets 分区尾，四项镜像齐全），esptool 从 `0x0` 单文件写入 3,801,088 字节（`Hash of data verified`，不另刷 assets.bin），冷启动 `font READY ... resident, init 584 ms`、五 App 中文显示正常，发布形态成立。预期代价：整段擦除覆盖 NVS，Wi-Fi 凭据与 Settings 被清、需重新配网。范围外新缺陷（不阻塞收口、待立项）：重配网时 `httpd_start` 两次 `ESP_ERR_HTTPD_TASK (0xb008)`（SoftAP/DHCP/DNS 正常，仅 5120 B 栈的 httpd 任务创建失败，疑内部 RAM 不足，节点 10 后疑似回归）。→ `goals/20260922-1711-node15-integration-validation.md`

- 2026-09-25 -- 节点 15 CP6 全 UI 与既有功能回归通过（项目负责人目视逐项确认「没啥问题」，未附新增日志／截图，证据类型与节点 8/9 末轮回归同类口径），CP1～CP6 就此全部闭环。节点 15 仅剩可选 CP7 merged bin（首次尝试因命令名笔误 `mergebin` 失败，正确为 `idf.py merge-bin`，输出 `build/merged-binary.bin`，从 `0x0` 单文件烧录验证发布形态），执行或豁免后即可勾选。→ `goals/20260922-1711-node15-integration-validation.md`

- 2026-09-25 -- 节点 15 CP5 有 SD 部分实机通过、20 次开合补齐、损坏注入经批准跳过：插卡后 A 扫描挂载成功且中文显示不受影响、其余 App 正常；21 组开合日志（games×3／pc_monitor×6／settings×2／tools×10，`screen children=2` 恒定）加前轮 5 组共 26 组，超出 20 次口径，无泄漏迹象。字体损坏英文回退启动观察由项目负责人决定明确跳过（不作通过项，probe 级 CRC 拒收与 token 永不返回 NULL 的静态契约保留）。CP4 就此全部闭环；节点 15 剩余 CP6 全 UI 逐项回归与可选的 CP7 merged bin（验证发布形态：分区偏移／factory 单文件重装／Assets 交付，不验新功能），保持未勾选。→ `goals/20260922-1711-node15-integration-validation.md`

- 2026-09-23 -- 节点 15 Font 自测实机 `PASS`（流式版，ELF `6418ab4b8`，无 SD，全程无 WDT）：init 569 ms、内部 RAM 成本 316 字节、全表 7,445 码点两档遍历 0 failures、fixture 仅以 CRC 被拒、fallback 语义确认。同时暴露遍历耗时 262.6 秒（≈17 ms/字模，根因 SPIFFS 随机 seek 触发对象查找区全表扫描），生产首屏冷渲染不可接受。经项目负责人批准，Font Service 重写为整包 PSRAM 驻留：init 一次顺序读入 759,456 字节并在内存内完成全量校验，glyph 读取退化为指针运算＋A2→A8 展开，LRU／scratch 删除，snapshot ABI 保留（`cache_misses` 恒 0）。待人工重跑字体自测（预期遍历秒级、SPIRAM delta ≈ -793 KB、init 日志含 `resident`）后进常规固件 CP4。→ `goals/20260922-1711-flash-chinese-font-runtime.md`、`goals/20260922-1711-node15-integration-validation.md`

- 2026-09-24 -- 节点 15 CP4 中文目视复验通过（字形落位修复版）：字体自测重跑（ELF `e853cd96c`，新增落位不变量断言）`PASS`、驻留版指标无回归；常规固件六开关全 `OFF` 无 SD 冷启动，项目负责人确认五个 App 中文显示与 12／16 px 可读性正常，叠压缺陷消除，15C 中文 UI 由未验证转为实机确认。同轮附带 5 组 App 开合 `screen children=2` 恒定（20 次口径的代表样本）。节点 15 剩余：完整 20 次开合、CP5（有 SD 对照／插拔／字体损坏英文回退）、CP6 全 UI 回归、CP7 merged bin。→ `goals/20260922-1711-flash-chinese-font-runtime.md`、`goals/20260922-1711-node15-integration-validation.md`

- 2026-09-24 -- 节点 15 CP4 常规固件中文目视不通过（实机照片：全部中文糊成重叠笔画），Agent 定位为 LVGL 9.5 字形摆位语义误用：`lv_font_t.base_line` 须填降部（`line_height - base`）、`ofs_y` 是盒底相对基线上偏移（本字库为 `base - px`＝0），原实现按 LVGL 8 旧语义填 ascent/`base`，中文字模被整体上移约一个行盒。修复 `xiaomiao_font_service.c` 两处并在自测加落位不变量断言；XMF1 包与生成器不动。教训：两轮自测 `PASS` 只覆盖位图管线，视觉验收不可省。待人工重跑字体自测＋重新目视。→ `goals/20260922-1711-flash-chinese-font-runtime.md`

- 2026-09-24 -- 节点 15 Font 自测驻留版实机 `PASS`（ELF `81482b8ac`，无 SD，无 WDT）：`bytes resident` 确认新构建；init 594 ms、heap delta `internal=0 SPIRAM=-785160`（整包 PSRAM 驻留、内部 RAM/DMA 归零）、全表遍历 703 ms／0 failures（约 370 倍提速）、`fetches: hits=15190 misses=0`、fixture 仍仅以 CRC 被拒。15B CP2／CP3 与两套自测就此闭环，性能整改完成。剩余常规固件 CP4 无 SD 中文目视与 CP5～CP7 人工验收。→ `goals/20260922-1711-flash-chinese-font-runtime.md`、`goals/20260922-1711-node15-integration-validation.md`

- 2026-09-23 -- 节点 15 Font 自测实机首跑：先暴露自测开关切换陷阱（CMake `option()` 缓存持久，旧 Assets 开关不显式 OFF 则互斥链仍取 Assets，零重编＋No changed sectors，已在 15B／15D 记录口径）；正确切换后步骤 1～2 通过，步骤 3 全表遍历连续占用 main 约 10 秒触发 task_wdt 告警（IDLE0 饿死，非崩溃非死锁）。修复：遍历循环每 32 码点 `vTaskDelay(1)`。待重跑 `FONT_SERVICE_SELF_TEST: PASS`。→ `goals/20260922-1711-flash-chinese-font-runtime.md`

- 2026-09-23 -- 节点 15 Assets 自测第三轮实机 `PASS`（ELF `be39aef26`）：15A 两个 SPIFFS 契约修复（seek 越 EOF 钳制、空目录流返回 `ESP_ERR_NOT_FOUND`）确认生效，步骤 1～4 全过后正常挂起；同轮确认挂载约 94 ms、`total=1438481 used=783371` 复现、`spiffsgen --obj-name-len=48` 与 `0x220000 assets.bin` 参数、自测构建 unused-function 警告为互斥分支预期。Assets 自测项闭环，下一步字体自测与常规固件 CP4 无 SD 验收。→ `goals/20260922-1711-assets-filesystem-foundation.md`、`goals/20260922-1711-node15-integration-validation.md`

- 2026-09-23 -- 节点 15 Assets 自测第二轮（修复版 ELF `31c4e07bd`）：首轮两处修复实机确认生效（无崩溃、快照 memcmp 通过），新暴露两个被崩溃掩盖的 SPIFFS 平台差异并已修复——`xiaomiao_asset_seek` 越 EOF 目标钳制到文件尾（SPIFFS `lseek` 拒绝越过长度，统一两命名空间契约），`xiaomiao_asset_list` 对空目录流（SPIFFS 对不存在路径 opendir 成功）返回 `ESP_ERR_NOT_FOUND`，代价与 FATFS 侧待复核项记录于 15A。均为契约细化，签名不变，Font／Icons 调用方不受影响。待第三轮重跑 `ASSETS_SERVICE_SELF_TEST: PASS`。→ `goals/20260922-1711-assets-filesystem-foundation.md`、`goals/20260922-1711-node15-integration-validation.md`

- 2026-09-23 -- 节点 15 进入人工验证：首次全量构建一次通过（`xiaomiao.bin` 0x176070≈1497 KB，2 MB factory 余 27%，flash args 含 `0x220000` Assets 镜像；`'default 0' bool` 告警核对为 IDF 组件既有、非本仓库）。Assets 自测实机首跑暴露并修复两个缺陷：① `get_snapshot` 未清零结构 padding 使 memcmp 幂等断言假失败；② 自测文件生命周期假定 manifest ≤ 64 字节（实际 519），`fread` 栈溢出破坏句柄指针触发 `LoadProhibited` 重启循环。实机正向证据：SPIFFS 挂载 READY、total=1438481/used=783371、路径规则表全过、分区无漂移。待修复版重跑两套自测与常规固件 CP4 场景。→ `goals/20260922-1711-assets-filesystem-foundation.md`、`goals/20260922-1711-node15-integration-validation.md`

- 2026-09-22 19:30 -- 节点 15C／15D 完成 Agent 侧全部实施与复核：main.c Hardware Test 15 页由专用后台代理补完（页名 ID 表、`short_err`、10 提示、7 手势渲染期映射、37 动作结果、11 About 头、App 名运行时解析，旧宏与 `lv_font_montserrat_*` 清零），主 Agent 独立核收（201/203 ID 被引用、两个未引用 ID 经证明无对应字面量、全 UI 文件 ASCII／括号检查、`git diff --check`、路径与调用边界）；PC Monitor 一处中文注释改 ASCII。文档同步：15C 交付结果、15D CP1 静态部分＋CP2 终检、总纲／ROADMAP 状态改为「15A～15C 已实施、15D 静态复核完成，待人工构建烧录验证」，README 与项目概览记录中文 UI 事实与回退链。节点 15 保持未勾选：CP3～CP7（编译、两套自测 `PASS`、无 SD 中文、SD 故障降级、全 UI 回归、merged bin）全部等待项目负责人人工证据。→ `goals/20260922-1711-ui-chinese-localization.md`、`goals/20260922-1711-node15-integration-validation.md`

- 2026-09-22 18:28 -- 节点 15C 进行中收拢（Goal 回合预算耗尽、自动暂停前）：i18n 词表冻结（203 个 `XM_TEXT_*`，中英双表完整、全部字符在 GB2312 内、中文仅在 Font Service READY 时启用）；Launcher／Games／PC Monitor／Tools 已完成中文化，Tools 新增 Assets 诊断页（三服务快照 6 行、动态值 DOTS 截断），Launcher 接入内置图标三级回退（`asset:/icons/<id>.bin` → LVGL Symbol → 占位条，`xiaomiao_icons.{h,c}` 已注册 CMake，`assets/icons/` 暂为空目录占位、回退链生效）；Settings 与 Hardware Test 15 页迁移仍在进行，完成后需残留英文扫描、文案 ID 引用核销、`git diff --check` 与 README 同步，随后进入 15D 终检与 CP4／CP7 人工验证。15D 静态边界预检已提前通过（SPIFFS 单点注册、无 `sd:/` 字体路径、App 层无 FS 调用、自测互斥链、`.gitignore` 精确例外）。全部编译与显示效果仍为未验证。→ `goals/20260922-1711-ui-chinese-localization.md`、`goals/20260922-1711-node15-integration-validation.md`

- 2026-09-22 18:02 -- 节点 15B 收口前容量复核修正：旧 fixture（完整包 759 KB 的 1 字节 CRC 翻版）使 `assets/` 负载达 1,526,763 字节、超出 1.5 MB SPIFFS 可用容量，正常构建的镜像生成将失败。改为 `make_test_fixture.py` 合成的 6,592 字节最小自洽包（仅 CRC 字段损坏、确定性、SHA-256 记录在案），`font_validate()` 检查顺序调整为结构→递增→CRC→完整字库策略（策略失败返回 `ESP_ERR_NOT_SUPPORTED`），保证损坏样本以 `ESP_ERR_INVALID_CRC` 确定性被拒；主机 `verify --self-check` 重新通过，assets 负载降至 774,340 字节。另加 `CONFIG_SPIFFS_OBJ_NAME_LEN=48`（IDF 6.1 确认符号名，首稿误写的 `SPIFFS_MAX_NAME_LEN` 已纠正）消除 32 字符字体路径处于 SPIFFS 名字上限边界的风险。状态：15B 维持已实施、待人工验证。→ `goals/20260922-1711-flash-chinese-font-runtime.md`
- 2026-09-22 17:55 -- 节点 15B“Flash 完整中文字库与字体运行时”完成 CP1～CP3：固定 Noto Sans CJK SC Regular v2.004（SIL OFL-1.1，源文件 SHA-256 为权威指纹，GitHub 速率限制未取得精确 commit）；`tools/font-pack/` 生成器（Pillow 11.3.0，仅项目 venv）产出 `assets/fonts/xiaomiao-zh-cn.xmf`——GB2312 全集 7,445 glyph、759,456 字节 <1 MiB、两次生成 SHA-256 一致，Agent 独立复核 Header／offset 公式／码点严格递增／CRC 与 fixture（仅 0x1C 一字节损坏）全部通过；C 侧新增 `xiaomiao_font_service.{h,c}`（XMF1 全量校验、PSRAM 索引与 128 glyph／档 LRU、12／16 px `lv_font_t` 回调、Montserrat 12／14 fallback、产物读取全走 Assets Service、无 `sd:/` 字体路径）、`xiaomiao_fonts.h` 令牌与 `XIAOMIAO_FONT_SERVICE_SELF_TEST`，并接入 CMake 与启动链。已知偏差：U+3000（Zs）与 U+FF3F（16px 窗口裁剪）登记为空白 glyph。未运行 ESP-IDF 构建、烧录或串口监视，CP4 人工字体验收待执行。状态：已实施，待人工验证。→ `goals/20260922-1711-flash-chinese-font-runtime.md`
- 2026-09-22 17:45 -- 节点 15A“Assets 与只读文件系统基础”完成 CP1～CP3 编码与静态检查：新增 `main/services/xiaomiao_assets_service.{h,c}`（幂等只读挂载 `/assets`、三态快照、`asset:/`／`sd:/` 路径解析与拒绝表、只读文件与有界目录遍历 API）、自测 `XIAOMIAO_ASSETS_SERVICE_SELF_TEST`、`assets/` manifest 与目录占位；CMake 接入 `spiffs_create_partition_image(assets ../assets FLASH_IN_PROJECT)`，`main/main.c` 启动链在非致命位置初始化 Assets Service，`.gitignore` 增加 `!assets/**/*.bin` 例外。`sd:/` 只读取 Storage Service 快照、从不自动挂载。未运行 ESP-IDF 构建、烧录或串口监视，CP4 无 SD／有 SD 实机场景待人工验证。状态：已实施，待人工验证。→ `goals/20260922-1711-assets-filesystem-foundation.md`
- 2026-09-22 17:11 -- 修正节点 15 施工拆分：原 `goals/20260922-1701-assets-filesystem-chinese-font.md` 保留为总纲，新增四个可独立执行和验收的子 Goal，依次为 15A Assets 与只读文件系统基础、15B Flash 完整中文字库与字体运行时、15C 全系统中文化与布局适配、15D 集成与发布验收；四者有依赖，后续每次只执行一个。状态：四个子 Goal 均待实施。→ `goals/20260922-1711-assets-filesystem-foundation.md`、`goals/20260922-1711-flash-chinese-font-runtime.md`、`goals/20260922-1711-ui-chinese-localization.md`、`goals/20260922-1711-node15-integration-validation.md`
- 2026-09-22 17:01 -- 创建节点 15“Assets、文件系统与 Flash 完整中文字库”施工文档并确认方案：完整 GB2312（6,763 汉字 + 682 符号）提供 12／16 px A2 两档，随固件存入本机 1.5 MB `assets` SPIFFS 分区，通过 Flash 流式读取与 PSRAM glyph 缓存接入 LVGL；默认中文，字体异常时英文降级，SD 不参与生产字体加载。范围同时覆盖现有 UI 中文化、内置图标、只读 `asset:/`／显式 `sd:/` 资源接口和无 SD 强制验收。状态：设计已确认，待实施。→ `goals/20260922-1701-assets-filesystem-chinese-font.md`
- 2026-09-22 15:38 -- 节点 14“Storage Service”完成 CP5 人工实机验收：项目负责人确认 ESP-IDF 构建、烧录与九个场景全部通过（无卡冷启动与 10 次重试无 GPIO22 冲突、插卡挂载成功 SD 29818MB、带卡冷启动直接进 MOUNTED、卸载与幂等重挂、15 页往返与五 App 烟测无回归）。`cmd=5 R1 illegal command` 确认为 IDF v6.1 标准 SDIO 探测步骤、microSD 拒绝 CMD5 属预期正常；无卡时的 `0x107` 确认为卡不响应的预期行为。Agent 未重复执行构建、烧录或串口监视。状态：已完成。→ `goals/20260922-0938-storage-service.md`
- 2026-09-22 10:04 -- 节点 14“Storage Service”完成 CP1～CP3 编码与 CP4 静态复核：新增 `main/services/xiaomiao_storage_service.{h,c}`（固定 `/sdcard`、同步串行、幂等、快照输出、保留 GPIO22 修复时序、禁止自动格式化），`main/main.c` 迁出全部 SD 生命周期并接入启动链与 MicroSD 页 A／B 动作，`SERVICE_SRCS` 加入新源文件；静态检查（调用边界、括号配平、行尾空白、diff 范围）通过。未运行 ESP-IDF 构建、烧录或串口监视，CP5 实机验收待人工执行。状态：已实施，待人工验收。→ `goals/20260922-0938-storage-service.md`
- 2026-09-22 09:38 -- 创建节点 14“Storage Service 与 MicroSD 完整验证”施工文档并立项：迁移 SD 生命周期到独立 Service，保留 GPIO22 修复，Hardware Test 改用公开快照与挂载／卸载接口；SD 卡已到货，计划覆盖无卡启动、正常挂载、安全卸载、重新挂载、带卡冷启动和共享 SPI 回归。状态：已立项，尚未实施。→ `goals/20260922-0938-storage-service.md`
- 2026-09-21 21:58 -- 项目负责人确认已知良好 SD 卡尚未到货，`0x107` 根因区分与正常卡挂载验证挂起，待到货后复测；GPIO22 冲突修复其余部分保持已验证通过状态。→ `goals/20260921-2101-sd-gpio22-conflict-fix.md`
- 2026-09-21 21:48 -- 完成新版 MicroSD GPIO22 修复的静态复核：确认不再调用 `esp_vfs_fat_sdspi_mount()`，手动流程覆盖 SDSPI 初始化、卡初始化、FATFS 挂载、失败清理和成功卸载；未运行项目禁止的 ESP-IDF 构建、烧录或串口监视，实机结果待补。状态：已实施，待验证。→ `goals/20260921-2101-sd-gpio22-conflict-fix.md`
- 2026-09-21 21:43 -- 根据人工日志重新核对 IDF 6.1 失败清理时序：挂载前复位无法阻止 `esp_vfs_fat_sdspi_mount()` 内部在 `sdmmc_card_init()` 失败后立即执行的 GPIO 配置；`main/main.c` 改为手动 SDSPI 初始化与 FATFS 挂载，失败清理和成功卸载均在 `sdspi_host_remove_device()` 前复位 GPIO22。状态：已实施，待静态复核和人工验证。→ `goals/20260921-2101-sd-gpio22-conflict-fix.md`
- 2026-09-21 21:29 -- 人工验证 MicroSD GPIO22 修复未通过：用户提供的烧录后日志中至少 6 次出现 `sdmmc_card_init failed (0x107)`，且每次约 1 ms 后仍出现 `gpio: conflict found for GPIO[22]`；`gpio_reset_pin()` 挂载前调用未达到“不再出现冲突警告”的验收预期。`0x107` 尚未通过已知良好 SD 卡区分具体根因，需重新核对失败清理路径。状态：验证失败，未收口。→ `goals/20260921-2101-sd-gpio22-conflict-fix.md`
- 2026-09-21 21:01 -- 定位 MicroSD GPIO22「冲突」根因并实施最小修复：经 IDF 6.1 源码逐行核实，`gpio: conflict found for GPIO[22]` 是 sdspi 驱动 `deinit_slot()` 不调 `esp_gpio_revoke()` 导致的占用位图泄漏（仅警告、非真实引脚冲突，README 与固件中 GPIO22 均只归 SD CS）；修复为 `main/main.c` `sd_try_mount()` 挂载前调 `gpio_reset_pin(PIN_NUM_SD_CS)`。`sdmmc_card_init failed (0x107)`（卡不响应超时）不在本任务处理，需插已知良好卡区分"未插卡预期"与共享 SPI 总线问题。代码未构建、未实机验证，人工验证命令与预期结果见 ROADMAP「下一步」。状态：已实施，待人工验证。→ `goals/20260921-2101-sd-gpio22-conflict-fix.md`
- 2026-09-21 16:33 -- 节点 11 收尾完成：PC Agent 自动化测试更新为 18/18，通过固件静态复核；项目负责人确认 ESP-IDF 构建、烧录与 CP5 手动场景全部通过。Agent 未重复执行固件构建、烧录或串口监视，人工确认未附新增日志、截图或资源数值；不可构造的底层分配／网络栈故障仍按源码检查记录。状态：已完成。→ `goals/20260921-1238-pc-monitor-communication.md`
- 2026-09-21 12:42 后 -- 实施节点 11 全部代码（CP1～CP4）：新增 `pc-agent/`（monitor.py、pc_metrics.py、requirements.txt 固定 `psutil>=7.0,<8`、两个测试文件，17/17 通过）、`main/Kconfig.projbuild`（HOST 默认空、PORT 8766）、`main/services/xiaomiao_agent_service.{h,c}`、`main/main.c` 启动链接入、`main/apps/pc_monitor/xiaomiao_pc_monitor.c` 接入 LVGL timer 快照刷新、CMake 与 `sdkconfig.defaults` 更新。静态复核：括号配平／空白／分层边界（App 不碰 HTTP/Wi-Fi/cJSON/NVS）／资源释放／敏感字面量全部通过。口径差异（HTTP 无独立连接超时、ok 示例与判定文字矛盾按文字为准、可选字段类型错误按降级处理、Worker 栈 4096、age_sec≥3.0 拒绝）与未验证范围（全部固件行为、Agent 实机链路、栈高水位）记录于 Goal 实施记录。状态：已实施，待人工验证（CP5）。→ `goals/20260921-1238-pc-monitor-communication.md`
- 2026-09-21 12:38 -- 创建节点 11“统一 Agent 基线与 PC Monitor 通信”施工文档并立项：固定一个 Python HTTP Agent、固定 IPv4 + 端口 `8766`、`/api/v1/health` 与 `/api/v1/pc/metrics`、固件 Agent Service、CPU／RAM／GPU／温度快照及 3 秒失效语义；服务发现、AI 额度和其他 PC 数据端点不在本节点实现。状态：已立项，尚未实施。→ `goals/20260921-1238-pc-monitor-communication.md`
- 2026-09-21 11:49 -- 节点 10 收尾完成：项目负责人确认人工构建、烧录及手动测试全部通过；Agent 同步 Goal、README、项目概览、AGENTS 与 ROADMAP，并在最终静态审查中修正 NVS 提交失败仍提前替换运行时凭据和关闭配网页的问题。状态：已完成。→ `goals/20260920-2210-wifi-service.md`
- 2026-09-21 — 节点 10「更换网络」功能闭合：以正确密码试连真正不同的网络 `CMCC-5pu4`（信道 6、bssid 与旧网络不同）1.6 秒连上并保存，`POWERON_RESET` 真断电重启后 `saved network loaded, ssid='CMCC-5pu4'` → 自动连上新网络并取得 IP——新凭据跨断电存活且替换旧凭据。同时判定「错误密码不覆盖旧凭据」通过（写 NVS 唯一入口以取得 IPv4 为门控、失败试连从不落盘）。路由器级断线重连与 10 分钟空闲超时完成代码复核、无缺陷，连同图标三态由负责人决定暂缓。→ `goals/20260920-2210-wifi-service.md`

- 2026-09-21 — 节点 10 验证范围收敛：「错误密码不覆盖旧凭据」判定通过（写 NVS 唯一入口以取得 IPv4 为门控、失败试连从不落盘；失败试连后多次重启均连回原网络构成反证）；路由器级断线重连与 10 分钟空闲超时两项完成代码复核、无缺陷（三条会话收尾路径一致走 Service stop + 重连）。负责人决定本轮只补验「正确密码切换到另一个网络 + 重启持久化」，其余暂缓。→ `goals/20260920-2210-wifi-service.md`

- 2026-09-21 — 节点 10 配网链路复测通过并修掉两个阻塞性缺陷。修复：① 换网络时先释放旧关联再试连（此前 `esp_wifi_connect()` 在已关联时被驱动拒绝，导致**更换网络功能实际不可用**、只能干等 20 秒超时）；② 手机缓存旧密码导致首次关联必败一事经定位后**决定保持现状**（固定 SSID + 每次随机密码的必然结果）。复测证据：试连前旧连接被主动释放、不再出现 `sta is connected, disconnect before...`；提交另一个网络 `CMCC-5pu4` 后 4.5 秒失败并 `saved network kept`；回连原网络 2.1 秒成功并保存。同批确认生效：配网期间关闭省电（`Set ps type: 0 → 1`）、HT20、AP 跟随 STA 信道。仍未验证：路由器级断线重连、10 分钟空闲超时、以正确密码切换到另一网络的持久化、图标另外三个状态。→ `goals/20260920-2210-wifi-service.md`

- 2026-09-21 — 节点 10 配网首次走通并完成重启恢复验证。配网日志证明：扫描列出 10 个网络、手机提交后试连 `CMCC-U2Tx`、**取得 IPv4（192.168.1.29）之后才写入凭据**、会话逆序收尾并切回 STA；冷启动日志证明：`has_credentials=1`、自动连回同一 SSID 与同一 IP、Launcher 在取 IP 之前就已就绪（不阻塞启动）。期间修复三处：AP 配置必须先于 `esp_wifi_set_mode(APSTA)` 之后调用、配网会话期间 STA 断开不再触发旧网络重连、DNS 响应器改用 `SO_RCVTIMEO` 退出（lwip 的 UDP `shutdown()` 唤不醒阻塞的 `recvfrom()`）。**DNS 修复与退避重连、错误密码、忘记网络等路径仍未复测。** → `goals/20260920-2210-wifi-service.md`

- 2026-09-21 — 分区表重划（经确认，一次到位）：加 Wi-Fi 后 app 二进制 1359 KB 占满 1500 KB 槽位的 90.6%，只剩 141 KB，而 Flash 另有 2.41 MB 未进分区表。改为项目自有 `partitions.csv`：`nvs`（24 KB @ 0xA000）与 `phy_init`（4 KB）及 factory 起始偏移（0x20000）全部不变，`factory` 扩到 2 MB，新增 1.5 MB `assets`（data/spiffs）预留字体/图标/音效，末尾留约 384 KB。用 IDF `gen_esp32part.py` 校验通过。构建前需重新生成被忽略的本地 `sdkconfig`。→ `goals/20260920-2210-wifi-service.md`
- 2026-09-21 — 节点 10 首个实机证据：双缓冲降级路径生效并确认接受。启动日志为 `third draw buffer unavailable (40960 bytes), falling back to two-buffer refresh` → `LVGL display: 160x128, dpi=60, 2 full-screen DMA buffers, SPI=60 MHz` → `Launcher ready, 5 app(s) registered`，证明 Wi-Fi Service 先于显示初始化且不阻塞启动、全局图标创建成功。项目负责人确认接受双缓冲（60 MHz SPI 下一帧 40 KB 约 5.5 ms、预算 16.7 ms；本屏无 TE 无法垂直同步，三缓冲只换抖动余量），不再为第三块缓冲调参。Wi-Fi 功能本身仍未验证。→ `goals/20260920-2210-wifi-service.md`

- 2026-09-21 — 节点 10 首轮人工构建与烧录：构建依次暴露三处问题（网页字符串漏引号、`HTTPD_ERR_HANDLER_URI_NOT_FOUND` 名称错误、`station_reset_retry` 前向引用）并已修复；烧录后首次启动在 `lvgl_display_init()` 断言 `buf3` 失败——Wi-Fi 占用的内部 RAM 使第三块全屏 LCD DMA 缓冲无法分配。处理：`CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y` 把 Wi-Fi／LWIP 缓冲移到 PSRAM，且第三块缓冲失败时运行时降级为双缓冲而非中止启动。**修复后尚未重新烧录。** → `goals/20260920-2210-wifi-service.md`

- 2026-09-20 — 节点 10“Wi-Fi Service”源码实施完成，状态改为“已实施，等待人工验证”：新增 Wi-Fi Service（状态快照、幂等初始化、扫描、异步连接、退避重连、忘记网络、RSSI 采样）、SoftAP + DNS + HTTP 配网页、全局右上角状态图标，改造 Settings／Tools 的 Wi-Fi 页与启动链，并显式声明网络相关 `sdkconfig.defaults`。凭据按 goal 允许的私有适配器分支实现（驱动全程 `WIFI_STORAGE_RAM` + `xiaomiao/wifi_creds` blob v1）。→ `goals/20260920-2210-wifi-service.md`
- 2026-09-20 — 节点 10 实现后静态检查通过（11 个文件括号配平、非 ASCII、制表符、行尾空白均为 0；7 个 `.c` 文件的 static 函数前向引用检查通过）。工具链 `-fsyntax-only` 检查因缺少 IDF 构建注入选项而失败，报错全部来自工具链系统头文件，不采信该结论。**未运行 `idf.py build`、烧录或 Monitor。** → 同上
- 2026-09-20 — 节点 10 分区表确认（项目负责人选“改用 large single app”）：`sdkconfig.defaults` 加入 `CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE=y`，app 分区由 1 MB 扩大为 1500 KB，nvs 仍为 24 KB 且偏移不变。这是该节点“不修改分区表”边界的唯一例外。因本地 `sdkconfig` 仍显式写着旧分区表选项，构建前需重新生成配置。→ 同上
- 2026-09-20 — 节点 10 阻塞项记录：加入 Wi-Fi 后固件大概率超过默认 1 MB app 分区，需确认改用 large single app（app 1.5 MB，nvs 大小与偏移不变）或自定义分区表；该项被本节点“不修改分区表”的边界禁止，未擅自改动。→ 同上
- 2026-09-20 22:10 -- 创建节点 10 施工文档并立项，固定 SoftAP + 手机网页配网、成功取得 IPv4 后再持久化凭据、自动连接与断线重连、Settings／Tools 接入，以及全局四档彩色 Wi-Fi 状态图标；状态为尚未实施。→ `goals/20260920-2210-wifi-service.md`
- 2026-09-20 — 完成 Launcher 网格导航改造（修订三，最终版）：`←`／`→` 换列并在外侧列翻页、同行优先且缺行回退到目标页第一格，`↑`／`↓` 页内换行不翻页。自测固件 `LAUNCHER_SELF_TEST: PASS`（引导路径 13 步，含缺行回退断言），普通固件实机行为由人工确认无问题。→ `goals/20260920-2015-launcher-lr-paging.md`
- 2026-09-20 — Launcher 网格导航（修订三）自测固件通过：`LAUNCHER_SELF_TEST: PASS`、引导路径 13 步，数量边界含新增的缺行回退断言（`n` 为 5／6 时第 2 行按 `→` 必须落到下一页第一格）。→ `goals/20260920-2015-launcher-lr-paging.md`
- 2026-09-20（修订三）— Launcher 导航再修订：翻页改为**同行优先、缺行回退**（目标页没有对应行时落到该页第一行第一格，目标页没有任何 App 才不翻页），使 5 个 App 时第 1 页第 2 行也能进入第 2 页；数量边界新增该回退断言。修订二虽已通过自测并取得人工确认，仍被本次取代，其结论作废。→ `goals/20260920-2015-launcher-lr-paging.md`
- 2026-09-20 — Launcher 网格导航（修订二）验证通过：自测固件 `LAUNCHER_SELF_TEST: PASS`、引导路径 13 步；普通固件四方向导航与五个 App、Hardware Test 15 页由人工确认符合要求，无补充串口日志。**该结论已随修订三作废。** → `goals/20260920-2015-launcher-lr-paging.md`
- 2026-09-20 — 完成 Launcher 导航改造（`←`／`→` 换列并在外侧列按同一行翻页、`↑`／`↓` 页内换行不翻页）。随后被修订三取代。→ `goals/20260920-2015-launcher-lr-paging.md`
- 2026-09-20（修订二）— Launcher 导航再修订：翻页改为**保持同一行**（右列按 `→` 落到下一页同一行第一格，左列按 `←` 落到上一页同一行第二格，目标格不存在时保持焦点）。修订一未验证即被取代，引导路径由 17 步改为 13 步。→ `goals/20260920-2015-launcher-lr-paging.md`
- 2026-09-20（修订一）— 按用户反馈修订 Launcher 导航：首版“左右线性 ±1 且删掉上下键”改为“`←`／`→` 换列并在外侧列翻页、`↑`／`↓` 页内换行不翻页”，引导路径由 16 步改为 17 步。该版未验证即被修订二取代。→ `goals/20260920-2015-launcher-lr-paging.md`
- 2026-09-20 — 实现 Launcher 导航左右切换（首版：`←`／`→` 线性 ±1，越过本页第 4 格翻页，上下键不再移动焦点；自测引导路径 16 步）。已被同日两次修订取代。→ `goals/20260920-2015-launcher-lr-paging.md`
- 2026-09-20 20:16 — 完成节点 9「Settings Service 与 NVS」，标记为已完成。→ `goals/20260920-1429-settings-service-nvs.md`
- 2026-09-20 — 实现节点 9 源码（新增 `main/services/`、Settings Service、6 步自测入口、CMake 与启动链接入、Settings Display／System 页改造）。状态：曾为待人工验证，后续已完成。→ 同上
- 2026-09-20 14:29 — 创建节点 9 施工文档并立项，固定范围、配置字段与默认值、NVS schema v1、恢复边界与验收标准。→ 同上
- 2026-09-20 14:03 — 完成节点 8「Settings UI」，标记为已完成。→ `goals/20260920-1338-settings-ui.md`
- 2026-09-20 13:45 — 实现节点 8 源码（新增 `main/apps/settings/`，五入口注册，四个只读状态页）。→ 同上
- 2026-09-20 13:28 — 完成节点 7「Tools App」，标记为已完成。→ `goals/20260920-1258-tools-app.md`
- 2026-09-20 13:16 — 实现节点 7 源码（新增 `main/apps/tools/`，Wi-Fi／System Info／About 三项菜单，`PRIV_REQUIRES` 增 `esp_app_format`）。→ 同上
- 2026-09-20 13:10 — 按人工确认修订节点 7 的轮次口径条款，把“连续 10 轮／至少 5 轮交替”改为可满足的“累计 ≥N 次往返 + open／close 成对计数”。→ 同上（修订记录）
- 2026-09-20 12:58 — 创建节点 7 施工文档并立项。→ 同上
- 2026-09-20 12:52 — 完成节点 6「PC Monitor UI」，标记为已完成。→ `goals/20260920-1235-pc-monitor-ui.md`
- 2026-09-20 12:42 — 实现节点 6 源码（新增 `main/apps/pc_monitor/` 静态骨架，四项值恒为编译期占位）。→ 同上
- 2026-09-20 12:35 — 创建节点 6 施工文档并立项。→ 同上
- 2026-09-20 12:29 — 完成节点 5「Games 占位 App」，标记为已完成。→ `goals/20260920-1159-games-placeholder.md`
- 2026-09-20 12:15 — 实现节点 5 代码（新增首个业务目录 `main/apps/games/`，`INCLUDE_DIRS ""` 改为 `"."`）。→ 同上
- 2026-09-20 11:59 — 修正 ROADMAP 中“普通固件仍以单文件 Dashboard 为入口”的过期表述，并创建节点 5 施工文档。→ 同上
- 2026-09-20 11:46 — 完成节点 4「Hardware Test App」，标记为已完成；判定“电机／蜂鸣器运行状态下退出先停输出”在当前硬件上无法构造，标记未验证且不阻塞。→ `goals/20260920-1053-hardware-test-app.md`
- 2026-09-20 11:32 — 为节点 4 检查点 2 补充可观测日志（open／close 打印 active screen 子对象数量与 Launcher 焦点／页码）。→ 同上
- 2026-09-20 11:18 — 实现节点 4 代码（15 页 Dashboard 注册为 `Hardware Test` App、默认入口切到 Launcher、B 手势判定改为 `keypad_read_cb()` 边沿状态机）。→ 同上
- 2026-09-20 11:10 — 修订节点 4 施工文档：保留 800 ms 长按并在决策 8 记录否决理由，决策 9 新增长按可发现性提示。→ 同上
- 2026-09-20 11:02 — 补齐节点 4 的 B 键判定通道决策：经核对 LVGL 9.5 `indev_keypad_proc()`，对象级事件回调通道不可行，改为强制使用按下／释放边沿加独立状态机。→ 同上

## 验证记录

- 2026-09-22 15:38 -- 节点 14 CP5 人工验收通过：ESP-IDF v6.1 构建与烧录成功；九个场景覆盖无卡启动、10 次失败重试、正常挂载、安全卸载、重新挂载、带卡冷启动、共享 SPI 与五 App 回归，结果全部通过。Agent 未重复运行固件构建、烧录或串口监视。→ `goals/20260922-0938-storage-service.md`
- 2026-09-22 09:32 -- 同步 MicroSD 当前状态文档：`README.md`、项目概览和 v0.1 设计文档均明确 GPIO22 冲突已修复并通过连续 9 次失败重试实机验证；正常 SD 卡挂载与 `0x107` 根因区分仍待已知良好卡，不改变节点 14 未完成状态。→ `goals/20260921-2101-sd-gpio22-conflict-fix.md`

- 2026-09-21 21:56 -- 新版 MicroSD GPIO22 修复人工验证通过：烧录后连续 9 次触发 `sdmmc_card_init failed (0x107)`，没有再出现 `gpio: conflict found for GPIO[22]`；Hardware Test 关闭、重开和返回 Launcher 正常。已知良好 SD 卡挂载尚未验证，`0x107` 根因仍待区分。→ `goals/20260921-2101-sd-gpio22-conflict-fix.md`
- 2026-09-21 21:48 -- 新版 MicroSD GPIO22 修复完成静态复核：确认不再调用 `esp_vfs_fat_sdspi_mount()`，手动流程覆盖 SDSPI 初始化、卡初始化、FATFS 挂载、失败清理和成功卸载；未运行项目禁止的 ESP-IDF 构建、烧录或串口监视，实机结果待补。→ `goals/20260921-2101-sd-gpio22-conflict-fix.md`
- 2026-09-21 11:49 -- 节点 10 最终验收：项目负责人确认此前暂缓项与完整手动回归均通过；已有逐项日志继续保留，新确认部分为人工口头证据、无新增日志或截图。Agent 未运行项目禁止的构建／烧录／Monitor，仅完成静态复核。NVS 提交失败与 Service 初始化失败故障注入仍只按源码检查确认，不阻塞收口。→ `goals/20260920-2210-wifi-service.md`
- 2026-09-20 — Launcher 左右切换自测固件（首版模型）通过：`LAUNCHER_SELF_TEST: PASS`，引导路径 16 步，八种数量边界与失败路径均通过。该结论随同日模型修订失效。→ `goals/20260920-2015-launcher-lr-paging.md`
- 2026-09-20 20:16 — 节点 9 收尾复核通过（用户确认开发与手动测试全部完成，Agent 复核静态检查）。→ `goals/20260920-1429-settings-service-nvs.md`
- 2026-09-20 — 节点 9 Settings UI 与五 App 回归通过：四项详情页文案与布局、Wi-Fi／Sound 页无开关、五 App 进入／返回均正常。**人工口头确认，无补充串口日志。** → 同上
- 2026-09-20 — 节点 9 自测固件 6 步通过，输出 `SETTINGS_SERVICE_SELF_TEST: PASS`。→ 同上
- 2026-09-20 19:34 — 节点 9 普通构建首启与重启持久化部分通过：首启 `source=defaults`，断电重启后 `source=nvs`，两次均到 `Launcher ready, 5 app(s) registered`。→ 同上
- 2026-09-20 — 节点 9 实现后静态检查通过（括号配平、空白／ASCII、NVS 调用范围、IDF API 行为核对）。运行时当时全部未验证。→ 同上
- 2026-09-20 14:08 — 节点 8 收尾确认：人工确认手动测试全部通过，补齐首轮日志未覆盖的 Games 与 PC Monitor 进入／返回，结论为功能验收无待办。**无补充串口日志。** → `goals/20260920-1338-settings-ui.md`
- 2026-09-20 14:03 — 节点 8 实机验证通过（含一处口径差异，人工确认接受）。→ 同上
- 2026-09-20 13:45 — 节点 8 实现后静态检查通过。→ 同上
- 2026-09-20 13:28 — 节点 7 实机验证通过（含轮次口径差异，人工确认接受）。→ `goals/20260920-1258-tools-app.md`
- 2026-09-20 13:16 — 节点 7 实现后静态检查通过。→ 同上
- 2026-09-20 12:52 — 节点 6 实机验证通过（含轮次口径差异，人工确认接受）。→ `goals/20260920-1235-pc-monitor-ui.md`
- 2026-09-20 12:42 — 节点 6 实现后静态检查通过。→ 同上
- 2026-09-20 12:29 — 节点 5 实机验证通过（含两处数量口径差异，人工确认接受）。→ `goals/20260920-1159-games-placeholder.md`
- 2026-09-20 12:15 — 节点 5 实现后静态检查通过（含发现并修正 `INCLUDE_DIRS` 阻塞性缺口）。→ 同上
- 2026-09-20 11:59 — 节点 5 施工文档复核通过。→ 同上
- 2026-09-20 11:46 — 节点 4 收口判定：人工确认蜂鸣器发声正常、本机无电机硬件，据此判定“电机／蜂鸣器停止输出”条款无法构造，标记未验证且不阻塞收口。→ `goals/20260920-1053-hardware-test-app.md`
- 2026-09-20 11:43 — 节点 4 最终生命周期轮转与人工确认：16 轮配对、前 11 轮不中断、`screen children=2` 恒定。→ 同上
- 2026-09-20 11:32 — 节点 4 生命周期轮转与 MicroSD 缺失路径：19 轮严格配对，MicroSD 页按 A 三次失败三次后仍正常返回（SD／GPIO22 冲突首次实机复现）。→ 同上
- 2026-09-20 11:20 — 节点 4 检查点 1 实机验证通过。→ 同上
- 2026-09-20 11:18 — 节点 4 实现后静态检查通过，运行时当时未验证。→ 同上

## 更早节点

节点 0～3 的逐条验证记录在 2026-09-20 之前已迁出 ROADMAP，证据保留在各自施工文档：

- 节点 0 构建基线恢复 → `goals/20260919-1834-build-baseline.md`（交付内容、主 Agent 复现与用户实机确认）
- 节点 1 App Framework → `goals/20260919-2037-app-runtime.md`（验证证据）
- 节点 2 Navigation → `goals/20260920-0816-navigation.md`（普通构建回归证据、实施记录）
- 节点 3 Launcher → `goals/20260920-1007-launcher.md`（实现记录，含自测 PASS、首轮编译失败修复与视觉确认）
