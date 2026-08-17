# IME 冲突修复 - The Binding of Isaac: Repentance+

中文输入法开启时，以撒会吞掉 WASD 等按键，或弹出拼音候选框。本 Mod 在游戏启动后自动把游戏窗口切换到英文键盘布局。

本仓库公开源码，是为了方便审查涉及 DLL 加载与 PE 补丁的代码。

## 项目结构

```text
ime-conflict-fix/
├── main.lua                  # Lua 侧：执行标记、游戏内设置信号
├── metadata.xml              # 创意工坊元数据与描述
├── build_all.bat             # 一键构建三个组件
└── src/
    ├── ime_fix/              # 功能 DLL 源码（发布为 ime_fix.bin）
    ├── ime_loader/           # 游戏根目录加载器源码
    └── ime_patcher/          # PE 导入表补丁工具源码
```

其他本地开发文档与打包工具不上传，仓库只保留构建和审查所需内容。

当前版本：`main.lua 0.3.13` · `ime_fix 0.4.15-layout` · `ime_loader 0.2.4` · `ime_patcher v1.5`。

## 安装

### 方式一：Steam 创意工坊订阅（推荐）

1. 订阅本 Mod。
2. 进入游戏目录的 `mods\ime-conflict-fix_<id>` 文件夹，解压 `ime_loader.zip`。
3. 运行 `ime_patcher.exe` 打补丁。
4. 启动游戏，在 Mods 菜单启用 “IME Conflict Fix”。

### 方式二：从源码构建

1. 克隆本仓库。
2. 运行 `build_all.bat`（需要 Visual Studio 2022，构建 x86）。
3. 在 `mods\` 下新建 `ime-conflict-fix`，放入 `main.lua`、`metadata.xml`，并把 `ime_fix.dll` 重命名为 `ime_fix.bin` 放入。
4. 运行 `ime_patcher.exe` 打补丁。
5. 启动游戏，在 Mods 菜单启用本 Mod。

### 打补丁（一次性）

运行 `ime_patcher.exe`，它会：

- 修改 `isaac-ng.exe` 的导入表，让游戏启动时加载 `ime_loader.dll`；
- 生成 `isaac-ng.exe.imefix.bak` 备份；
- 把 `ime_loader.dll` 复制到游戏根目录；
- 装有官中补丁时，自动处理其启动校验配置。

Mod 后续更新不需要重新打补丁；只有 Steam 更新游戏本体后才需要重打。

旧版本用户：请先运行旧版 `ime_patcher.exe` 卸载/还原，再解压本版本 `ime_loader.zip` 使用新的 `ime_patcher.exe` 安装。

### 游戏内配置

安装了 **Mod配置菜单（中文版）** 的玩家，可在游戏内设置：

- 锁定英文布局
- 联机禁用 Mod 也生效
- 联机聊天切换（实验性）

未安装 Mod配置菜单（中文版）的玩家，可在 mod 文件夹手动创建 `settings.ini`：

```ini
[input]
lock_layout=1

