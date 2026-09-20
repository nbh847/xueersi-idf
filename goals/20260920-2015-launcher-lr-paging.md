# Launcher 导航改为网格 + 右键翻页

## 元信息

- 对应节点：节点 3“Launcher”的导航行为变更（非设计文档第 15 节的有序节点）
- 状态：已完成（2026-09-20；模型经两次修订，最终版自测固件输出 `LAUNCHER_SELF_TEST: PASS`，普通固件四方向导航由人工确认“符合要求”）
- 创建时间：2026-09-20 20:15（北京时间）
- 前置条件：节点 0～9 均已完成并实机验证
- 触发来源：用户提出“五个 App 改成左右切换，往最右边按才切到第二页，而不是往下切页”，随后明确“上下键也是有用的”
- 后续影响：本变更取代节点 3 决策 5 的“左右限本行、上下 ±2 可跨页”规则；节点 3～9 的 Launcher 回归基线随之更新，需重新验证一轮

## 目标与预期行为

在保留 2 列 × 2 行网格的前提下，把翻页键从 `↓` 改到 `→`：上下负责页内换行，左右负责换列并在外侧列翻页。

- 页面布局不变：160 × 128 上 2 列 × 2 行共 4 格的固定网格，格位与索引仍为行优先（槽 0／1 在第 1 行，槽 2／3 在第 2 行）。
- `←` / `→` 在当前行内换列：索引 0 ↔ 1、索引 2 ↔ 3。
- 在右列继续按 `→` 翻页，且**保持在同一条行**：从第 1 行右列翻到下一页第 1 行第一格，从第 2 行右列翻到下一页第 2 行第一格。目标格不存在时（例如下一页只有 1 行）保持焦点不变。
- 在左列继续按 `←` 反向翻页，同样保持同一行：从第 1 行左列翻回上一页第 1 行第二格，从第 2 行左列翻回上一页第 2 行第二格。
- `↑` / `↓` 在当前页内换行：索引 0 ↔ 2、索引 1 ↔ 3；上下键不会翻页。
- 其余越出网格的移动（第 1 行按 `↑`、最后一行按 `↓`）保持焦点不变，不循环。
- 每页 4 格时，从本页第 1 格起按 `→` 两次即翻页：第一次到右列，第二次离开本页。

本变更只改按键到目标的映射，不改网格几何、每页容量、App 注册顺序、A 打开／B 返回语义、生命周期、Navigation 边界或任何 App 内部行为。

## 背景与文件入口

- `main/framework/xiaomiao_launcher.c` 的 `launcher_next_index()` 是唯一的方向→目标索引映射，`launcher_key_cb()` 是唯一的按键分发点，`launcher_move()` 负责页面对比与局部重绘。
- `main/framework/xiaomiao_launcher.h` 声明 `XIAOMIAO_LAUNCHER_COLUMNS/ROWS/PER_PAGE`，本变更不动这些常量。
- `main/framework/xiaomiao_launcher_selftest.c` 的 `s_steps[]` 引导路径与 `check_counts_auto()`／`check_moves_auto()`／`check_open_back_auto()` 中的断言直接编码了旧的导航规则，必须同步。
- `docs/xiaomiao_firmware_v0.1_design.md` 第 4 节的操作规则需要同步。
- `docs/project-overview.md`、`ROADMAP.md` 与 `goals/ROADMAP-history.md` 的导航描述与状态需要同步。

## 范围边界

### 本节点必须完成

- `launcher_next_index()` 改为网格映射：左右换列并在外侧列翻页，上下页内换行，越界夹紧不循环。
- `launcher_key_cb()` 恢复对 `↑` / `↓` 的分发（它们仍是导航键）。
- 更新 `xiaomiao_launcher.c` 与 `.h` 中描述规则的注释，使其与实现一致。
- 同步 `xiaomiao_launcher_selftest.c`：引导路径覆盖四个方向与两侧翻页；数量边界改为按网格规则走到末索引；App 打开态断言保留左右键的被忽略验证。
- 保留页内局部重绘优化、页边界整页重建、`create`／`destroy` 幂等、A 打开／B 返回与 App 打开态下方向键被忽略的既有行为。
- 同步 `docs/xiaomiao_firmware_v0.1_design.md`、`docs/project-overview.md`、`ROADMAP.md` 与 `goals/ROADMAP-history.md` 中的导航描述与状态。
- 提供人工验证命令与验收标准。

