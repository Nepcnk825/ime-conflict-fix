# IME Conflict Fix — 三阶段 Exe 状态记录

> 测试流程：重下游戏（原版）→ 打官中补丁 → 打我们 mod 补丁
> 记录工具：PowerShell Get-FileHash / PE 头解析
> 说明：Steam 验证文件总数由用户从 Steam 客户端读取（不计 mods 与 data 文件夹）

---

## 阶段 1：无修补（原版）

**记录时间**: 2026-08-14 22:1x
**Steam 验证文件总数**: 1613（用户提供，不含 mods 与 data 文件夹）

| 项 | 值 |
|----|----|
| 文件 | `isaac-ng.exe` |
| 大小 | 9,362,440 字节 |
| MD5 | `2FA5097A4EF74194821D13A5CAE7B304` |
| SHA256 | `3BDFC8BAE0DC7E334B76009D0AD45DFBB16EE5F00C06FFBC3A0094E34D44616B` |
| 文件时间 | 2026-08-14 22:10:59（Steam 下载完成） |
| PE 编译时间 | 2026-04-21 10:40:39 |
| 架构 | x86 (machine 0x014C, PE32) |
| 段列表 | .text, .rdata, .data, .rsrc, .reloc, .bind（6 段，无 .imefix） |

**备注**: 游戏目录无任何本 mod 文件（ime_loader.dll / ime_fix.dll / .imefix.bak / .imefix.state 均不存在）；mods 文件夹中尚无 ime-conflict-fix 订阅。

---

## 阶段 2：官中补丁后

**记录时间**: 2026-08-14 22:26

| 项 | 值 |
|----|----|
| 大小 | 9,362,440 字节（与原版同大小——官中补丁在原有空间内修改，不增大小） |
| MD5 | `2CF863C5E311743B6BB7D3F2A66B27D1` |
| SHA256 | `7122AC28779925B24E23E2416F231322B1470388BD25E2C08665AD8D53B3EA4F` |
| 段列表 | .text, .rdata, .data, .rsrc, .reloc, .bind（6 段，无 .imefix） |
| 游戏根目录新文件 | bootstp.dll (30,208 B) —— 官中补丁部署的加载器 |
| 部署的资源 | resources\packed\repentance_zh.a (23,130,610 B) |
| CN config.ini | check=2119522054（原值，官中补丁未改） |

**重要修正**: 官中补丁 **确实修改 exe**（原版 2FA5097A → 2CF863C5），只是同大小内修改（导入表未加 bootstp.dll，方式未知）。
交叉验证: 2CF863C5 == 旧机器 .imefix.bak / isaac-ng.exe.bak 的 MD5 —— 当初的备份是"官中补丁后"状态而非纯原版；2FA5097A 才是官方原版。

---

## 阶段 3：本 mod 补丁后

**记录时间**: 2026-08-14 22:24

| 项 | 值 |
|----|----|
| 大小 | 9,363,968 字节（原版 +1,528） |
| MD5 | `51518C9D41C5885D97614C5E8B34DAC6` |
| SHA256 | `3FE71CBA37C53C05855867516788AD22A5272B04BCB365A9041222F9B71DEC0A` |
| 段列表 | .text, .rdata, .data, .rsrc, .reloc, .bind, **.imefix**（7 段） |
| 导入 | ime_loader.dll!IME_Init |
| 游戏根目录 | ime_loader.dll (82,432 B, v0.2.0 薄壳) + .imefix.bak + .imefix.state —— 无 ime_fix.dll（v0.4 架构正确） |
| mods 文件夹 | ime-conflict-fix/: ime_fix.bin (84,992 B) + main.lua + metadata.xml + ime_loader.zip |
| CN config.ini | check=-1（原值 2119522054 已存入 .imefix.state） |

**检验结论**: 补丁状态完全符合预期（+1528 字节、.imefix 段、loader 就位、CN 处理正确）

---

## 还原检验（2026-08-14 22:2x，用户再次运行 patcher 点"否"完全清理后）

