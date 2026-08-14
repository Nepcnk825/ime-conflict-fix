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

1. 在 Steam 创意工坊搜索 "IME Conflict Fix" 并订阅（等待 Steam 下载完成）
2. 打开游戏的 mods 文件夹（Steam 库 -> 右键以撒 -> 管理 -> 浏览本地文件 -> 进入 `mods/` 目录），确认其中已有 `ime-conflict-fix` 开头的文件夹（Steam 订阅下载为 `ime-conflict-fix_<id>` 形式，如 `ime-conflict-fix_3783304248`；加载器会自动查找）
3. 进入 mods 下 `ime-conflict-fix` 开头的文件夹，**解压其中的 `ime_loader.zip`**，得到 `ime_patcher.exe` 和 `ime_loader.dll`（保持在同一文件夹）
4. 运行 `ime_patcher.exe` 打补丁（见下方"打补丁"）
5. 启动游戏，在主菜单 **Mods** 菜单中启用 "IME Conflict Fix"

> 方式一不需要额外下载任何工具——创意工坊订阅即完成 mod 本体的获取（会自动创建 `mods/ime-conflict-fix/` 文件夹，补丁工具就打包在 `ime_loader.zip` 里），解压后直接运行即可。

### 方式二：从源码自行构建

1. 克隆本仓库到本地
2. **自行构建**（需要 Visual Studio 2022）：
   ```bat
   cd src\ime_fix      && build.bat    :: 生成 ime_fix.dll（功能 DLL，发布为 ime_fix.bin）
   cd src\ime_loader   && build.bat    :: 生成 ime_loader.dll（薄壳加载器）
   cd src\ime_patcher  && build.bat    :: 生成 ime_patcher.exe
   ```
   构建产物放在同一目录
3. 打开游戏的 mods 文件夹（Steam 库 -> 右键以撒 -> 管理 -> 浏览本地文件 -> 进入 `mods/` 目录），**自行新建一个文件夹并命名为 `ime-conflict-fix`**，把仓库中的 `main.lua` + `metadata.xml` 复制进去，并把 `ime_fix.dll` 重命名为 `ime_fix.bin` 一并放入（功能 DLL 本体，由加载器直接加载）
4. 运行 `ime_patcher.exe` 打补丁（见下方"打补丁"）
5. 启动游戏，在主菜单 Mods 菜单中启用 "IME Conflict Fix"

### 获取打补丁工具

由于本仓库只发布源代码（不提供预构建二进制，有意审查者可从源码自行构建），`ime_patcher.exe` 和 `ime_loader.dll` 需要自行构建（见上方方式二第 2 步；方式一订阅后直接解压 `ime_loader.zip` 即可）。

> **可复现构建验证**：构建脚本使用 `/Brepro`（确定性编译），任何人用相同源码 + Visual Studio 2022 构建，应得到与下列校验码一致的二进制。构建后可用以下命令比对：
> ```bat
> certutil -hashfile ime_fix.dll MD5
> certutil -hashfile ime_loader.dll MD5
> certutil -hashfile ime_patcher.exe MD5
> ```
>
> **当前版本（v0.4.0：ime_fix v0.2.1 / ime_loader v0.2.0 / ime_patcher v1.4）源码构建的参考校验码**：
>
> | 文件 | MD5 | SHA256 |
> |------|-----|--------|
> | ime_fix.dll (84,992 B) | `EFAAFD2AF8987507C3CA2842FDE06B47` | `C954E1AF809E756B9C86A2511876857B822FB63072D7B6E478BEB916B12389E3` |
> | ime_loader.dll (82,432 B) | `E343C2A1F1114966062541C5BC19059B` | `C1D65D99108CCD68263F4E59D0A445E1E3998A0D7FE64AC7632DE81C40E0AC37` |
> | ime_patcher.exe (136,704 B) | `88BA79B481180A520AFB162DEC42EB8C` | `D652A5D6621FF569333ADACC6311EC97E500C06683DC6270337BE0B993621378` |

### 打补丁（一次性，全自动）

**双击运行 `ime_patcher.exe`**。它会自动从 Steam 注册表找到以撒的游戏目录，弹出确认框，点"是"即完成：

- 自动给 `isaac-ng.exe` 打补丁（备份为 `isaac-ng.exe.imefix.bak`）
- **自动把 `ime_loader.dll` 复制到游戏目录**（唯一常驻文件，永不更新；启动时直接加载创意工坊的 `mods/ime-conflict-fix/ime_fix.bin`，实现免重打补丁的自动更新）
- 如果装有官中补丁，会自动把其 config.ini 的 check 改为 -1（消除启动校验弹窗）

全程无需手动操作。**这个步骤只需要做一次**，不需要每次启动游戏都运行。

> 若自动定位失败，会弹出文件选择框，手动选 `isaac-ng.exe` 即可。

### 启动游戏

正常启动以撒，在主菜单进入 Mods，确认 "IME Conflict Fix" 已启用。