### 本节点禁止修改或实现

- 不改 `XIAOMIAO_LAUNCHER_COLUMNS/ROWS/PER_PAGE`，不改卡片几何、配色或字体，不改每页 4 格的容量。
- 不改槽位与索引的行优先对应关系（不改成纵向填充）。
- 不改 App Registry、App Manager、Navigation 或任何 App 的源码。
- 不改 A 打开、B 返回、Hardware Test 长按 B 或 Settings／Tools 两级 B 的语义。
- 不新增按键、不新增动画、不新增滚动、不引入循环导航。
- 不改引脚、硬件协议、GD32 工程、分区表、依赖版本或 `dependencies.lock`。

## 实现决策

1. **保留 2 列 × 2 行网格与行优先槽位**：`←` / `→` 换列，`↑` / `↓` 换行，格位与索引的对应关系不变，因此卡片几何、分页容量与既有布局证据都不受影响。
2. **翻页键改为 `→`，条件是在右列，目标是下一页同一行第一格**：这直接满足“往最右边按才切页、不是往下切页”，并满足“上排翻页到第二页上排、下排翻页到第二页下排第一个”。目标格不存在时保持焦点，因此 5 个 App 时下排按 `→` 不会跳到第 2 页（第 2 页只有 1 行）。代价是页内第 2 行的入口（如 Tools、Settings）不在 `→` 的路径上，需要用 `↓` 到达——这正是用户要求保留上下键的原因。
3. **`←` 取对称规则**：从第 p 页第 r 行左列翻回第 p-1 页第 r 行第二格。上一页只要存在就是满页，因此该格必然存在，不需要额外的存在性判断。
4. **上下键不翻页**：`↓` 只在同一页内向下换行，`↑` 只在同一页内向上换行。若允许上下跨页，翻页就会有两个入口，与决策 2 冲突。
5. **两端夹紧而非循环**：与节点 3 决策 5 的“越界保持焦点”一致，避免长按方向键后在首尾来回跳。
6. **`launcher_move_t` 保留四个方向**，`launcher_next_index()` 用 `XIAOMIAO_LAUNCHER_COLUMNS/ROWS` 计算列、行与页基址，不写死 2。
7. **不改 `launcher_move()`／`launcher_render_page()`**：页边界对比与局部重绘逻辑与按键方向无关，新映射自然复用。
8. **自测引导路径改为 13 步**：测试主流程注册 7 个 App（第 0 页 4 个、第 1 页 3 个，两页都有两行），路径覆盖首行与首列的夹紧、行内左右换列、两行各自的翻页与反向翻页、上下换行与上下夹紧，结束时回到焦点 0、页码 0，复用既有 `traversal ends at index 0`／`page 0` 断言。
9. **数量边界的“走到末索引”改为按网格规则显式构造路径**：每页两次 `→` 翻页，最后按末槽位（0／1／2／3）补 0／1／2／2 步，八种注册数量都能在不依赖“线性 ±1”的前提下到达末索引，并逐次断言焦点不越界、页码与焦点一致。
10. **保留“App 打开态忽略方向键”的测试强度**：仍用 `→`，因为从索引 0 按 `→` 在守卫失效时会移到索引 1。

## 执行检查点

### 检查点 1：导航映射

- `launcher_next_index()` 的左右分支只依赖列与页基址，上下分支只依赖行并且不改变页码。
- `launcher_key_cb()` 四个方向键都有对应分支。
- `XIAOMIAO_LAUNCHER_COLUMNS/ROWS/PER_PAGE`、卡片几何与配色未改。

预期结果：静态检查即可确认四条规则与决策 1～5 一一对应，且上下键不可能翻页。

### 检查点 2：自测路径与数量边界

- `s_steps[]` 覆盖四个方向、两侧翻页与四处夹紧，路径结束回到焦点 0、页码 0。
- `check_counts_auto()` 在 0／1／2／3／4／5／7／16 八种注册数量下都能到达末索引，并逐次验证焦点不越界、页码与焦点一致。
- 自测仍在数量边界、App 打开态、open／back 两轮等原有分组上跑完，`LAUNCHER_SELF_TEST: PASS` 标记不变。

