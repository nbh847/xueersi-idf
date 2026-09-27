# Launcher 顺序与 Tools／Settings 信息归位设计

状态：设计待审；仅文档，未修改固件，未构建或实机验证。2026-09-27 19:25。

## 目标

1. Launcher 第一页第三格显示 `Games`。
2. `Tools` 不再显示目前的 Wi-Fi、System Info、About、Assets。这四项都是设备状态、设置或诊断信息，应从 Settings 的对应位置访问。
3. `Tools` 仍保留 Launcher 入口，最终仅提供独立设计的「番茄时钟」（见 [番茄时钟设计](pomodoro-timer-design.md)），不再用 Wi-Fi 或系统信息充当工具。两个方案若分步实施，中间阶段可短暂显示“暂无工具”。

## 当前事实与目标位置

当前 `main/main.c` 按 `Games → PC Monitor → Tools → Settings → Hardware Test` 注册。Launcher 每页 2 列 × 2 行，注册顺序决定槽位，第一格是索引 0，第三格是索引 2。`Tools` 源码目前实际有 **四项**，其中 Assets 是节点 15 后加入的资源诊断页；不能只迁 Wi-Fi、System Info、About 三项。

| 现有入口／内容 | 目标入口／内容 | 处理 |
| --- | --- | --- |
| Launcher：Games 索引 0 | 第一页第三格，索引 2 | 改注册顺序；保留 `games` App ID、页面和行为 |
| Tools → Wi-Fi：状态、SSID、信号、IPv4 | Settings → Wi-Fi → 连接详情 | Settings 已有状态、自动连接、配网、忘记网络；连接详情补齐 Tools 独有的三个只读字段，使用 Wi-Fi Service 快照，不复制网络控制逻辑 |
| Tools → System Info：芯片、CPU 配置频率、Flash、PSRAM、ESP-IDF、固件版本 | Settings → System → 系统信息 | 保持原数据来源、`Unknown`／`None` 降级语义；“CPU”继续明确为配置频率，不声称运行时测量值 |
| Tools → Assets：资源分区、容量、使用量、字体、字形数、SD 状态 | Settings → System → 资源状态 | 继续从 Assets、Font、Storage Service 快照读取；资源或 SD 不可用时保留现有降级显示，不启动挂载操作 |
| Tools → About：项目、固件版本、作者、仓库标识 | Settings → System → 关于 | 保留现有信息与真实固件版本，不新增外部跳转 |
| Settings → System：配置来源与 NVS 错误摘要 | Settings → System → 配置状态 | 保留原有持久化状态信息，不因 System 变成子菜单而消失 |
| Tools 菜单 | 最终仅有「番茄时钟」 | 不显示四项旧菜单，也不复制 Settings 的入口；计时功能由独立设计定义 |

## 页面结构与交互

### Launcher

本次按最小顺序调整设计为：

```text
第一页，2 列 × 2 行       第二页
┌────────────┬────────────┐  ┌────────────┐
│ PC Monitor │ Tools      │  │ Hardware   │
│ 索引 0     │ 索引 1     │  │ Test，4    │
├────────────┼────────────┤  └────────────┘
│ Games      │ Settings   │
│ 索引 2     │ 索引 3     │
└────────────┴────────────┘
```

其余四个 App 保持原相对先后顺序。若监控 App 的独立额度方案随后把 `PC Monitor` 显示名改成「监控」，本设计只改变其位置，不依赖或覆盖该命名工作。Launcher 的左右翻页、上下换行、焦点边界与第二页 Hardware Test 保持现有语义；启动焦点随索引 0 落在 PC Monitor。

### Settings

