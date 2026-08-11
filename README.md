# IME 冲突修复 - The Binding of Isaac: Repentance+

## 这是什么？

很多中国玩家玩以撒时会遇到一个烦人的问题：中文输入法（微软拼音、搜狗等）处于开启状态时，游戏内的键盘操作会被吞掉，按 WASD 没反应，或者突然弹出拼音选词框。

本 Mod 解决这个问题：

- **启用 Mod 时**：游戏启动后立即禁用中文输入法（主菜单即生效），按键恢复正常，全程无感。
- **禁用/卸载 Mod 时**：不生效，输入法完全正常。

卸载或删除后完全还原，不影响游戏本身。

## 安装方法

### 第一步：订阅 Mod（Steam 创意工坊）

在 Steam 创意工坊搜索 "IME Conflict Fix" 并订阅。（如果不想用创意工坊，也可以把本项目的 `ime-conflict-fix/` 文件夹直接复制到游戏的 mods 目录。）

### 第二步：下载工具

从 [GitHub Releases 页面] 下载（两个文件放在同一目录）：

- `ime_patcher.exe`（补丁工具，只需用一次）
- `ime_fix.dll`（输入法管理 DLL）

### 第三步：运行补丁（一次性，全自动）

**双击运行 `ime_patcher.exe`**。它会自动从 Steam 注册表找到以撒的游戏目录，弹出确认框，点"是"即完成：

- 自动给 `isaac-ng.exe` 打补丁（备份为 `isaac-ng.exe.imefix.bak`）
- **自动把 `ime_fix.dll` 复制到游戏目录**
- 如果装有官中补丁，会自动把其 config.ini 的 check 改为 -1（消除启动校验弹窗）

全程无需手动操作。**这个步骤只需要做一次**，不需要每次启动游戏都运行。

> 若自动定位失败，会弹出文件选择框，手动选 `isaac-ng.exe` 即可。

### 第四步：启动游戏并在 Mod 菜单启用

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

### Q: 可以和 REPENTOGON / 官中补丁一起用吗？
A: 可以。本 Mod 通过修改 exe 的导入表加载 DLL，和 REPENTOGON、官中补丁互不冲突。它们都改过 `isaac-ng.exe` 也没关系，按顺序依次打补丁即可。

### Q: 游戏更新后需要重新打补丁吗？
A: **需要**。Steam 更新游戏时会用新的 exe 覆盖旧的，之前的补丁就没了。更新后重新运行一次 `ime_patcher.exe` 即可（第二步到第四步，一分钟搞定）。

### Q: 这个补丁会改坏游戏吗？
A: 不会。`ime_patcher.exe` 只往 exe 的导入表里加一个条目，让游戏启动时自动加载 `ime_fix.dll`。打补丁前会自动生成 `isaac-ng.exe.bak` 备份，随时可以还原。

### Q: 为什么需要 DLL？其他 Mod 不需要啊。
A: 输入法管理需要操作系统层面的 API，以撒的 Lua Mod API 做不到这件事。所以本 Mod 用补丁的方式让游戏启动时自动加载一个 DLL 来完成。DLL 是纯 Windows API 实现的，没有运行时依赖，而且全程不联网、不注入其他进程。

### Q: 联机模式下输入法会怎样？
A: 由于 `ImmDisableIME(-1)` 不可逆，启用 Mod 后联机游戏内也无法使用中文输入法（游戏内聊天仅限英文）。如需联机中文聊天，请禁用本 Mod。这是"启动即禁用"方案的固有权衡。

### Q: 搜狗输入法能用吗？
A: 微软拼音已经完整支持。搜狗拼音在大多数情况下可用，如果遇到问题请在 GitHub Issues 反馈。

### Q: Steam Deck / Linux 能用吗？
A: 目前仅支持 Windows。非 Windows 系统上 Lua Mod 会自动跳过，不影响游戏运行。

### Q: 杀毒软件报毒怎么办？
A: 本 Mod 所有可执行文件都通过了 VirusTotal 扫描。如果被杀软拦截，将文件加入白名单即可。你也可以提交误报给杀毒厂商。

## 技术原理（给想了解的人）

整个方案由三个部分组成：

1. **ime_patcher.exe**：PE 导入表补丁工具。修改 `isaac-ng.exe` 的导入表，让 Windows 加载器在游戏启动时自动加载 `ime_fix.dll`。类似官中补丁的做法，一次性修改，自动备份原文件。内置官中补丁 config.ini 处理（check=-1，消除弹窗）。

2. **ime_fix.dll**：输入法管理器。启动后在工作线程轮询存档目录的 log.txt：
   - 记录启动时 log.txt 大小（基线）
   - 扫描**新增部分**是否出现 `Running Lua Script: .../ime-conflict-fix/main.lua`（游戏只在 mod 真正执行时写这行，`LOADED MOD` 枚举不可靠）
   - 找到 → 调用 `ImmDisableIME(-1)` 禁用输入法（主菜单即生效）
   - 30 秒未找到（mod 禁用/未执行）→ 不做任何事

3. **Lua Mod**（创意工坊）：极简——只负责被游戏执行并留下 `main.lua` 执行日志，作为 DLL 的"mod 已启用"信号。

为什么用 log.txt 信号而不是直接启动时禁用？因为 `ImmDisableIME(-1)` 是**不可逆**的，一旦调用就无法在进程内重新启用。必须确认 mod 已启用（= 用户需要禁用 IME）才调用。

## 反馈与贡献

- GitHub Issues: [repository URL placeholder]
- 补丁排错群: [placeholder]