预期结果：自测断言与新映射一一对应，不存在仍按旧规则断言的步骤。

### 检查点 3：实机行为

- 自测固件输出 `LAUNCHER_SELF_TEST: PASS`，引导路径为 13 步。
- 普通固件从 Games 按 `→` 到 PC Monitor，再按 `→` 翻到第 2 页第 1 行的 Hardware Test，页码由 `1/2` 变为 `2/2`；按 `←` 翻回第 1 页第 1 行的 PC Monitor。
- 从 Tools 按 `→` 到 Settings；在此按 `↓` 无效（已是最后一行）；按 `←` 从第 2 页第 2 行反向翻回第 1 页第 2 行的 Settings（需要第 2 页存在第 2 行）。
- 5 个 App 时第 2 页只有 1 行，因此从第 1 页第 2 行右列（Settings）按 `→` **不应**跳到第 2 页，焦点保持。
- 按 `↓` 从 Games 到 Tools、从 PC Monitor 到 Settings；`↓` 在最后一行无效、`↑` 在第一行无效，两者都不改变页码。
- A 打开焦点 App、B 返回后焦点与页码保持；五个 App 与 Hardware Test 15 页无回归。

预期结果：四个方向键都有明确作用，翻页只由左右键在最外侧列触发且保持同一行，既有打开／返回与生命周期行为不变。

### 检查点 4：文档交付

- 设计文档第 4 节的按键规则改为左右换列并在外侧列翻页、上下页内换行。
- `docs/project-overview.md`、`ROADMAP.md` 与 `goals/ROADMAP-history.md` 的描述与新行为一致。
- 明确记录未验证范围：普通固件的实机验证尚未执行。

预期结果：文档不残留“上下跨页”或“上下键不参与导航”的描述，也没有把未执行的验证写成通过。

## 失败路径与处理要求

- 注册表为空：四个方向键全部无操作，焦点与页码保持 0，不创建 Navigation 根对象。
- 单页注册（1～4 个 App）：`→` 在右列因没有下一页而保持焦点；`↓` 在最后一行保持焦点。
- 焦点索引越界（不应发生）：`launcher_move()` 保持原索引并返回，不重建页面。
- 翻页失败（页面对比异常）：不作为预期路径；若出现，必须表现为焦点不变而非删除对象。
- 自测任一断言失败：输出 `LAUNCHER_SELF_TEST: FAIL ...` 并 abort，不得跳过。

## 验收标准

> 勾选依据见“验证结果”。自测固件有串口日志；普通固件的四方向导航为人工确认，未提供补充串口日志。

- [x] `←` / `→` 在行内换列：索引 0 ↔ 1、索引 2 ↔ 3。（自测第 3、12 步；人工确认）
- [x] 在右列按 `→` 翻到下一页**同一行**的第一格；目标格不存在时保持焦点。（自测第 4、8 步覆盖两行翻页；5 个 App 下排不翻页为人工确认）
- [x] 在左列按 `←` 翻回上一页**同一行**的第二格；第 1 页第 1 行左列按 `←` 保持焦点。（自测第 1、5、9 步）
- [x] 从第 1 行翻页落在下一页第 1 行，从第 2 行翻页落在下一页第 2 行。（自测第 4 步 → 索引 4 第 1 行、第 8 步 → 索引 6 第 2 行）
- [x] `↑` / `↓` 在页内换行（0 ↔ 2、1 ↔ 3），在任何情况下都不改变页码。（自测第 6、7、10、11 步；代码层面上下分支不含页基址与 `PER_PAGE`）
- [x] 第 1 行按 `↑`、最后一行按 `↓` 保持焦点不变。（自测第 2、7、11 步）
- [x] 每页 4 格时按 `→` 两次即翻页。（自测引导路径第 3→4 步即两次 `→`；人工确认）
- [x] 5 个 App 时从第 1 页第 2 行右列按 `→` 保持焦点（第 2 页没有第 2 行）。（人工确认）
- [x] 网格几何、每页容量、槽位与索引的行优先对应关系、卡片布局与配色未改。（静态检查：`COLUMNS/ROWS/PER_PAGE` 与卡片几何常量未动）
- [x] 页内移动只重画受影响的两格，翻页整页重建的既有优化保留。（`launcher_move()`／`launcher_render_page()` 未改）
- [x] A 打开、B 返回、App 打开态忽略方向键、create／destroy 幂等均无回归。（自测：`open 'self.app0' failed: ESP_ERR_INVALID_STATE`、两处 `destroy refused: an App is open`、3 轮 A／B）
- [x] 自测固件输出 `LAUNCHER_SELF_TEST: PASS`，且引导路径与断言已按本次规则改写。（`guided key path: 13 steps + 3 rounds` + `LAUNCHER_SELF_TEST: PASS`）
- [x] 普通固件五个 App 与 Hardware Test 15 页无回归。（人工确认）
- [x] 设计文档、`docs/project-overview.md`、`ROADMAP.md` 与 `goals/ROADMAP-history.md` 已同步真实状态。
- [x] 人工使用 ESP-IDF 6.1 完成自测构建与普通构建的编译、烧录与实机验证。（自测固件有完整串口日志；普通固件为人工确认，无补充串口日志）