[online]
enabled=0
chat_toggle=0
```

`settings.ini` 不会被创意工坊覆盖；只有 Mod配置菜单（中文版）修改联机选项后，DLL 才会自动创建或更新该文件。

## 如何确认生效

游戏窗口化运行时，主菜单阶段任务栏输入法指示器应显示英文（ENG）。开启“锁定英文布局”后，手动切回中文也会立即被切回英文。

说明：如果系统原本没有英文（美式键盘）布局，Mod 会加载 Windows 自带的英文（US）布局兜底。退出游戏后该布局可能仍留在语言列表中，但设备重启后会自动消失；建议直接安装美式键盘。

## 卸载

运行 `ime_patcher.exe` 并选择已补丁的 `isaac-ng.exe`：

- 还原 exe；
- 删除游戏根目录的 `ime_loader.dll`；
- 恢复官中补丁配置；
- 清理旧版本可能遗留的日志/配置文件夹。

备份丢失时，可在 Steam 中验证游戏文件完整性恢复原版。随后在创意工坊取消订阅即可。

## 常见问题

### Q: 会影响其他游戏或应用吗？
A: 不会。只请求以撒游戏窗口切换键盘布局，不注入其他进程，不联网。

### Q: 可以和忏悔+官中补丁一起用吗？
A: 可以，已实测兼容，无安装顺序要求。

### Q: 可以和 REPENTOGON 一起用吗？
A: 未测试。需要同时使用的玩家请自行测试；若不生效，请卸载本 Mod。本 Mod 暂不针对 REPENTOGON 做专门兼容。

### Q: 启动后才在游戏内启用 Mod，多久生效？
A: 游戏会先重载 Mod；一旦游戏日志出现本 Mod 的执行记录，DLL 会立即请求英文布局。该请求是异步的，不会卡住界面。若游戏因此自动重启，下次启动会在主菜单直接生效。

### Q: 联机模式下输入法会怎样？
A: 默认情况下，联机禁用 Mod 后本 Mod 不生效，中文聊天正常。
若在 Mod配置菜单（中文版）中开启“联机禁用 Mod 也生效”，DLL 会在主菜单就保持英文布局。开启实验性“联机聊天切换”后，第一次 Enter 切到中文，消息发出后自动切回英文。

### Q: 游戏更新后需要重新打补丁吗？
A: 需要。Steam 更新会覆盖 `isaac-ng.exe`，重新运行一次 `ime_patcher.exe` 即可。

### Q: Mod 更新后需要重新打补丁吗？
A: 不需要。加载器会直接加载创意工坊同步的新版 `ime_fix.bin`。

### Q: 这个补丁会改坏游戏吗？
A: 不会。补丁只追加一个 DLL 导入条目，并在修改前备份 exe。任何情况下都可通过 Steam 验证游戏文件完整性恢复。

### Q: 为什么需要 DLL？
A: 输入法管理需要 Windows API，以撒的 Lua Mod API 无法完成。DLL 只使用 Windows 系统 API，不注入其他进程，不联网。

### Q: 搜狗 / 小狼毫等输入法能用吗？
A: 微软拼音已验证。其他输入法使用同一 Windows 键盘布局机制，通常可用；如不生效请在 GitHub Issues 反馈输入法名称与版本。

### Q: Steam Deck / Linux 能用吗？
A: 目前仅支持 Windows。非 Windows 系统上 Lua 部分自动跳过，不影响游戏运行。

### Q: 杀毒软件报毒怎么办？
A: 本 Mod 的 DLL/patcher 会修改游戏 exe 或加载 DLL，可能被杀软误报。源码与构建参数公开，校验码见下文；可自行构建核对，或将文件加入白名单。

## 可复现构建与校验码

构建使用 Visual Studio 2022 x86 + `/Brepro`。构建命令：

```bat
build_all.bat
certutil -hashfile src\ime_fix\ime_fix.dll MD5
certutil -hashfile src\ime_loader\ime_loader.dll MD5
certutil -hashfile src\ime_patcher\ime_patcher.exe MD5
```

当前 MSVC `/Brepro` 确定性构建校验码（2026-08-17）：

| 文件 | 大小 | MD5 | SHA256 |
|------|------|-----|--------|
| ime_fix.dll / ime_fix.bin | 93,696 B | `74E64D06F79B1B61D4E0652DEC67DA9B` | `02B8BF6424F112451ECF9B10BBE5CE34DF6036A4C5628C3B8D1D46D95F8A026F` |
| ime_loader.dll | 82,944 B | `7E9F6A9D396A2EFE90DEAD7EDAE6085A` | `DFE7CF89E6ACFB7D063BE1CABC37B082C282E0F8A0A039F0B580754F6C2D7922` |
| ime_patcher.exe | 137,216 B | `C6C25A202704F56C06C5CC86B976B4F4` | `5C34D4038DFBA3097FCA935064214CF1DA4A4F212FEB4457EE72204675419659` |

已核验：`ime_fix.dll` 只导入 `USER32/KERNEL32/WINMM`；`ime_loader.dll` 只导入 `USER32/KERNEL32`。

## 技术原理（给想了解的人）

1. `ime_patcher.exe` 给 `isaac-ng.exe` 追加一个 DLL 导入条目，使游戏启动时加载 `ime_loader.dll`。
2. `ime_loader.dll` 位于游戏根目录，只负责找到 mods 文件夹中的 `ime_fix.bin` 并加载，Mod 更新时无需重打补丁。
3. `ime_fix.bin` 轮询游戏日志确认 Lua Mod 是否运行，确认后向游戏窗口发送 `WM_INPUTLANGCHANGEREQUEST` 切换到英文键盘布局。游戏在前台时，若开启锁定或联机保持，会持续检查并在被切回中文时立即重新切回英文。
4. `main.lua` 负责写入 Mod 执行标记与设置信号。联机大厅、聊天广播等状态通过游戏日志识别；`settings.ini` 仅用于未安装 Mod配置菜单（中文版）时的手动配置，或联机禁用 Mod 时让 DLL 读取已保存的联机选项。

## 参考

- 忏悔+官中补丁：兼容其启动校验。
- IMBlocker（Minecraft 输入法修复）：思路参考。

本项目开发过程使用了 AI 辅助，源码与构建产物均经过人工审查和实机测试。

## 反馈

- GitHub Issues: https://github.com/Nepcnk825/ime-conflict-fix/issues
