# 输入法兼容性说明

本文覆盖当前源码行为（ime_fix v0.4.15-layout）。

## 工作原理

DLL 会在安全时机向游戏主窗口发送 `WM_INPUTLANGCHANGEREQUEST`，把当前键盘布局
切到英文（US）：

1. 启动窗口内检测到 Mod 执行（主菜单即切换）
2. 开局信号 `[IME_RUN_STARTED]`
3. 玩家空闲 3 秒信号 `[IME_IDLE]`
4. 可选：MCM 开启“锁定英文布局”后，前台时每 500ms 检查并切回英文
5. 可选：联机禁用 Mod 也生效（检测到联机大厅后保持英文布局）

这是可逆的键盘布局切换，不会调用进程级 `ImmDisableIME(-1)`，退出游戏后
系统布局不受影响。

## 兼容性结论

| IME | 状态 | 说明 |
|-----|------|------|
| 微软拼音（Win10/Win11） | 完整支持 | 切换到英文（US）布局后不再吞键；聊天时可实验性切回 |
| 搜狗拼音 | 大多数情况可用 | 布局切换为 Windows 标准机制，异常请反馈 |
| 纯英文系统 | 无副作用 | 无需切换，DLL 为 no-op |
| 其他 TSF IME | 通常可用 | 切换的是键盘布局，不受 TSF/IMM32 差异影响 |

## 已知限制

| 限制 | 说明 |
|------|------|
| 启动后才启用 Mod | 返回主菜单瞬间切换仍不可靠，等待开局信号或 3 秒空闲信号 |
| 联机聊天 | 中文聊天依赖实验性 Enter 启发式，效果不稳定 |
| 作用域 | 只影响 `isaac-ng.exe` 进程的前台窗口布局请求，不影响其他应用 |
| 平台 | 仅 Windows；非 Windows 上 Lua Mod 自动跳过 |

## 为什么不用其他 API

| 方案 | 结果 |
|------|------|
| `ImmSetOpenStatus` / `ImmSetConversionStatus` | 现代 TSF IME 忽略（IMM32 兼容层静默失败） |
| `ImmAssociateContext` | 同样被 TSF 忽略 |
| `SendInput(Ctrl+Space)` / `ImmSimulateHotKey` | 跨 IME 不可靠 |
| `WM_INPUTLANGCHANGEREQUEST` + 现有英文布局 | 可逆、异步、不会卡 UI；当前方案 |

## 报告兼容性问题

请提供：

1. 输入法名称和版本
2. Windows 版本
3. 游戏版本（主菜单左下角完整版本号）
4. `<游戏目录>\mods\ime-conflict-fix_<id>\debug.log` 内容
5. 启用 Mod 后具体操作步骤（何时启用、等待多久、现象）
