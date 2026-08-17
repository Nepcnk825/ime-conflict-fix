# 架构说明（当前源码状态）

本文描述 **当前工作区源码** 的实际架构。已上线创意工坊版本可能落后于此；当前仓库与发布状态记录在内部 `AGENTS.md`，发布流程见 `docs/RELEASING.md`。

## 版本矩阵

| 组件 | 版本 | 位置 | 说明 |
|------|------|------|------|
| `main.lua` | 0.3.12 | 仓库根 | Lua 信号源 + MCM 配置（联机选项） |
| `ime_fix.dll` | 0.4.15-layout（mod 目录日志 + 单布局兜底 + online force 主菜单生效 + 1ms 轮询 + 大厅标记去重 + 聊天广播后才复位） | `src/ime_fix/` | 功能 DLL，创意工坊内发布为 `ime_fix.bin` |
| `ime_loader.dll` | 0.2.1 | `src/ime_loader/` | 薄壳加载器，游戏根目录唯一常驻文件 |
| `ime_patcher.exe` | v1.5 | `src/ime_patcher/` | PE 导入表补丁工具；卸载时清理旧版遗留目录 |
| `metadata.xml` | 0.7 | 仓库根 | 创意工坊元数据唯一真源 |

## 组件关系

```
isaac-ng.exe（patcher v1.5 打补丁）
  └─ Windows PE 加载器 → ime_loader.dll（游戏根目录，永不更新）
       ├─ SetEnvironmentVariableW("IME_FIX_GAME_DIR", <游戏根目录>)
       ├─ 枚举 mods\，前缀匹配 "ime-conflict-fix*"
       │    （兼容手工安装 ime-conflict-fix 与 Steam 的 ime-conflict-fix_<id>）
       └─ LoadLibrary(mods\<匹配文件夹>\ime_fix.bin)
            └─ 读 IME_FIX_GAME_DIR → savedatapath.txt → log.txt
                 └─ worker 线程：基线偏移 + 只扫描 log.txt 新增部分
```

关键设计原因：

1. **exe 导入的 DLL 必须在游戏根目录**：Windows 导入表搜索顺序是 exe 目录 → 系统目录 → PATH，不会搜索 `mods/`。所以游戏根目录必须有一个真实存在的 DLL。
2. **运行中的 DLL 不能被替换**：如果功能 DLL 放在游戏根目录，Steam 更新 Mod 时会因文件锁定失败。因此功能 DLL 放在 mods 文件夹（Steam 就地更新），游戏根目录只留一个永不变化的薄壳 loader。
3. **loader 用 `DllMain` 的 hModule 解析自身路径**：`GetModuleFileNameW(NULL)` 返回宿主 exe 路径，standalone 测试时会错。loader 保存 `g_hself`，再取 `GetModuleFileNameW(g_hself)`。
4. **`ime_fix.bin` 不解析自身路径**：它通过 `IME_FIX_GAME_DIR` 找到游戏根目录，再读 `savedatapath.txt` 得到存档目录。

## Lua 信号协议

`main.lua` 只发三种信号，全部写入游戏 `log.txt`：

| 信号 | 触发时机 | DLL 动作 |
|------|----------|----------|
| `Running Lua Script: .../ime-conflict-fix/main.lua` | 游戏真正执行本 Mod（启动时） | 阶段 1：请求英文布局 |
| `[IME_RUN_STARTED]` | `MC_POST_GAME_STARTED` 开局 | 阶段 2：请求英文布局 |
| `[IME_IDLE]` | 玩家连续 3 秒键盘/鼠标均无输入且游戏未暂停 | 阶段 2：请求英文布局 |

`LOADED MOD` 枚举行不可信：`EnableMods=0` 时游戏仍会枚举 mod 文件夹；只有 `Running Lua Script` 能证明 Lua 真正执行。

## DLL 检测流程（ime_fix v0.4.15-layout）

1. 进程附加后 2 秒，worker 解析存档路径并记录 `log.txt` 当前大小作为基线。
2. **滚动扫描**：阶段 1 和阶段 2 都只读取新增字节；单次最多读 256KB，
   并保留 128 字节尾部重叠，避免长会话日志超过 256KB 后永久漏检，
   也避免标记恰好跨轮询边界时丢失。
3. **阶段 1（启动窗口 12 秒）**：每 2 秒扫描新增日志。用位置关联匹配
   `ime-conflict-fix` 后 64 字符内出现 `/main.lua`，避免枚举行误报。
   命中后通过 `WM_INPUTLANGCHANGEREQUEST` 请求游戏主窗口切换到
   英文（US）键盘布局，日志写
   `mod enabled - English layout requested at startup`。
4. **阶段 2（每 200ms 滚动扫描）**：
   - 中途启用 Mod：检测到 Mod 执行标记后立即向游戏主窗口异步
     `PostMessage(WM_INPUTLANGCHANGEREQUEST)`，失败则每轮重试；不再等待
     `AnmCache`、开局或 3 秒空闲。如果游戏重启，下次启动阶段 1 再次请求。
   - `[IME_RUN_STARTED]` → 请求英文布局。
   - `[IME_IDLE]` → 请求英文布局，作为兜底。
5. Mod 全程禁用时，任何信号都不会出现，DLL 不做任何事。

## 设计演进（opencode 会话沉淀）

早期调研与试错已经收敛为以下事实，之后不要再回头重试：

