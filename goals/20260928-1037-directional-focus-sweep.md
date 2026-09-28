# Goal：方向键焦点切换加入光带扫入动效

状态：已完成收口（2026-09-28 13:02）。Launcher 与 Settings 于首轮实机验收通过；Tools 补接入后由项目负责人烧录复验确认「没问题了，我测过了」，并附串口日志（Launcher／PC Monitor／Settings／Tools 开合正常，`screen children=2` 恒定，无新增错误）。收尾清理修复后，负责人再次确认构建、烧录通过，Settings／Tools 连续快速按键光带正常；该轮未附新增日志或视频。动效参数保持初始值（140 ms），实机未要求调整。

创建时间：2026-09-28 10:37（北京时间）

## 目标

在 Launcher 与 Settings 的方向键选项焦点切换中加入短促的「光带扫入」反馈。按键被接受时立即更新逻辑焦点和蓝底白框，光带只在新选项内部扫过；动画结束后维持现有高对比静态焦点。用户可以在动画过程中继续按方向键或按 A，最终可操作项始终与视觉焦点一致。

设计依据为 `docs/directional-focus-transition-design.md`，五种候选的浏览器对比预览为 `docs/focus-transition-effects-preview.html`。预览只证明方案可比较，不是固件实现或实机验收证据。当前状态以 `ROADMAP.md` 为准。

## 边界与依赖

- 原定范围为 `main/framework/xiaomiao_launcher.c` 的同页四向移动及跨页进入，以及 `main/apps/settings/xiaomiao_settings.c` 当前顶层菜单、Wi-Fi 可操作项的上下移动。首轮实机反馈后，补接入 `main/apps/tools/xiaomiao_tools.c` 的现有四项菜单上下移动；原因、实现与复验见下文交付记录。三处均按现有事件入口接入，不改变焦点索引、按键映射、注册顺序、分页算法、A／B 语义或设置持久化。
- Settings 后续新增的 System 子菜单、Wi-Fi 连接详情随其独立设计实施时再接入；Tools 番茄时钟内部选项按其独立设计验收。本 Goal 不改变 PC Monitor、其他 App 的整页切换，也不改变 Hardware Test 15 页导航。
- 空闲待机画面尚未实施；本 Goal 不为它增设输入或动画状态。日后实施待机时，唤醒键被吞掉、原焦点直接稳定显示，不补播动效。
- 复用项目现有 LVGL 9.5 和 UI 任务。实施前核对本机组件源码中的动画、裁剪和对象生命周期 API；不新增第三方依赖、FreeRTOS 任务、全屏显示缓冲或公共模块，除非现有代码中出现可证明的重复。
- 硬件无关：不改引脚、GD32、Wi-Fi Service、NVS、分区表和任何外部协议。浏览器预览文件不进入固件构建链。

## 已确定的行为

1. 只在目标焦点与旧焦点不同且目标存在时播放。到达网格或菜单边界、方向键无效时不播放。
2. 接受按键时立即将旧项恢复普通样式、新项设为蓝底白框，并同步更新逻辑索引。窄亮带从旧项所在方向一侧扫入新项，在新项内部裁剪，不做全屏闪光。初始时长 110～160 ms，宽度与亮度由实机视频调整。
3. A 在动画中打开最新焦点对应项；进入 App、切换 Settings 页面或 B 返回前，停止动画并释放临时对象。快速连按时取消前一段，在最新目标重新播放；不能排队导致视觉落后。
4. Launcher 跨页先按现有导航规则确定目标、填充新页、更新页码及蓝底白框，再在目标卡片内播放进入版光带；无效跨页不动。保留“同行优先、缺行回退”的既有语义。
5. LVGL 动画或临时对象创建失败时仍显示正确静态焦点，按键可继续使用；关闭或重建页面后不回调已删除对象，不留下额外对象。目标板负载若使动效不稳定，记录现象与数据后再决定如何调整，不擅自更换效果。

## 施工检查点与验证

### CP1：Launcher 光带样例