## 人工验证命令与预期结果

Agent 只执行源码、配置与 diff 静态检查，不主动运行以下命令。人工在已加载 ESP-IDF 6.1 的环境中执行。

### 自测固件（Launcher 导航规则）

```bash
idf.py -B .tmp/build-launcher-lr/build -D SDKCONFIG=D:/WorkSpace/hardware/esp32-lab/xueersi-idf/.tmp/build-launcher-lr/sdkconfig -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.ci" -D XIAOMIAO_LAUNCHER_SELF_TEST=ON -D XIAOMIAO_FRAMEWORK_SELF_TEST=OFF -D XIAOMIAO_NAVIGATION_SELF_TEST=OFF -D XIAOMIAO_SETTINGS_SERVICE_SELF_TEST=OFF set-target esp32
idf.py -B .tmp/build-launcher-lr/build -D SDKCONFIG=D:/WorkSpace/hardware/esp32-lab/xueersi-idf/.tmp/build-launcher-lr/sdkconfig -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.ci" -D XIAOMIAO_LAUNCHER_SELF_TEST=ON build
idf.py -B .tmp/build-launcher-lr/build -p COM5 flash monitor
```

预期：

- 自动断言先输出 `automatic checks passed, guided key path: 13 steps + 3 rounds`（步数为 13）。
- 屏幕按提示按完引导路径后输出 `guided traversal done`，随后 3 轮 A 打开／B 返回输出 `round 1/3 done` → `round 2/3 done` → `round 3/3 done`。
- 最终唯一标记 `LAUNCHER_SELF_TEST: PASS`。

### 普通固件（实机导航）

```bash
idf.py build
idf.py -p COM5 flash monitor
```

预期：

- 启动链输出 `launcher created (5 apps)` 与 `Launcher ready, 5 app(s) registered`。
- 从 Games 按 `→` 到 PC Monitor，再按 `→` 翻到第 2 页选中 Hardware Test，页码由 `1/2` 变 `2/2`。
- 按 `←` 从 Hardware Test 翻回第 1 页第 1 行并选中 PC Monitor。
- 按 `↓` 从 Games 到 Tools、从 PC Monitor 到 Settings，页码保持 `1/2`。
- 从 Tools 按 `→` 到 Settings，再按 `→` **不应**翻页（第 2 页没有第 2 行），焦点保持。
- 在第 1 页第 1 行按 `↑`、在任意页最后一行按 `↓` 均无反应。
- A 进入焦点 App、B 返回后焦点与页码保持；Hardware Test 15 页可达。

## 静态检查要求

实现后至少检查：

```bash
rg -n "LAUNCHER_MOVE_UP|LAUNCHER_MOVE_DOWN|LAUNCHER_MOVE_LEFT|LAUNCHER_MOVE_RIGHT" main/framework/xiaomiao_launcher.c main/framework/xiaomiao_launcher.h
rg -n "launcher_next_index" -A 45 main/framework/xiaomiao_launcher.c
rg -n "LV_KEY_UP|LV_KEY_DOWN|LV_KEY_LEFT|LV_KEY_RIGHT" main/framework/xiaomiao_launcher.c main/framework/xiaomiao_launcher_selftest.c
```

