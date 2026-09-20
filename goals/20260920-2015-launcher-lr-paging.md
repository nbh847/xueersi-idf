# Launcher 导航改为左右切换

## 元信息

- 对应节点：节点 3“Launcher”的导航行为变更（非设计文档第 15 节的有序节点）
- 状态：已实现，待人工验证
- 创建时间：2026-09-20 20:15（北京时间）
- 前置条件：节点 0～9 均已完成；节点 3 的 Launcher、节点 9 的 Settings Service 均已实机验证
- 触发来源：用户提出“五个 App 改成左右切换，往下不再是切页”
- 后续影响：本变更取代节点 3 决策 5 的“左右限本行、上下 ±2 跨页”规则。节点 3～9 的实机回归基线随之更新，需重新验证一轮 Launcher 相关行为

## 目标与预期行为

把 Launcher 的焦点移动从“2 列 × 2 行网格的四向移动”改为“左右方向的线性翻页”。

- `←` / `→` 是唯一的焦点移动键，按 Registry 注册顺序线性前进／后退一格。
- 焦点从本页最后一格（索引 3）继续按 `→`，落到下一页第一格（索引 4），即翻页；反向按 `←` 从索引 4 回到索引 3。第 2 页只有 1 个 App 时，索引 4 按 `→` 保持不变。
- 焦点在索引 0 按 `←`、在最后一个索引按 `→` 都保持不变，不循环。
- `↑` / `↓` 不再是导航键，按下不改变焦点、页码或任何对象。
- 页面布局不变：仍是 160 × 128 上 2 列 × 2 行共 4 格的固定网格，焦点顺序为行优先阅读顺序（索引 0 → 1 → 2 → 3）。翻页时整页重建，页内移动只重画受影响的两格。

本变更只改按键到目标的映射，不改网格几何、App 注册顺序、A 打开／B 返回语义、生命周期、Navigation 边界或任何 App 内部行为。

## 背景与文件入口

- `main/framework/xiaomiao_launcher.c` 的 `launcher_next_index()` 是唯一的方向→目标索引映射，`launcher_key_cb()` 是唯一的按键分发点，`launcher_move()` 负责页面对比与局部重绘。
- `main/framework/xiaomiao_launcher.h` 声明 `XIAOMIAO_LAUNCHER_COLUMNS/ROWS/PER_PAGE`，本变更不动这些常量：网格几何与每页容量不变。
- `main/framework/xiaomiao_launcher_selftest.c` 的 `s_steps[]` 引导路径与 `check_counts_auto()`／`check_moves_auto()`／`check_open_back_auto()` 中的断言直接编码了旧的四向规则，必须同步。
- `docs/xiaomiao_firmware_v0.1_design.md` 第 4 节的操作规则写的是 `↑ / ↓ / ← / →`：移动焦点。
- `docs/project-overview.md` 与 `ROADMAP.md` 记录的“2 列 × 2 行、上下跨页、焦点索引”描述需要同步。

## 范围边界

### 本节点必须完成

- `launcher_next_index()` 改为左右两向的线性 ±1，越界夹紧不循环。
- `launcher_key_cb()` 不再把 `↑` / `↓` 映射为焦点移动。
- 更新 `xiaomiao_launcher.c` 与 `.h` 中描述旧规则的注释，使其与新行为一致。
- 同步 `xiaomiao_launcher_selftest.c`：引导路径改为仅左右键；数量边界、自动断言与 App 打开态断言中依赖上下键的部分一并改写。
- 保留页内局部重绘优化、页边界整页重建、`create`／`destroy` 幂等、A 打开／B 返回与 App 打开态下方向键被忽略的既有行为。
- 同步 `docs/xiaomiao_firmware_v0.1_design.md`、`docs/project-overview.md` 与 `ROADMAP.md` 中的导航描述与状态。
- 提供人工验证命令与验收标准。

### 本节点禁止修改或实现

- 不改 `XIAOMIAO_LAUNCHER_COLUMNS/ROWS/PER_PAGE`，不改卡片几何、配色或字体。
- 不改 App Registry、App Manager、Navigation 或任何 App 的源码。
- 不改 A 打开、B 返回、Hardware Test 长按 B 或 Settings／Tools 两级 B 的语义。
- 不新增按键（例如用 A／B 翻页）、不新增动画、不新增滚动、不引入循环导航。
- 不改引脚、硬件协议、GD32 工程、分区表、依赖版本或 `dependencies.lock`。

## 实现决策