- 在现有焦点变更点接入仅限目标卡片的光带，静态焦点先更新；同页左右／上下、边界无效键、快速连按、A 中断与 B 往返均须保持索引一致。
- 跨页先更新卡片和页码，再播放目标卡片的进入光带；五个现有 App 与 Hardware Test 入口可达，缺行回退不变。
- Agent 静态核对本机 LVGL 9.5 API、对象清理和 `git diff --check`；负责人在目标板录制按键短视频，对比当前静态基线并反馈清晰度、闪烁和刷新表现。

### CP2：Settings 顶层与 Wi-Fi 选项

- 顶层四项及 Wi-Fi 可操作项只在焦点真实变化时触发相同光带。切换详情、进入配网页、取消／完成配网、关闭 Settings 时停止动画并清理对象。
- 快速连按、动画中按 A／B、中文显示与英文回退、重复开合均保持原有行为。Settings 新增页面不在本 Goal 中实施。
- Agent 检查页面状态和释放路径；负责人在目标板检查菜单与 Wi-Fi 页面，并观察串口无新增错误。

### CP3：人工构建与整体回归

- 项目负责人从仓库根目录、已加载 ESP-IDF 6.1 的环境执行 `idf.py -p COM5 flash monitor`；`flash` 会按需构建，无需预先另跑 `idf.py build`。预期构建和烧录通过、启动到 Launcher，五 App 与 Hardware Test 正常进入和返回，焦点移动与页码正确。
- 对 Launcher 和 Settings 各录制普通按键及快速连按短视频，核对光带只在目标项内、静态焦点立即可见、最终目标不滞后；检查显示无明显闪烁、内存或 LVGL 错误日志无新增异常。目标板上再决定最终时长、宽度和亮度。
- Agent 只执行源码、配置、diff 静态检查并复核人工证据；不主动执行 `idf.py build`、`set-target`、`flash`、`monitor`、`esptool` 或占用串口。

## 验收标准

- [x] Launcher 同页与跨页移动、Settings 顶层与 Wi-Fi 操作项均呈现选定的 B 光带；无效方向键不播放。（2026-09-28 首轮实机确认）
- [x] 逻辑焦点和静态蓝底白框立即更新，A 始终打开最新焦点；快速连按、A／B 中断、页面关闭后无旧光带、悬空回调或对象累积。（同上；串口日志三次 App 开合 `screen children=2` 恒定）
- [x] 五 App、Launcher 分页和缺行回退、Settings 配网、Hardware Test B 语义无回归；中文／英文状态下无文字遮挡。（串口日志显示 Tools／Settings／PC Monitor 开合与字体 READY 正常；光带为行内临时对象，不改动文案渲染路径）
- [x] Agent 静态检查通过，项目负责人完成 ESP-IDF 6.1 构建、烧录与目标板按键／视觉验收，并提供足以判定效果的视频或明确结果。（首轮「移动的特效都没问题」+ Tools 复验「没问题了，我测过了」附串口日志；未附动效视频，效果判定以负责人目视为准）
- [x] 完成后在本 Goal 记录实际改动、实机参数、验证时间与证据及未验证范围，按实际状态同步 `ROADMAP.md` 与 `goals/ROADMAP-history.md`；若当前行为说明受影响，同步 `docs/project-overview.md`。（已完成）

## 当前交付与未验证范围