| 检验项 | 结果 |
|--------|------|
| exe MD5 == 阶段 1 原版 `2FA5097A4EF74194821D13A5CAE7B304` | ✅ True |
| exe 大小 9,362,440 / 6 段无 .imefix | ✅ True |
| 游戏根目录无 ime_loader.dll / ime_fix.dll / .imefix.bak / .imefix.state | ✅ 全部不存在 |
| 官中 config.ini check 恢复 `2119522054` | ✅ True |
| mods\ime-conflict-fix 文件夹 | ✅ 用户已手动删除 |

**结论**: 还原功能完美 —— patcher 一次性清除所有改变，exe 与官方原版逐字节一致，零残留。

---

## 共存状态：官中补丁 + 本 mod 补丁（2026-08-14 22:29 记录）

| 项 | 值 |
|----|----|
| 大小 | 9,363,968 字节（+1,528，基于官中补丁后的 exe） |
| MD5 | `8CD5A4CB301241A429BEEC70765258CF` |
| SHA256 | `E35CD1F7064E410ECE46629FC6D7B7952C5644595F3E3E6374B5AB7A409F015D` |
| 段列表 | 7 段含 .imefix；导入 ime_loader.dll |
| 游戏根目录 | bootstp.dll (30,208 B, 官中) + ime_loader.dll (82,432 B, 本 mod) + .imefix.bak + .imefix.state |
| .imefix.bak | MD5 2CF863C5 == 阶段 2（官中补丁后）—— pre-patch 备份正确 |
| .imefix.state | 2119522054（官中 config 原值，还原时恢复） |
| CN config.ini | check=-1（本 mod patcher 消除官中补丁校验弹窗 ✓） |
| mods/ime-conflict-fix | ime_fix.bin + main.lua + metadata.xml 就位 |

**交叉验证**: 8CD5A4CB == 旧机器（6/30 流程: 官中补丁→我们补丁）打补丁后的 exe MD5 ——
补丁流程确定性验证：相同 pre-patch 状态 → 相同补丁结果（v1.3/v1.4 布局一致）。

---

## 去除本 mod 检验（2026-08-14 22:3x，从共存状态还原）

| 检验项 | 结果 |
|--------|------|
| exe MD5 == 阶段 2 官中补丁后 `2CF863C5` | ✅ True（回到官中补丁状态，而非原版——备份正确） |
| exe 9,362,440 / 6 段无 .imefix | ✅ |
| ime_loader.dll / ime_fix.dll / .imefix.bak / .imefix.state | ✅ 全部清除 |
| bootstp.dll（官中补丁的） | ✅ 保留（本 mod 不动官中文件） |
| CN config.ini check | ✅ 恢复 `2119522054` |
| mods\ime-conflict-fix | ✅ 已删除 |

**结论**: 共存状态下去除本 mod 完美 —— 只清除自己的改变，官中补丁完整保留（与官中补丁共存/互不影响的设计目标达成）。

---

## 当前 MSVC /Brepro 确定性构建（ime_fix v0.4.15-layout / loader v0.2.4 / patcher v1.5，2026-08-17）

记录时间：2026-08-16

| 文件 | 大小 | MD5 | SHA256 |
|------|------|-----|--------|
| `src/ime_fix/ime_fix.dll`（发布为 ime_fix.bin，当前 layout 方案） | 93,696 B | `74E64D06F79B1B61D4E0652DEC67DA9B` | `02B8BF6424F112451ECF9B10BBE5CE34DF6036A4C5628C3B8D1D46D95F8A026F` |
| `src/ime_loader/ime_loader.dll` | 5,632 B | `AB957DFB668F751EC05A072032E5BBDF` | `36B68A4D770AB632E524E89AD13A61D7F79D993B38B7F6BA8E40D1CC6921BAFF` |
| `src/ime_patcher/ime_patcher.exe` | 58,368 B | `0E06D346F4A35615C897AC81C870BA2F` | `D89E706E7ED5EE43935B86F65295A1BE6DB2D6A2E2A3354DC70CCD362A924386` |

> 上表是当前 MinGW 测试构建。正式发布前需在 Windows 上执行 MSVC `/Brepro` 构建并刷新。上一版 MSVC v0.3.6 产物备份在 `build/previous-msvc-v036/`。
