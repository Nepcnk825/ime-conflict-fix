# IME 冲突修复 - The Binding of Isaac: Repentance+

## 这是什么？

很多中国玩家玩以撒时会遇到一个烦人的问题：中文输入法（微软拼音、搜狗等）处于开启状态时，游戏内的键盘操作会被吞掉，按 WASD 没反应，或者突然弹出拼音选词框。

本 Mod 解决这个问题：

- **启用 Mod 时**：游戏启动后立即禁用中文输入法（主菜单即生效），按键恢复正常，全程无感。
- **禁用/卸载 Mod 时**：不生效，输入法完全正常。

卸载或删除后完全还原，不影响游戏本身。

## 安装方法

有两种方式，**任选其一**即可。两种方式的差异只在于如何获取 mod 本体，**无论哪种方式都需要运行 `ime_patcher.exe` 打补丁**：

### 方式一：Steam 创意工坊订阅（推荐）

1. 在 Steam 创意工坊搜索 "IME Conflict Fix" 并订阅
2. 启动游戏，在主菜单 Mods 菜单确认 "IME Conflict Fix" 已启用
3. 运行 `ime_patcher.exe` 打补丁（见下方"打补丁"）

> 方式一不需要额外下载工具——创意工坊订阅即完成 mod 本体的获取，只需构建/获取 `ime_patcher.exe` 和 `ime_fix.dll` 打补丁。

### 方式二：从源码自行构建

1. 克隆本仓库到本地
2. **自行构建**（需要 Visual Studio 2022）：
   ```bat
   cd src\ime_fix      && build.bat    :: 生成 ime_fix.dll
   cd src\ime_patcher  && build.bat    :: 生成 ime_patcher.exe
   ```
   构建产物放在同一目录
3. 把 `main.lua` + `metadata.xml` 复制到游戏的 `mods/ime-conflict-fix/` 目录
4. 启动游戏，在主菜单 Mods 菜单启用 "IME Conflict Fix"
5. 运行 `ime_patcher.exe` 打补丁（见下方"打补丁"）

### 获取打补丁工具

由于本仓库只发布源代码（不提供预构建二进制，有意审查者可从源码自行构建），`ime_patcher.exe` 和 `ime_fix.dll` 需要自行构建（见上方方式二第 2 步）。

> **可复现构建验证**：构建脚本使用 `/Brepro`（确定性编译），任何人用相同源码 + Visual Studio 2022 构建，应得到与下列校验码一致的二进制。构建后可用以下命令比对：
> ```bat
> certutil -hashfile ime_fix.dll MD5
> certutil -hashfile ime_patcher.exe MD5
> ```
>
> **v0.2.0 源码构建的参考校验码**：
>
> | 文件 | MD5 | SHA256 |
> |------|-----|--------|
> | ime_fix.dll (84,992 B) | `6CC9DCD26835E8C95E95D064AD031FC7` | `188B942D380737B5CB0CD621DD31775612F162C6E95894E5902F306F1B6B2DC6` |
> | ime_patcher.exe (134,656 B) | `B45FB3E28D485DF2F159A75155F4BC3D` | `3E90C5E9B87C2517C179A9D95161D638D2DC6046D5422BD370CF6DAC8DF45079` |

### 打补丁（一次性，全自动）

**双击运行 `ime_patcher.exe`**。它会自动从 Steam 注册表找到以撒的游戏目录，弹出确认框，点"是"即完成：

- 自动给 `isaac-ng.exe` 打补丁（备份为 `isaac-ng.exe.imefix.bak`）
- **自动把 `ime_fix.dll` 复制到游戏目录**
- 如果装有官中补丁，会自动把其 config.ini 的 check 改为 -1（消除启动校验弹窗）

全程无需手动操作。**这个步骤只需要做一次**，不需要每次启动游戏都运行。

> 若自动定位失败，会弹出文件选择框，手动选 `isaac-ng.exe` 即可。

### 启动游戏

正常启动以撒，在主菜单进入 Mods，确认 "IME Conflict Fix" 已启用。