1. **导航模型**：Launcher 视为按注册顺序排列的单一线性序列，每页 4 格。`←`／`→` 是 ±1，跨过页边界即翻页。这比“保留网格四向移动、只把翻页从下键挪到右键”更简单，也符合用户“全部都换成左右”的要求；代价是索引 1 按 `→` 会到索引 2（下一行左格），视觉上按行优先阅读顺序前进。
2. **两端夹紧而非循环**：索引 0 按 `←`、末索引按 `→` 保持不变。与节点 3 决策 5 的“越界保持焦点”一致，避免用户长按方向键后在列表两端来回跳。
3. **上下键改为无操作**，而不是复用为左右或第二套翻页入口。用户明确要求“全部的切换方向都要换成左右”；保留第二套入口会让翻页有两个来源，也会让自测与文档出现两套并行规则。
4. **`launcher_move_t` 只保留 `LAUNCHER_MOVE_LEFT`／`LAUNCHER_MOVE_RIGHT`** 两个枚举值，删除 `_UP`／`_DOWN`。保留枚举（而非改成裸 `int`）是为了让 `launcher_key_cb()` 的分发意图仍然自解释。
5. **不改 `launcher_move()`／`launcher_render_page()`**：页边界对比与局部重绘逻辑与按键方向无关，新映射自然复用；页边界整页重建这一优化不因本次变更失效。
6. **不新增按键提示**：页脚现有提示为 `A open`，未提到方向键，因此本次不需要改文案，也不为了说明“现在只能左右”而新增提示。
7. **自测的引导路径改为 16 步**：`←`（起点保持）→ `↓`／`↑`（验证被忽略）→ 连续 `→` 到末格（含一次翻页与末端保持）→ 连续 `←` 回到索引 0。路径结束时必须回到焦点 0、页码 0，复用既有的 `traversal ends at index 0`／`page 0` 断言，不改自测的控制流。
8. **保留“App 打开态忽略方向键”的测试强度**：原断言用的是 `↓`，而现在 `↓` 在任何情况下都不动，断言失去区分度，因此改按 `→`——若 App 打开态的守卫失效，焦点会从 0 移到 1，断言即可捕获。

## 执行检查点

### 检查点 1：导航映射

- `launcher_next_index()` 只区分左右两向，越界返回原索引。
- `launcher_key_cb()` 中不存在把 `↑`／`↓` 交给 `launcher_move()` 的分支。
- `XIAOMIAO_LAUNCHER_COLUMNS/ROWS/PER_PAGE`、卡片几何与配色未改。

预期结果：静态检查即可确认映射规则单一、无残留四向逻辑。

### 检查点 2：自测路径与数量边界

- `s_steps[]` 只含 `←`／`→`／`↑`／`↓` 中的左右移动与上下忽略步骤，路径结束回到焦点 0、页码 0。
- `check_counts_auto()` 用 `→` 前进到末索引，并覆盖 0／1／2／3／4／5／7／16 八种注册数量下的“末索引可达且不越界”。
- 自测仍在数量边界、App 打开态、open／back 两轮等原有分组上跑完，`LAUNCHER_SELF_TEST: PASS` 标记不变。

预期结果：自测断言与新映射一一对应，不存在仍按旧规则断言的步骤。

### 检查点 3：实机行为

- 自测固件输出 `LAUNCHER_SELF_TEST: PASS`，引导路径步数与原“15 步”不同属预期。
- 普通固件按 `→` 从第 1 格逐格走到第 4 格后进入第 2 页的第 5 个 App；按 `←` 反向回到第 1 页。
- 在索引 0 按 `←`、在第 2 页末尾按 `→` 均无反应。
- 按 `↑`／`↓` 焦点不动。
- A 打开焦点 App、B 返回后焦点与页码保持；五个 App 与 Hardware Test 15 页无回归。

预期结果：左右切换连续可达全部 5 个入口，上下键不再切页，既有打开／返回与生命周期行为不变。

### 检查点 4：文档交付

- 设计文档第 4 节的按键规则改为左右移动焦点、上下不参与导航。
- `docs/project-overview.md` 与 `ROADMAP.md` 的 Launcher 描述与新行为一致。
- 明确记录未验证范围：自测固件与普通固件的实机验证均未执行。

预期结果：文档不残留“上下跨页”的描述，也没有把未执行的验证写成通过。

## 失败路径与处理要求

- 注册表为空：`←`／`→`／`↑`／`↓` 全部无操作，焦点与页码保持 0，不创建 Navigation 根对象。
- 焦点索引越界（不应发生）：`launcher_move()` 保持原索引并返回，不重建页面。
- 翻页失败（页面对比异常）：不作为预期路径；若出现，必须表现为焦点不变而非删除对象。
- 自测任一断言失败：输出 `LAUNCHER_SELF_TEST: FAIL ...` 并 abort，不得跳过。