预期：四个方向都有独立分支；**左右分支的翻页目标只由页基址 + `PER_PAGE` + 行号 × `COLUMNS` 组成**，上下分支只使用行号与 `COLUMNS/ROWS`、不含页基址或 `PER_PAGE` 运算（即上下键在代码层面不可能改变页码）。

## 验证结果

### 最终模型的自测固件（2026-09-20，人工执行、Agent 复核）-- 通过

人工用 `XIAOMIAO_LAUNCHER_SELF_TEST=ON` 在 `.tmp/build-launcher-lr/` 重新编译烧录后取得串口日志：

- `automatic checks passed, guided key path: 13 steps + 3 rounds` —— 步数为 **13**，与最终模型的 `s_steps[]` 一致，说明自测跑的是“保持同一行翻页”的路径。
- `guided traversal done`：实机按完 13 步，覆盖首行与首列夹紧、行内两向换列、第 1 行与第 2 行各自的 `→` 翻页与 `←` 反向翻页、上下换行与上下夹紧；焦点回到索引 0／页码 0，满足 `traversal ends at index 0`／`page 0` 断言。
- 八种数量边界（0／1／2／3／4／5／7／16）与失败路径（`open 'self.app0' failed: ESP_ERR_INVALID_STATE`、两处 `destroy refused: an App is open`）全部通过。
- 3 轮 A 打开／B 返回输出 `round 1/3 done` → `round 2/3 done` → `round 3/3 done`，最终唯一标记 `LAUNCHER_SELF_TEST: PASS`。
- 无 panic、无看门狗、无重启循环、无输入失效。

### 普通固件（2026-09-20，人工确认）-- 通过

人工在目标板上确认四个方向的导航行为符合要求，包含：右列按 `→` 翻页并落在下一页同一行、按 `←` 沿同一行翻回、`↓` 在页内换行且不改变页码、5 个 App 时第 1 页第 2 行按 `→` 不翻页，以及五个 App 与 Hardware Test 15 页无回归。

**证据边界**：本次只有人工口头确认（“测完了，符合要求”），未提供普通固件的补充串口日志，因此不补造 `open`／`closed` 时间戳、`screen children` 数值或焦点索引。

### 已作废的早期结论

首版“左右线性 ±1”模型（引导路径 16 步）曾于同日通过自测，其日志见 `goals/ROADMAP-history.md`。该结论随模型修订失效，不作为当前实现的证据。

## 当前交付与未验证范围

- 已完成：范围、导航模型、边界与失败路径决策、检查点与验收标准；Launcher 源码与自测已按最终模型改写；设计文档、`docs/project-overview.md`、`ROADMAP.md` 与 `goals/ROADMAP-history.md` 已同步；静态检查；自测固件的编译、烧录与 13 步实机按键路径；普通固件四方向导航的人工确认。
- 证据边界：自测固件有完整串口日志；普通固件为人工口头确认，无补充串口日志。
- 节点状态：已完成，功能验收无待办。

## 修订记录

- 2026-09-20（首版）：按“全部切换方向都换成左右”实现为**左右线性 ±1**——`←`／`→` 沿注册顺序逐格前进后退，越过本页第 4 格翻页，`↑`／`↓` 不再参与导航。自测引导路径 16 步，并通过实机验证（提交 `21b53eb`）。
- 2026-09-20（修订一）：用户反馈“不是依次选中每个 App，而是一排两个 App、按两次右就翻页”以及“上下键也是有用的”。首版把 4 格当作线性序列并删掉上下键，与网格语义不符。改为恢复上下键为页内换行、左右键为行内换列、翻页条件为“在右列按 `→`”，引导路径改为 17 步。
- 2026-09-20（修订二，最终）：用户补充“上排翻页应到第二页上排，下排翻页应到第二页下排的第一个”。修订一的翻页目标固定为下一页第一格，丢失了行信息。最终规则改为**翻页保持同一行**：右列按 `→` 落到下一页同一行第一格，左列按 `←` 落到上一页同一行第二格，目标格不存在时保持焦点。引导路径改为 13 步（7 个 App，两页都有两行，两行各自的翻页都能覆盖）。
- 验证状态：首版与修订一的自测结论均已作废；**最终模型（翻页保持同一行）已通过自测固件（13 步）并取得普通固件的人工确认**，验收标准全部勾选，详见“验证结果”。