**启用 = 禁用输入法**，**禁用 = 不生效**——完全由游戏自身的 Mod 开关控制，无需额外设置。

## 如何确认生效？

启动游戏到主菜单后，尝试切换输入法（Ctrl+Shift / Win+空格）或打字：
- **Mod 启用时**：切换无效、无法呼出中文输入法 —— Mod 在工作。
- **Mod 禁用时**：输入法正常可用。

## 卸载方法

**双击运行 `ime_patcher.exe`，选中已补丁的 `isaac-ng.exe`**，自动还原。卸载时会自动：

1. 用 `isaac-ng.exe.imefix.bak` 还原 exe（官中补丁的 bootstp 导入会保留，汉化不受影响）
2. 自动删除游戏目录中的 `ime_fix.dll`
3. 自动恢复官中补丁 config.ini 的 check 原值（如果安装时改过）
4. **完全清理**：还原后 exe 与官方原版 MD5 完全一致，无任何残留

> 还原时保留 `.imefix.bak` 备份文件（防误操作）。如需连备份一起删除（零残留），
> 在还原确认框点"否"（还原并完全清理），或命令行运行：
> `ime_patcher.exe --restore --clean "游戏目录\isaac-ng.exe"`

其他清理：
- 在 Steam 创意工坊取消订阅本 Mod（Lua 部分）
- 备份丢失时，可在 Steam 里对以撒右键 -> 属性 -> 已安装文件 -> 验证游戏文件完整性恢复原版

## 常见问题

### Q: 会影响其他游戏或应用吗？
A: 不会。本 Mod 只管理以撒这一个进程的输入法，退出游戏后不留下任何状态。

### Q: 可以和忏悔（REPENTOGON）/ 忏悔+官中补丁一起用吗？
A: 可以。本 Mod 通过修改 exe 的导入表加载 DLL，和忏悔（REPENTOGON）、忏悔+官中补丁互不冲突。它们都改过 `isaac-ng.exe` 也没关系，按顺序依次打补丁即可。

### Q: 游戏更新后需要重新打补丁吗？
A: **需要**。Steam 更新游戏时会用新的 exe 覆盖旧的，之前的补丁就没了。更新后重新运行一次 `ime_patcher.exe` 即可（一分钟搞定）。

### Q: Mod 更新后需要重新打补丁吗？
A: **不需要**。分两部分看：
- **Lua mod 更新**（创意工坊自动更新）：只改 `main.lua`，补丁和 DLL 都不变 → 无需重打补丁
- **ime_fix.dll 更新**（发布新版本）：exe 补丁仍指向 `ime_fix.dll!IME_Init`，导入不变 → 无需重打补丁，只需把新 DLL 复制到游戏目录

只有当**游戏本体**更新（Steam 覆盖 exe）时才需要重新打补丁。

### Q: 这个补丁会改坏游戏吗？
A: 不会。`ime_patcher.exe` 只往 exe 的导入表里加一个条目，让游戏启动时自动加载 `ime_fix.dll`。打补丁前会自动生成 `isaac-ng.exe.bak` 备份，随时可以还原。**即使出现问题，也可以在 Steam 里对以撒右键 -> 属性 -> 已安装文件 -> 验证游戏文件完整性**，即可恢复官方原版文件。

### Q: 为什么需要 DLL？其他 Mod 不需要啊。
A: 输入法管理需要操作系统层面的 API，以撒的 Lua Mod API 做不到这件事。所以本 Mod 用补丁的方式让游戏启动时自动加载一个 DLL 来完成。DLL 是纯 Windows API 实现的，没有运行时依赖，而且全程不联网、不注入其他进程。

### Q: 联机模式下输入法会怎样？
A: **联机（多人）模式下游戏会禁用所有 Mod**（这是游戏机制，包括本 Mod 的 Lua 部分），所以联机时 DLL 检测不到 mod 启用信号，**不会禁用输入法**——联机聊天中文输入正常。