## 验收标准

> 复选框在实机验证完成前一律保持未勾选。

- [ ] `←`／`→` 是 Launcher 中唯一的焦点移动键，按注册顺序线性 ±1。
- [ ] 索引 3 按 `→` 进入第 2 页的索引 4；索引 4 按 `←` 回到索引 3。
- [ ] 索引 0 按 `←`、末索引按 `→` 均保持不变，不循环。
- [ ] `↑`／`↓` 不改变焦点、页码或对象数量。
- [ ] 网格几何、每页容量、卡片布局与配色未改。
- [ ] 页内移动只重画受影响的两格，翻页整页重建的既有优化保留。
- [ ] A 打开、B 返回、App 打开态忽略方向键、create／destroy 幂等均无回归。
- [ ] 自测固件输出 `LAUNCHER_SELF_TEST: PASS`，且引导路径与断言已按新规则改写。
- [ ] 普通固件五个 App 与 Hardware Test 15 页无回归。
- [ ] 设计文档、`docs/project-overview.md` 与 `ROADMAP.md` 已同步真实状态。
- [ ] 人工使用 ESP-IDF 6.1 完成自测构建与普通构建的编译、烧录与实机验证。

## 人工验证命令与预期结果

Agent 只执行源码、配置与 diff 静态检查，不主动运行以下命令。人工在已加载 ESP-IDF 6.1 的环境中执行。

### 自测固件（Launcher 导航规则）

```bash
idf.py -B .tmp/build-launcher-selftest/build -D SDKCONFIG=$(pwd)/.tmp/build-launcher-selftest/sdkconfig -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.ci" -D XIAOMIAO_LAUNCHER_SELF_TEST=ON -D XIAOMIAO_FRAMEWORK_SELF_TEST=OFF -D XIAOMIAO_NAVIGATION_SELF_TEST=OFF -D XIAOMIAO_SETTINGS_SERVICE_SELF_TEST=OFF set-target esp32
idf.py -B .tmp/build-launcher-selftest/build -D SDKCONFIG=$(pwd)/.tmp/build-launcher-selftest/sdkconfig -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.ci" -D XIAOMIAO_LAUNCHER_SELF_TEST=ON build
idf.py -B .tmp/build-launcher-selftest/build -p COM5 flash monitor
```

预期：

- 自动断言部分先输出 `automatic checks passed`，其中引导路径步数为新的步数（非 15）。
- 屏幕按提示按完引导路径后输出 `guided traversal done`，随后 3 轮 A 打开／B 返回输出 `round 1/3 done` → `round 2/3 done` → `round 3/3 done`。
- 最终唯一标记 `LAUNCHER_SELF_TEST: PASS`。

### 普通固件（实机导航）

```bash
idf.py build
idf.py -p COM5 flash monitor
```

预期：

- 启动链输出 `launcher created (5 apps)` 与 `Launcher ready, 5 app(s) registered`。
- 按 `→` 依次经过 Games → PC Monitor → Tools → Settings，再按一次 `→` 跳到第 2 页的 Hardware Test，页码由 `1/2` 变为 `2/2`。
- 按 `←` 逐格回退，越过第 1 格边界与第 2 页边界时焦点保持。
- 按 `↑`／`↓` 焦点不动。
- A 进入焦点 App、B 返回后焦点与页码保持；Hardware Test 15 页可达。

## 静态检查要求

实现后至少检查：

```bash
rg -n "LAUNCHER_MOVE_UP|LAUNCHER_MOVE_DOWN|LV_KEY_UP|LV_KEY_DOWN" main/framework/xiaomiao_launcher.c main/framework/xiaomiao_launcher.h
rg -n "launcher_next_index|LAUNCHER_MOVE_LEFT|LAUNCHER_MOVE_RIGHT" main/framework/xiaomiao_launcher.c
rg -n "LV_KEY_UP|LV_KEY_DOWN" main/framework/xiaomiao_launcher_selftest.c
```

预期：`launcher.c`／`.h` 内不再出现 `LAUNCHER_MOVE_UP`／`LAUNCHER_MOVE_DOWN`，且 `LV_KEY_UP`／`LV_KEY_DOWN` 只出现在按键分发的显式忽略位置（若保留）或完全不出现；自测中保留的上／下按键步骤只用于断言“焦点不变”。

## 实现记录（2026-09-20）

### 修改文件

