# 发布与创意工坊上传流程

> 当前仓库/创意工坊的具体状态记录在内部 `AGENTS.md` 与 `.dev/`，不要写入公开文档。

## 发布前检查清单（v0.5.0 待发布）

1. 用户已完成 `docs/TESTING.md` 的联机/聊天/主菜单测试（当前 v0.4.15-layout 已通过）。
2. 在 Windows 使用 Visual Studio 2022 执行确定性构建（MSVC `/Brepro`）。
3. 记录三个 MSVC 产物的 MD5/SHA256/大小，替换以下文档中的“当前 MinGW 测试构建”表：
   - `README.md`
   - `docs/BUILDING.md`
   - `docs/TEST_RECORDS.md`
4. 确认版本号：
   - `main.lua`：0.3.12
   - `ime_fix`：0.4.15-layout
   - `ime_loader`：0.2.4
   - `ime_patcher`：v1.5
   - `metadata.xml`：0.8（更新日志已写入 v0.5.0 条目）
5. 运行 `python tools\package_workshop.py`，确认打印哈希与 MSVC 产物一致。
6. 人工核对 `workshop_upload/mod/` 文件列表与描述。
7. 仅在用户明确下达上传命令后执行上传。

## Windows 确定性构建命令

```bat
cd /d <repo>
build_all.bat
python tools\package_workshop.py
certutil -hashfile src\ime_fix\ime_fix.dll MD5
certutil -hashfile src\ime_loader\ime_loader.dll MD5
certutil -hashfile src\ime_patcher\ime_patcher.exe MD5
```

把三个 MD5 发给 AI 会话，用于刷新 README/描述中的校验码表，然后再上传。

## 上传包结构

```
workshop_upload/mod/
├── main.lua          # Lua 信号源
├── metadata.xml      # 元数据 + 完整 BBCode 描述
├── ime_fix.bin       # ime_fix.dll 改名（功能 DLL，loader 直接加载）
└── ime_loader.zip    # ime_patcher.exe + ime_loader.dll（一次性打补丁工具包）
```

## Steam ModUploader 限制与对策

- ModUploader 禁用 `.exe/.dll/.ini/.com/.bat/.cmd/.pif`
- 不禁 `.zip` 和 `.bin`
- 因此 `ime_patcher.exe` + `ime_loader.dll` 打包成 `ime_loader.zip`
- `ime_fix.dll` 改名为 `ime_fix.bin` 直接放 mod 目录

## 上传步骤（必须用户明确下达命令后执行）

1. 确认 `workshop_upload/mod/` 内容无误。
2. 使用平台共享工具 `tools/ModUploader/ModUploader.exe`（Windows 侧，多 mod 共用）上传。
3. 上传后核对创意工坊页面描述、版本号和下载内容。

## 卸载语义（GUI/CLI，patcher v1.5）

- 双击 patcher 选中已补丁 exe：
  - **点“是”**：还原 exe + 删除 `ime_loader.dll` + 恢复官中 `config.ini` + 删除 `.imefix.bak`/`.imefix.state` + 递归清理旧版遗留目录
  - **点“否”**：仅还原，保留 `.imefix.bak`；仍会清理旧版遗留目录
- CLI：
  - `ime_patcher.exe isaac-ng.exe`
  - `ime_patcher.exe --restore --clean "游戏目录\isaac-ng.exe"`
- v1.5 额外清理：
  - `%APPDATA%\ime-conflict-fix`
  - `<游戏目录>\ime-conflict-fix`

## Git 注意事项

- `workshop_upload/`、`.dev/`、`AGENTS.md`、`*.dll/*.exe/*.bin` 均不入库。
- 推送前只保留已经用户实测确认的修复；未经验证的实验提交不得推送。
- 发布 GitHub Release 时，二进制应作为 release assets，仓库本体保持纯源码。