**启用 = 禁用输入法**，**禁用 = 不生效**——完全由游戏自身的 Mod 开关控制，无需额外设置。

## 如何确认生效？

启动游戏到主菜单后，任选以下方式验证：

- **操作测试**：尝试切换输入法（Ctrl+Shift / Win+空格）或打字：
  - **Mod 启用时**：切换无效、无法呼出中文输入法 —— Mod 在工作。
  - **Mod 禁用时**：输入法正常可用。
- **状态栏观察**：将游戏设为**窗口化或无边框窗口**运行，直接观察任务栏（或系统托盘）的输入法指示器——Mod 生效时指示器显示为"圆圈中间带 X"（输入法被禁用的标志），无论怎么按切换键都无法恢复中文模式。

## 卸载方法

**双击运行 `ime_patcher.exe`，选中已补丁的 `isaac-ng.exe`**，自动还原。卸载时会自动：

1. 用 `isaac-ng.exe.imefix.bak` 还原 exe——**如果**你之前安装过其他补丁（如官中补丁），备份的就是"安装其他补丁后"的状态，还原后其他补丁完整保留、不受影响；**如果**你没装过任何其他补丁，还原后就是官方原版
2. 自动删除游戏目录中的 `ime_loader.dll`
3. **如果**装有官中补丁且安装时改过其 config.ini，会自动恢复 check 原值（官中补丁本身不受影响）
4. **完全清理**：还原后 exe 与打补丁前的状态逐字节一致（未装其他补丁时即官方原版 MD5），无任何残留

> 还原确认框点"否"仅还原（保留 `.imefix.bak` 备份文件，防误操作）；点"是"还原并完全清理（连备份一起删除，零残留，推荐），
> 或命令行运行：
> `ime_patcher.exe --restore --clean "游戏目录\isaac-ng.exe"`

> 补充说明：官方补丁（官中补丁等）不是本 Mod 的组成部分，本 Mod 的卸载**只移除自己的影响**。如果只装了本 Mod（没装官中补丁——非忏悔+版本玩家不一定需要官中补丁），卸载后游戏就是纯净的官方状态，无需担心任何"受影响"的问题。

其他清理：
- 在 Steam 创意工坊取消订阅本 Mod（Lua 部分）
- 备份丢失时，可在 Steam 里对以撒右键 -> 属性 -> 已安装文件 -> 验证游戏文件完整性恢复原版

## 常见问题

### Q: 会影响其他游戏或应用吗？
A: 不会。本 Mod 只管理以撒这一个进程的输入法，退出游戏后不留下任何状态。

### Q: 可以和 REPENTOGON / 忏悔+官中补丁一起用吗？
A: **忏悔+官中补丁：实测可共用，且无安装顺序要求**（先装哪个都行）——本 Mod 在 `isaac-ng.exe` 导入表上追加独立条目，官中补丁在其自有空间内修改 exe，两者互不影响、互不覆盖。本 Mod 还自动兼容官中补丁的启动校验（将其 config.ini 的 check 改为 -1，消除弹窗）。

**REPENTOGON：理论上兼容，但未联合实测，请自行验证。**

### Q: 游戏更新后需要重新打补丁吗？
A: **需要**。Steam 更新游戏时会用新的 exe 覆盖旧的，之前的补丁就没了。更新后重新运行一次 `ime_patcher.exe` 即可（一分钟搞定）。

### Q: Mod 更新后需要重新打补丁吗？
A: **不需要**。Mod 更新（Lua 脚本或 DLL）都会通过 Steam 自动同步到 mods 下 `ime-conflict-fix` 开头的文件夹，游戏启动时 `ime_loader.dll` 会直接加载 `ime_fix.bin` 的新版本，全程无感。只有**游戏本体**更新（Steam 覆盖 exe）时才需要重新打补丁。

### Q: 这个补丁会改坏游戏吗？
A: 不会。`ime_patcher.exe` 只往 exe 的导入表里加一个条目，让游戏启动时自动加载 `ime_loader.dll`（它再加载 mods 文件夹里 `ime-conflict-fix` 开头的文件夹中的 `ime_fix.bin`）。打补丁前会自动生成 `isaac-ng.exe.imefix.bak` 备份，随时可以还原。**即使出现问题，也可以在 Steam 里对以撒右键 -> 属性 -> 已安装文件 -> 验证游戏文件完整性**，即可恢复官方原版文件。

### Q: 为什么需要 DLL？其他 Mod 不需要啊。
A: 输入法管理需要操作系统层面的 API，以撒的 Lua Mod API 做不到这件事。所以本 Mod 用补丁的方式让游戏启动时自动加载一个 DLL 来完成。DLL 是纯 Windows API 实现的，没有运行时依赖，而且全程不联网、不注入其他进程。

### Q: 联机模式下输入法会怎样？
A: **联机（多人）模式下游戏默认需要禁用所有 Mod 才能进行**（这是游戏机制，包括本 Mod 的 Lua 部分），所以联机时 DLL 检测不到 mod 启用信号，**不会禁用输入法**——联机聊天中文输入正常。

