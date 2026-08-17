# 集成测试计划

> 前置条件：Windows 10/11、Steam 版 Repentance+、微软拼音已启用、已按 `docs/BUILDING.md` 构建。

## 0. 测试环境注意事项

- 游戏目录一般为 `<Steam>\steamapps\common\The Binding of Isaac Rebirth`；存档目录一般为 `Documents\My Games\Binding of Isaac Repentance+\`
- 若测试机 `options.ini` 为 `EnableMods=0`，保持原状，**不要修改**
- Steam 订阅安装的文件夹名是 `ime-conflict-fix_3783304248`，测试必须覆盖带 `_<id>` 命名
- 游戏内切换 Mod 启用状态可能导致“无法继续游戏”并退出，一次测试只覆盖一个状态场景

## 1. 构建与部署

- [ ] 1.1 `build_all.bat` 全部成功（exit 0），产物为 x86 PE32
- [ ] 1.2 `python tools\package_workshop.py` 成功，打印三个二进制校验码
- [ ] 1.3 将 `workshop_upload/mod/` 内容部署到游戏 `mods/ime-conflict-fix_3783304248/`
- [ ] 1.4 解压 `ime_loader.zip`，双击 `ime_patcher.exe` 打补丁
  - 预期：exe 增大约 1.5 KB、出现 `.imefix` 段、`ime_loader.dll` 自动复制到游戏根目录、`.imefix.bak` 生成

## 2. standalone loader 测试（无需启动游戏）

布置假游戏根目录：

```
game/
├── ime_loader.dll
├── savedatapath.txt            # 内容：Save Data Path: <fake>\savedata
└── mods\ime-conflict-fix_3783304248\ime_fix.bin
```

执行（**必须 SysWOW64**，System32 的 rundll32 是 x64，会静默失败）：

```powershell
C:\Windows\SysWOW64\rundll32.exe game\ime_loader.dll,IME_Init
Start-Sleep 2
Get-Content $env:APPDATA\ime-conflict-fix\debug.log -Tail 20
```

预期日志：
- `ime_loader.dll v0.2.1 loaded`
- `ime_fix.dll v0.4.0-layout loaded (startup-disable mode)`
- `save path: ...`（验证 `IME_FIX_GAME_DIR` 链路）

## 3. 功能测试

### 3.1 Mod 启用启动 → 主菜单禁用

1. 启动前 Mod 保持启用。
2. 启动游戏到主菜单。
3. 尝试 Ctrl+Shift / Win+空格呼出中文输入法。

预期：
- 无法呼出；任务栏输入法指示器为“圆圈带 X”
- `debug.log` 出现 `mod enabled - English layout requested at startup`

### 3.2 Mod 禁用启动 → 不生效

1. 启动前 Mod 保持禁用（或 `EnableMods=0`）。
2. 启动游戏，等 15 秒以上。
3. 尝试呼出中文输入法。

预期：
- 输入法正常可用
- `debug.log` 出现 `mod not enabled at launch - waiting for a safe disable moment`，且没有禁用日志

### 3.3 开局信号（当前阶段 2 安全路径之一）

1. Mod 禁用启动，进入主菜单后再启用 Mod（注意：切换可能触发“无法继续游戏”退出，测试流程需按用户习惯安排）。
2. 重新启动后开始一局游戏。

预期：
- 开局时 `debug.log` 出现 `per-run marker - IME disabled at run start`

### 3.4 空闲信号（兜底，3 秒无输入）

1. Mod 在启动窗口之后才生效。
2. 主菜单保持 3 秒不按任何键、不点击鼠标、不移动鼠标。
3. 检查 `debug.log` 与游戏 `log.txt`。

预期：
- 约 1 秒后 `log.txt` 出现 `Lua Debug: [IME_IDLE_PROBE_1S]`，证明 render 回调和 Input API 正常
- 约 3 秒后出现 `[IME_IDLE]`，`debug.log` 出现 `player idle - IME disabled after 3s of no input`
- 禁用过程不冻结 UI
- `log.txt` 中没有 `ACTION_CONFIRM` / `bad argument` 类 Lua 错误

反向验证：
- 3 秒内移动鼠标或点击鼠标，不应出现 `[IME_IDLE]`。

风险场景：
- 弹窗打开时玩家停手 3 秒是否冻结——若复现，需回退该方案并调整策略。
- 持续操作（键盘或鼠标）的玩家不会触发空闲信号，禁用时间推迟到开局信号。

### 3.5 中途启用 Mod（v0.4.0-layout：不做会话内 IME 调用）

1. 启动游戏前 Mod 保持禁用。
2. 启动后进入 Mods 菜单启用 Mod。
3. 持续狂按键盘/鼠标，观察游戏行为。

预期：
- `debug.log` 出现：
  - `mid-session mod execution detected - no in-session fast disable (game is restarting)`
- **不会**出现 `fast path:` / `WM_TIMER` / `ImmDisableIME` 调用。
- 游戏按自身机制处理 Mod 状态变化（可能自动重启/提示无法继续），全程不冻结。
- 重启游戏后 Mod 已启用，阶段 1 在主菜单立即禁用输入法。

### 3.6 Mod 更新免重打补丁

1. 替换 `mods/ime-conflict-fix_3783304248/ime_fix.bin` 内容（模拟 Steam 更新）。
2. 重启游戏。

预期：加载新 bin，无需重新运行 `ime_patcher.exe`。

### 3.7 联机禁用 Mod 也生效（online_force）

前置：Mod 启用时在 MCM 中把“联机禁用Mod也生效”设为开（会写入 mod 文件夹
`settings.ini` 的 `[online] enabled=1`），然后退出游戏并在游戏选项/Mod 菜单
保持 Mod 禁用。

1. 禁用 Mod 启动游戏，停留在主菜单：任务栏输入法应已自动变为英文（ENG）。
2. 进入联机大厅（创建或加入），英文应保持。
3. 第一次 Enter → 切到中文；在输入框内再按 Enter 不应切回英文；
   真正把消息发出去（游戏日志出现 `Broadcasting chat message`）后才应自动切回英文。
4. 离开大厅后再进入新大厅，或直接开始联机对局。

预期 `debug.log` 出现：
- `settings source=mod lock=... online_force=1 chat_toggle=...`
- `settings online_force=1 - English will be kept from startup`
- `online lobby detected`
- 第一次 Enter：`chat heuristic: Enter -> Chinese for chat (EXPERIMENTAL)`
- 消息发出：`chat message broadcast - preparing to restore English`
- 约 150ms 后：`chat message sent - switched back to English`
- 离开大厅：`left lobby - chat mode ended`
- 新建大厅：`new lobby detected - chat mode ended`
- 联机开局：`networked run start observed`

**重要**：测试结束后再复制 `debug.log`（不要在中途提前复制），否则后续状态
日志不在取证文件中。

## 4. 补丁/卸载测试

### 4.1 完全清理

双击 patcher 选中已补丁 exe → 点“是”。

预期：
- exe 还原到打补丁前状态（无官中补丁时与官方原版逐字节一致）
- 游戏根目录删除 `ime_loader.dll`、`.imefix.bak`、`.imefix.state`
- 官中 `config.ini` 的 `check` 恢复原值

### 4.2 仅还原保留备份

点“否”。

预期：
- exe 还原、DLL 删除
- `.imefix.bak` 保留

### 4.3 官中补丁共存

安装官中补丁后打本 Mod 补丁。

预期：
- `config.ini` 的 `check` 改为 -1，启动无校验弹窗
- 卸载本 Mod 后官中补丁文件完整保留，`check` 恢复

### 4.4 CLI 路径安全

在 exe 所在目录运行 `ime_patcher.exe isaac-ng.exe` 与 `ime_patcher.exe --restore --clean isaac-ng.exe`。

预期：相对路径不能覆盖/删除 exe 自身（v1.4 已修复，曾实测复现过此问题）。

### 4.5 旧版遗留目录清理（v1.5）

在测试机 C 盘和游戏根目录分别创建旧版遗留文件夹：

```text
%APPDATA%\ime-conflict-fix\debug.log
%APPDATA%\ime-conflict-fix\settings.ini
<游戏目录>\ime-conflict-fix\debug.log
```

运行 `ime_patcher.exe --restore --clean <游戏目录>\isaac-ng.exe`。

预期：
- `%APPDATA%\ime-conflict-fix` 整个文件夹被递归删除；
- `<游戏目录>\ime-conflict-fix` 整个文件夹被递归删除；
- 打印 `Removing legacy folder ...` 与 `Legacy ... folder cleaned`。

## 5. 日志验证

- `<游戏目录>\mods\ime-conflict-fix_<id>\debug.log`
- `Documents\My Games\Binding of Isaac Repentance+\log.txt`

关键游戏日志：
- `Running Lua Script: .../mods/ime-conflict-fix_3783304248/main.lua`（执行证明）
- `Lua Debug: [IME_RUN_STARTED]`
- `Lua Debug: [IME_IDLE]`

## 测试记录

已完成的 exe 三阶段/还原/共存测试见 `docs/TEST_RECORDS.md`。