- 顶层仍为 `Wi-Fi／Display／Sound／System` 四项，避免 160 × 128 屏幕增加第五行。Display 后续可承载 [待机画面设计](screen-idle-design.md) 的超时设置；本设计不实施那个功能。
- `Wi-Fi` 保留现有状态、自动连接、配网和忘记网络。状态行可进入“连接详情”，展示连接状态、SSID、信号强度和 IPv4；断线时清空旧地址和旧信号。列表持续刷新或在进入详情时读取新快照，不能把从 Tools 移出的静态旧值当成当前连接。配网凭据及保存流程保持 Wi-Fi Service 所有，Settings 不直接调用 `esp_wifi_*` 或 NVS。
- `System` 改为四项子菜单：`系统信息`、`资源状态`、`配置状态`、`关于`。前三者分别沿用 Tools 的只读内容和 Settings 现有配置来源内容；关于页沿用 Tools 的信息。每个详情页 B 返回 System 子菜单，System 子菜单 B 返回 Settings 顶层，顶层 B 返回 Launcher。焦点与菜单位置在同一打开会话内保留，不因查看详情被重置。
- 所有新页面使用现有 i18n 文案表和字体令牌，中英双表同步；中文缺字库时仍按现有整表英文回退。六行诊断数据沿用现有 160 × 128 单页排布，若文案更长先调整布局而非裁掉字段。

### Tools

保持注册、图标与 App 生命周期。最终菜单仅有「番茄时钟」，A 进入计时详情，B 返回 Launcher；不保留 Wi-Fi／系统／资源／关于的隐藏入口。若信息归位先于番茄时钟开发完成，过渡版可显示“暂无工具”并让 B 返回；最终验收以单入口菜单为准。

## 代码落点与边界

| 文件 | 预期改动 |
| --- | --- |
| `main/main.c` | 仅调整普通固件 `launcher_boot()` 的 App 注册顺序；不改注册表排序算法或 App ID |
| `main/apps/tools/xiaomiao_tools.c` | 将四个现有详情页及相关菜单状态从 Tools UI 移出，最终留下单个番茄时钟入口；清理该文件因此失去用途的定时器、引用和辅助函数。计时详情按独立设计实施 |
| `main/apps/settings/xiaomiao_settings.c` | Wi-Fi 连接详情、System 子菜单及四个详情页；复用 Service 公开快照和现有 B 释放锁存规则 |
| `main/framework/xiaomiao_i18n.{h,c}` | 补充或重新归类“暂无工具”“连接详情”“资源状态”“配置状态”等文案；不改变已有文案 ID 的含义以致其他页面错字 |
| `README.md`、`docs/project-overview.md`、`ROADMAP.md` | 实施并取得必要验证后更新当前状态、导航与验证记录；历史 Goal 文档保留原样，作为当时版本的验收记录 |

迁移只改变 UI 信息归属和 Launcher 顺序，不改变 Wi-Fi Service、Settings Service、Assets／Font／Storage Service 的业务语义，不触碰 NVS schema、网络凭据、分区表或 GD32 固件。已在进行中的 PC Monitor 额度工作保持独立，避免交叉修改其业务页面。

## 实施检查点与验收

| 检查点 | 静态检查 | 人工验收 |
| --- | --- | --- |
| CP1 Launcher 顺序 | 五个 App ID 唯一、注册顺序为 `pc_monitor/tools/games/settings/hardware_test` | 第一页四格按上图显示；第三格 A 进入 Games，返回焦点仍在第三格；第二页 Hardware Test 可达 |
| CP2 Settings 信息归位 | 四项 Tools 内容均有唯一 Settings 入口；服务调用边界、快照失效规则和 B 返回层级正确 | Wi-Fi 连接／断连详情，System 四页逐项目视；配网、忘记网络和配置状态仍可用 |
| CP3 Tools 单入口与回归 | Tools 不再引用旧详情状态／定时器，资源释放与焦点归还正确；`git diff --check` | 最终 Tools 仅见番茄时钟入口；Settings 多层往返、五 App 开关、Hardware Test B 长按、资源异常显示无回归。计时行为另按番茄时钟设计验收 |

固件构建、烧录和目标板操作依项目 `AGENTS.md` 由人工执行，可用 `idf.py build` 或 `idf.py -p COM5 flash monitor`；Agent 复核源码、diff、人工日志和照片。没有人工构建与实机证据时不能把开发节点标为完成。

## 设计假设

“游戏放在第三个位置”按 Launcher **第一页第三格（索引 2）**理解。为避免擅自重排其他入口，本方案将现有非游戏入口按相对顺序放在其余位置，因此 Tools 在第二格，Settings 在第四格。若希望 Settings 提前到第二格，需在实施前调整目标顺序；迁移归属与验收内容不受影响。
