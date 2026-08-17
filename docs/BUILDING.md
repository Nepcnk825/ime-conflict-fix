# 构建与打包

## 环境要求

- Windows 10/11
- Visual Studio 2022，安装 “Desktop development with C++”
- 只能构建 **x86**：Isaac Repentance+ 是 PE32（machine 0x14C）
- 可选：Python 3.8+（运行打包脚本）

## 一键构建

仓库根目录执行：

```bat
build_all.bat
```

脚本会依次构建 `ime_fix.dll`、`ime_loader.dll`、`ime_patcher.exe`；任何一步失败立即停止。

## 分组件构建

```bat
cd src\ime_fix     && build.bat   :: 功能 DLL（创意工坊内发布为 ime_fix.bin）
cd src\ime_loader  && build.bat   :: 薄壳加载器（游戏根目录常驻）
cd src\ime_patcher && build.bat   :: PE 导入表补丁工具
```

三个 `build.bat` 都会自动查找 VS 2022 的 `vcvars32.bat`：
- `D:\Software\Microsoft Visual Studio\2022\Community\...`（当前开发机非标准路径）
- 标准 Community / Professional / Enterprise / BuildTools 路径
- 找不到时回退到 `cl.exe` 已在 PATH 的环境

## 链接参数

| 组件 | 关键参数 |
|------|----------|
| `ime_fix.dll` | `/GS- /LD /EXPORT:IME_Init`；libs：`user32 kernel32 imm32 winmm`；无 CRT |
| `ime_loader.dll` | `/GS- /LD /EXPORT:IME_Init`；libs：`user32`（`wsprintfW` 属于 user32，不是 kernel32） |
| `ime_patcher.exe` | `/W3 /ENTRY:mainCRTStartup`；libs：`user32 comdlg32 advapi32`；嵌入 asInvoker manifest |

公共参数：`/O2 /MT /utf-8 /MACHINE:X86 /SUBSYSTEM:WINDOWS /Brepro`。

`/Brepro` 用于确定性构建，同一源码 + VS 2022 应得到与 `docs/TEST_RECORDS.md` 中校验码一致的产物。

## 构建后校验

```bat
dumpbin /imports ime_fix.dll
dumpbin /imports ime_loader.dll
dumpbin /imports ime_patcher.exe
```

预期 ime_fix 依赖只包含 `kernel32.dll`、`user32.dll`、`winmm.dll`（loader 只含 `kernel32/user32`；patcher 还有 `comdlg32.dll`、`advapi32.dll`），无 CRT 运行时依赖。

## 打包创意工坊上传目录

构建完成后执行：

```bat
python tools\package_workshop.py
```

或 Python 环境命令：

```bash
python3 tools/package_workshop.py
```

脚本会生成/刷新 `workshop_upload/mod/`：

- `main.lua`、`metadata.xml`：来自仓库根目录（唯一真源）
- `ime_fix.bin`：`src/ime_fix/ime_fix.dll` 改名复制
- `ime_loader.zip`：打包 `src/ime_patcher/ime_patcher.exe` + `src/ime_loader/ime_loader.dll`

脚本还会：
- 校验三个二进制均为 PE32 x86，防止误打包 x64
- 用固定时间戳生成 `ime_loader.zip`，同一组输入重复打包应得到相同哈希
- 打印每个产物的 SHA256/MD5/大小
- 只生成暂存目录，**不会**执行创意工坊上传

## 当前 MSVC /Brepro 确定性构建校验码（2026-08-17）

| 文件 | 大小 | MD5 | SHA256 |
|------|------|-----|--------|
| `ime_fix.dll` / `ime_fix.bin`（当前 layout 方案） | 93,696 B | `74E64D06F79B1B61D4E0652DEC67DA9B` | `02B8BF6424F112451ECF9B10BBE5CE34DF6036A4C5628C3B8D1D46D95F8A026F` |
| `ime_loader.dll` | 82,944 B | `7E9F6A9D396A2EFE90DEAD7EDAE6085A` | `DFE7CF89E6ACFB7D063BE1CABC37B082C282E0F8A0A039F0B580754F6C2D7922` |
| `ime_patcher.exe` | 137,216 B | `C6C25A202704F56C06C5CC86B976B4F4` | `5C34D4038DFBA3097FCA935064214CF1DA4A4F212FEB4457EE72204675419659` |

> 这是用户在 Windows 10 + Visual Studio 2022 17.14 下执行 `build_all.bat` 生成的确定性构建。上一版 MSVC v0.3.6 产物备份在 `build/previous-msvc-v036/`。