- **纯 Lua 方案不可行**：原版 API 与 REPENTOGON 均没有任何 IME 控制能力。
- **代理 DLL 方案不可行**：Isaac 不导入 `version.dll` / `dsound.dll`；`winmm` 代理实测会崩溃。
- **IMM32 状态 API 不可用**：TSF 输入法（微软拼音等）忽略 `ImmSetOpenStatus` / `ImmAssociateContext` 等调用。
- **聊天框焦点/热键切换不可靠**：Enter 轮询误报多，TSF 焦点接管和进程内重新启用均无法稳定工作。
- **联机状态无 Lua API**：硬编码内存偏移会随版本漂移；EOS IAT Hook 可行但成本高。最终采用 `log.txt` / `online_logs` 文件信号。
- **用户最终接受的形态**：启用 Mod 且启动时被游戏执行 → 自动禁用 IME；禁用 Mod → 完全不生效；联机场景保持 Mod 禁用即可正常输入中文。

## 为什么改用可逆的英文布局切换

`ImmDisableIME(-1)` 在实测中有两个不可接受的硬限制：

- **不可逆**：进程内一旦调用就无法恢复。
- **UI 活跃时调用会冻结**：Mod 重载、主菜单初始化、弹窗期间调用会卡死。

v0.4.0-layout 改为：

1. `LoadKeyboardLayoutW(L"00000409", KLF_ACTIVATE)` 加载英文（US）布局；
2. 向游戏主窗口 `PostMessage(WM_INPUTLANGCHANGEREQUEST, 0, hkl)`；
3. 游戏窗口走标准 `DefWindowProc` 处理该消息，切换该线程输入语言。

特点：
- 可逆，不影响其他进程；
- 异步 PostMessage，不会与忙碌 UI 线程同步卡死；
- 没有“调用时机导致冻结”的问题；
- 代价是输入法指示器显示为英文布局，而不是 ImmDisableIME 的“禁用 X”。

## 当前限制与待验证项

**游戏启动后、运行中途才在 Mods 菜单启用 Mod 的场景**，尚不能做到“返回主菜单立即禁用”。

试错历史（完整记录见 `.dev/HANDOVER-2026-08-15.md`）：

| 版本 | 方案 | 结果 |
|------|------|------|
| v0.2.3 | 等待日志静默 | 8 秒延迟 |
| v0.2.4-5 | 500ms 粒度日志稳定检测 | 卡死（重载中短暂静默误判） |
| v0.2.7 | 特征行 `AnmCache: cannot remove reference` | 卡死 |
| v0.2.8 | 固定等待 5 秒 | 慢 |
| v0.2.9 | `AnmCache: Clear` 后立即禁用 | 卡死（Clear 后主菜单仍在初始化） |
| v0.3.0 | Clear + 2 秒宽限 | 未测透 |
| v0.3.1-3 | Lua busy-wait 制造输入黑洞 | `os.clock` 在游戏 Lua 沙箱不可用 |
| v0.3.4 | Lua 信号 + DLL `SuspendThread` | 信号链路未通 |
| v0.3.5 | marker + `SuspendThread` | 永久冻结（imm32 与主线程同步死锁） |
| v0.3.6（已验证） | `[IME_IDLE]` 空闲信号 | 曾作为待验证方案 |
| v0.3.7 | 修复 `ACTION_CONFIRM/CANCEL` 不存在导致的 Lua 错误；Phase 2 改为滚动日志扫描；空闲检测覆盖鼠标输入 | 已修复，等待回归验证 |
| v0.3.8 | 新增主线程 `WM_TIMER` 快速路径：`AnmCache: Clear` 后由游戏主线程执行 `ImmDisableIME` | 用户实测：大量按键时仍冻结 |
| v0.3.9 | 在 WM_TIMER 回调调用 `ImmDisableIME` 前排空输入队列 | 仍处于游戏重启流程内，不安全 |
| v0.4.15-layout（当前） | 现有英文布局异步切换；online_force 从主菜单生效；1ms 快速轮询；大厅标记按日志绝对位置去重；Enter 只打开聊天，检测到 `Broadcasting chat message` 后才切回英文 | 待用户验证 |

已知安全时机：启动早期（阶段 1）、开局信号、空闲 3 秒。

## 反模式（本项目内不要再犯）

- **不要**依赖 `LOADED MOD` 判断启用。
- **不要**用 `ImmSetOpenStatus` 等 API 处理 TSF 输入法。
- **不要**漏掉 `/EXPORT:IME_Init` 链接参数。
- **不要**构建 x64：Isaac Repentance+ 是 32 位 PE。
- **不要**代理 `version.dll` / `dsound.dll` / `winmm.dll`：前两个游戏不导入，winmm 代理会崩。
- **不要**在 Mod 重载或 UI 忙碌时调用 `ImmDisableIME`。
- **不要**用 `GetModuleFileNameW(NULL)` 解析被加载 DLL 自身路径。
- 判断文件是否被补丁修改，必须比对完整文件哈希，不能只看导入表。

## 日志与调试

- DLL 日志：`<游戏目录>\ime-conflict-fix\debug.log`（不再写 C 盘）
- 游戏日志：`Documents\My Games\Binding of Isaac Repentance+\log.txt`
- 关键日志行：
  - `ime_loader.dll v0.2.1 loaded`
  - `ime_fix.dll v0.4.15-layout loaded (startup-disable mode)`
  - `mid-session mod execution detected - no in-session fast disable (game is restarting)`
  - `mod enabled - English layout requested at startup`
  - `per-run marker - English layout requested at run start`
  - `player idle - English layout requested after 3s of no input`
- standalone 测试必须使用 32 位 rundll32：`C:\Windows\SysWOW64\rundll32.exe ime_loader.dll,IME_Init`。System32 版本是 x64，会静默失败；PowerShell 不等待 GUI 子系统进程，检查日志前先 `Start-Sleep 2`。