- 已交付（2026-09-28 12:33 更新）：选定效果的设计决策、施工文档与固件实现。
  - `main/framework/xiaomiao_launcher.c`：新增 `s_sweep_band` 与 `launcher_band_stop()`／`launcher_band_completed_cb()`／`launcher_band_start()`。光带为临时光带子对象（12 px 宽或高，浅白 `0xE8F0FF`，radius 1），挂在目标卡片内、由父对象默认裁剪，140 ms `ease_out` 扫过一次后自删。方向语义为光带从旧项所在一侧扫入：`LEFT` 从右缘向左扫、`RIGHT` 从左缘向右扫、`UP` 从底缘向上扫、`DOWN` 从顶缘向下扫。`launcher_move()` 在有效移动时先 `launcher_band_stop()`，同页更新两卡样式后播放；跨页先 `launcher_render_page()` 再向目标卡播放进入版光带。`launcher_activate()` 与 `launcher_destroy()`、`launcher_render_page()` 均先停止并删除动画（`lv_anim_delete` 先于 `lv_obj_delete`），无悬空回调。
  - `main/apps/settings/xiaomiao_settings.c`：同构的 `settings_band_stop()`／`settings_band_completed_cb()`／`settings_band_start()`，光带 8 px 高、行宽 `LV_PCT(100)`。`settings_menu_move()` 与 `settings_wifi_move()` 在高亮更新后播放，`from_top = step > 0`（下移从顶缘进入）；`settings_show_view()` 在 `lv_obj_clean()` 前、`settings_close()` 在删除前停止动画。
  - `main/apps/tools/xiaomiao_tools.c`（2026-09-28 12:33 补接入，实机反馈 Tools 菜单无特效）：与 Settings 完全同构的 `tools_band_stop()`／`tools_band_completed_cb()`／`tools_band_start()`（8 px、`LV_PCT(100)`、140 ms），`tools_menu_move()` 在高亮后播放；`tools_show_view()` 在 `lv_obj_clean()` 前、`tools_close()` 在删除前停止动画。Tools 的 Wi-Fi／System Info／About／Assets 详情页没有选项焦点，不播放。三处实现暂不提取共享模块：Launcher 的光带含水平方向且挂在卡片上，而 Tools 归位设计（待审）落地后其菜单将只剩单入口，届时重复自然消解；若归位不落地再考虑提取。
  - 实机反馈（2026-09-28，项目负责人）：「移动的特效都没问题」，即 Launcher 与 Settings 的光带已通过目标板目视验收；未附视频或串口日志，证据类型为人工口头确认。同轮反馈 Tools 菜单上下切换无特效，属原范围外缺口，已补接入。
  - 收口验证（2026-09-28 12:50，项目负责人）：Tools 补接入后烧录复验确认「没问题了，我测过了」，附串口日志——启动到 Launcher（5 apps）、字体 READY、Agent 发现正常，PC Monitor／Settings／Tools 依次开合各一次，`screen children=2` 恒定，日志无新增错误（`cmd=52`／`cmd=5` 的 R1 为 SD 探测预期噪声）。
  - 行为保证（静态复核）：只在焦点真实变化时播放；静态蓝底白框与逻辑索引先于动画更新；快速连按取消前段、在最新目标重播；A 在动画中打开最新焦点；LVGL 对象分配失败只跳过装饰、焦点不受影响。API 依据本机 `managed_components/lvgl__lvgl`：`lv_anim_delete(var, NULL)` 删除该对象全部动画（lv_anim.c:186）；`completed_cb` 在动画移出链表后调用、回调内删对象安全（lv_anim.c:691）；子对象默认裁剪到父对象（`LV_OBJ_FLAG_OVERFLOW_VISIBLE` 未设置）。
  - 静态检查：`git diff --check` 通过；未执行固件编译。
- 2026-09-28 12:57 收尾静态复核发现并修正快速连按的清理路径：Settings／Tools 启动新光带前原本未停止上一条动画，`s_sweep_band` 被覆盖后旧光带可能留在旧菜单行中；现两处均先执行 `*_band_stop()`。同时三个入口在 `lv_anim_start()` 返回 `NULL` 时删除刚创建的光带对象，保持静态焦点不受动画创建失败影响。核对本机 LVGL 9.5 `lv_anim_start()` 的失败返回与 `lv_anim_delete()` 行为，`git diff --check` 通过。项目负责人于 13:02 确认该修复版构建、烧录通过，Settings／Tools 连续快速按键时光带正常；该轮未附新增视频或日志。
- 未验证：无阻塞项。动效参数保持初始值（140 ms，Launcher 12 px、Settings／Tools 8 px，颜色 `0xE8F0FF`），负责人实机确认效果后未要求调整；动效清晰度／闪烁的逐帧视频判定未做，以负责人目视为准。无效方向键不播放、快速连按等行为已在首轮实机覆盖。
- 人工验证入口：从仓库根目录、已加载 ESP-IDF 6.1 的环境执行 `idf.py -p COM5 flash monitor`（`flash` 会按需构建）。验收要点见上文 CP1～CP3。