需要澄清的边界场景：如果你给 `isaac-ng.exe` 打了补丁来在联机模式下使用 mod，且需要在游戏内使用中文输入法，那么你需要**在游戏内单独禁用本 Mod**（Mods 菜单中关闭 "IME Conflict Fix"）来防止输入法被禁用。因为打了补丁后 DLL 已注入，一旦本 Mod 启用（无论单人还是联机前的主菜单），`ImmDisableIME(-1)` 可能已调用且不可逆。这是"启动即禁用"方案与联机机制的固有权衡：**联机游玩如需中文输入法，请保持本 Mod 禁用状态启动**。

### Q: 搜狗输入法能用吗？
A: 微软拼音已经完整支持。搜狗拼音在大多数情况下可用，如果遇到问题请在 GitHub Issues 反馈。

### Q: Steam Deck / Linux 能用吗？
A: 目前仅支持 Windows。非 Windows 系统上 Lua Mod 会自动跳过，不影响游戏运行。

### Q: 杀毒软件报毒怎么办？
A: 本项目**未做过 VirusTotal 等第三方扫描认证**。二进制文件由开源代码构建（见 [构建方法](#安装方法)），你有兴趣可自行验证源码与二进制的一致性。如果被杀软误报，将文件加入白名单即可。

## 技术原理（给想了解的人）

整个方案由三个部分组成：

1. **ime_patcher.exe**：PE 导入表补丁工具。修改 `isaac-ng.exe` 的导入表，让 Windows 加载器在游戏启动时自动加载 `ime_fix.dll`。类似忏悔+官中补丁的做法，一次性修改，自动备份原文件。内置官中补丁 config.ini 处理（check=-1，消除弹窗）。

2. **ime_fix.dll**：输入法管理器。启动后在工作线程轮询存档目录的 log.txt（记录基线，只扫描新增部分），分两阶段检测：
   - **阶段 1（启动窗口 ~12 秒）**：扫描是否出现 `Running Lua Script: .../ime-conflict-fix/main.lua`（游戏只在 mod 真正执行时写这行，`LOADED MOD` 枚举不可靠）→ 找到立即 `ImmDisableIME(-1)`（主菜单即生效）
   - **阶段 2（启动窗口后）**：若 mod 在游戏内才被启用（log 出现 `Menu Mods Init` 之后的 mod 标记）→ 等 `AnmCache: Clear`（mod 重载完成、回到主菜单）→ 禁用；或等开局信号 `[IME_RUN_STARTED]` → 开局禁用
   - mod 全程禁用/未执行 → 不做任何事

3. **Lua Mod**（创意工坊）：极简——被游戏执行时留下 `main.lua` 执行日志（阶段 1 信号），并在开局时写 `[IME_RUN_STARTED]`（阶段 2 信号）。

为什么用 log.txt 信号而不是直接启动时禁用？因为 `ImmDisableIME(-1)` 是**不可逆**的，一旦调用就无法在进程内重新启用。必须确认 mod 已启用（= 用户需要禁用 IME）才调用。

## 参考与致谢

- **忏悔+官中补丁**（[Steam 创意工坊](https://steamcommunity.com/sharedfiles/filedetails/?id=3568677664)）：本项目参考了其 PE 导入表补丁的实现方式（patcher 修改 `isaac-ng.exe` 导入表加载 DLL），并兼容处理其 config.ini 校验（自动设置 `check=-1` 消除弹窗）。若未安装忏悔+官中补丁，本 Mod 的这部分兼容逻辑自动跳过，不影响使用。
- **忏悔（REPENTOGON）**：兼容，按顺序依次打补丁即可。
- **IMBlocker**（Minecraft 输入法修复项目）：启发自其输入法处理思路（本项目改用 Windows 原生 API 实现）。

> 本项目开发过程中使用了 AI 辅助工具（代码生成、审查与文档撰写），所有代码均经过人工审查与实测验证。

## 反馈与贡献

- GitHub Issues: https://github.com/Nepcnk825/ime-conflict-fix/issues