- `main/framework/xiaomiao_launcher.c`
  - `launcher_move_t` 只保留 `LAUNCHER_MOVE_LEFT`／`LAUNCHER_MOVE_RIGHT`，删除 `_UP`／`_DOWN`。
  - `launcher_next_index()` 重写为线性 ±1：`←` 在索引 0 夹紧，`→` 在末索引夹紧，其余为 ±1；删除列号计算与四向 `switch`。
  - `launcher_key_cb()` 删除 `LV_KEY_UP`／`LV_KEY_DOWN` 两个 case，在 `default` 处加注释说明上下键不是导航键。
  - `launcher_move()`、`launcher_render_page()`、`launcher_slot_*`、卡片几何与配色全部未改，页边界整页重建与页内局部重绘逻辑原样复用。
- `main/framework/xiaomiao_launcher.h`：头部契约注释新增“Navigation model”一节，说明左右线性、行优先顺序、两端夹紧与上下键不参与导航。
- `main/framework/xiaomiao_launcher_selftest.c`
  - `s_steps[]` 由 15 步四向表改为 16 步左右表：`←` 起点夹紧 → `↓`／`↑` 被忽略 → 连续 `→` 到索引 6（含索引 3→4 翻页与索引 6 夹紧）→ 连续 `←` 回到索引 0。
  - `check_moves_auto()`：边界断言改为“left at the start stays”“up is ignored”“down is ignored”。
  - `check_counts_auto()`：改用 `→` 从索引 0 走到末索引（`for i = 1; i < n`），补“right at the end stays”“vertical keys are ignored”，`n > 1` 时补左右往返一格，避免 `n == 1` 时断言不成立。
  - `check_open_back_auto()`：App 打开态的方向键断言由 `↓` 改为 `→`（决策 8）。
  - 引导路径的控制流、`XM_STEP_COUNT` 计数、PASS 标记与 3 轮 A／B 往返均未改。
- `docs/xiaomiao_firmware_v0.1_design.md` 第 4 节：按键规则由 `↑ / ↓ / ← / →` 移动焦点改为左右移动焦点并按 4 格翻页、上下不参与导航。
- `docs/project-overview.md`：启动链的 Launcher 行与“目标架构与演进约束”的 Launcher 描述补充左右移动焦点与翻页。
- `ROADMAP.md`：当前状态补导航模型、当前开发节点补本变更条目、下一步补待验证项；逐条实现记录写入 `goals/ROADMAP-history.md` 的施工记录。

### 静态检查结果（Agent 执行，未编译）

- `main/framework/xiaomiao_launcher.c` 与 `.h` 内不再出现 `LAUNCHER_MOVE_UP`／`LAUNCHER_MOVE_DOWN`／`LV_KEY_UP`／`LV_KEY_DOWN`（检索命中数为 0）。
- 花括号配平：`xiaomiao_launcher.c` 49/49、`xiaomiao_launcher.h` 1/1、`xiaomiao_launcher_selftest.c` 60/60。
- 三个文件无行尾空白、无 Tab、无非 ASCII 字符。
- 改动范围核对：`git status` 确认只修改 `main/framework/xiaomiao_launcher.c`、`.h`、`xiaomiao_launcher_selftest.c` 与四个文档；未修改 `main/apps/`、`main/services/`、`main/main.c`、`main/CMakeLists.txt`、Navigation、App Registry／Manager、GD32、硬件协议、引脚、`sdkconfig.*` 或 `dependencies.lock`。
- 自测步骤表与主流程注册数量核对：`XM_MAIN_APP_COUNT = 7`，步骤表最大目标索引 6、最大页码 1，均在范围内；路径末步回到索引 0／页码 0，与既有 `traversal ends at index 0`／`page 0` 断言一致。
- 数量边界逐项核对：`n` 取 0／1／2／3／4／5／7／16 时，`→` 连按 `n-1` 次均落在索引 `n-1`，页码为 `(n-1)/4`；`n == 1` 时跳过左右往返分支，避免 `n-2` 下溢。
- 按 `AGENTS.md` 的分工，未执行 `idf.py`、`set-target`、烧录、Monitor 或任何目标板操作。

## 当前交付与未验证范围

- 已完成：范围、导航模型、边界与失败路径决策、检查点与验收标准；Launcher 源码与自测同步；四个文档同步；上述静态检查。
- 尚未验证：Launcher 自测固件的编译与实机按键路径（`LAUNCHER_SELF_TEST: PASS`）、普通固件的左右切换实机行为、两端夹紧与上下键无反应的实机确认、A／B 语义与五个 App、Hardware Test 15 页回归。编译、烧录与串口监视按项目分工由人工执行。
- 节点状态：已实现，待人工验证；不得因源码完成而视为已通过。