需要澄清的边界场景：如果你给 `isaac-ng.exe` 打了补丁来在联机模式下使用 mod，且需要在游戏内使用中文输入法，那么你需要**在游戏内单独禁用本 Mod**（Mods 菜单中关闭 "IME Conflict Fix"）来防止输入法被禁用。因为打了补丁后 DLL 已注入，一旦本 Mod 启用（无论单人还是联机前的主菜单），`ImmDisableIME(-1)` 可能已调用且不可逆。这是"启动即禁用"方案与联机机制的固有权衡：**联机游玩如需中文输入法，请保持本 Mod 禁用状态启动**。

### Q: 搜狗输入法能用吗？
A: 微软拼音已经完整支持。搜狗拼音在大多数情况下可用，如果遇到问题请在 GitHub Issues 反馈。

### Q: Steam Deck / Linux 能用吗？
A: 目前仅支持 Windows。非 Windows 系统上 Lua Mod 会自动跳过，不影响游戏运行。

### Q: 杀毒软件报毒怎么办？
A: 本项目**未做过 VirusTotal 等第三方扫描认证**。二进制文件由开源代码构建（见 [构建方法](#安装方法)），你有兴趣可自行验证源码与二进制的一致性（校验码见上文"可复现构建验证"）。如果被杀软误报，将文件加入白名单即可。

## 技术原理（给想了解的人）

> 以下为简略描述，供快速了解大致思路。想要深入了解实现细节，请直接阅读源码：[src/ime_fix](https://github.com/Nepcnk825/ime-conflict-fix/tree/main/src/ime_fix)（输入法管理 DLL）、[src/ime_loader](https://github.com/Nepcnk825/ime-conflict-fix/tree/main/src/ime_loader)（薄壳加载器）与 [src/ime_patcher](https://github.com/Nepcnk825/ime-conflict-fix/tree/main/src/ime_patcher)（补丁工具）。

整个方案由四个部分组成：

1. **ime_patcher.exe**：PE 导入表补丁工具。修改 `isaac-ng.exe` 的导入表，让 Windows 加载器在游戏启动时自动加载 `ime_loader.dll`。一次性修改，自动备份原文件（`.imefix.bak`）。内置官中补丁 config.ini 处理（check=-1，消除弹窗）。

2. **ime_loader.dll**：薄壳加载器（游戏目录唯一常驻文件，永不更新）。启动时设置 `IME_FIX_GAME_DIR` 环境变量后直接加载创意工坊 mod 目录的 `ime_fix.bin`（Steam 自动同步的新版本）。功能 DLL 本体在 mods 文件夹中由 Steam 就地更新，Mod 更新时**无需重新打补丁**，游戏根目录也只有一个永不变化的文件。

3. **ime_fix.dll**（mods 文件夹内的 ime_fix.bin）：输入法管理器。通过 `IME_FIX_GAME_DIR` 定位存档目录后，在工作线程轮询 log.txt（记录基线，只扫描新增部分），分两阶段检测：
   - **阶段 1（启动窗口 ~12 秒）**：扫描是否出现 `Running Lua Script: .../ime-conflict-fix/main.lua`（游戏只在 mod 真正执行时写这行，`LOADED MOD` 枚举不可靠）→ 找到立即 `ImmDisableIME(-1)`（主菜单即生效）
   - **阶段 2（启动窗口后）**：若 mod 在游戏内才被启用（log 出现 `Menu Mods Init` 之后的 mod 标记）→ 等 `AnmCache: Clear`（mod 重载完成、回到主菜单）→ 禁用；或等开局信号 `[IME_RUN_STARTED]` → 开局禁用
   - mod 全程禁用/未执行 → 不做任何事

4. **Lua Mod**（创意工坊）：极简——被游戏执行时留下 `main.lua` 执行日志（阶段 1 信号），并在开局时写 `[IME_RUN_STARTED]`（阶段 2 信号）。

为什么用 log.txt 信号而不是直接启动时禁用？因为 `ImmDisableIME(-1)` 是**不可逆**的，一旦调用就无法在进程内重新启用。必须确认 mod 已启用（= 用户需要禁用 IME）才调用。

## 参考与致谢

- **忏悔+官中补丁**（[Steam 创意工坊](https://steamcommunity.com/sharedfiles/filedetails/?id=3568677664)）：本项目参考了其"patcher.exe 选择 isaac-ng.exe 打补丁"的交互流程，并兼容处理其 config.ini 校验（自动设置 `check=-1` 消除弹窗）。若未安装忏悔+官中补丁，本 Mod 的这部分兼容逻辑自动跳过，不影响使用。
- **IMBlocker**（Minecraft 输入法修复项目）：启发自其输入法处理思路（本项目改用 Windows 原生 API 实现）。

> 本项目开发过程中使用了 AI 辅助工具（代码生成、审查与文档撰写），所有代码均经过人工审查与实测验证。

## 反馈与贡献

- GitHub Issues: https://github.com/Nepcnk825/ime-conflict-fix/issues