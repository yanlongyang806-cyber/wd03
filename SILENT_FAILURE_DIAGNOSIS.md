# 静默失败诊断指南

## 🔍 问题现象

构建流程"完成"了（没有崩溃），但根本没有生成任何编译产物（.exe/.dll/.obj 等）。

**核心结论**：构建在"准备阶段"就失败了，根本没进入真正的编译。

MSBuild 在解析项目、加载属性表、执行预生成事件或检查依赖时就遇到了致命错误，导致它跳过了所有编译任务，直接"成功退出"（但实际是静默失败）。

## 🚫 常见原因（按优先级）

### 1. 缺失关键子模块/依赖 → 项目引用解析失败

**症状**：
- `.vcxproj` 中引用了 `..\CrossRoads\GameServerLib\GameServerLib.vcxproj`
- 但该文件不存在（因为依赖未拉取）
- MSBuild 无法加载依赖项目 → 跳过整个构建链

**验证方法**：
```powershell
Test-Path "..\CrossRoads\GameServerLib\GameServerLib.vcxproj"
Test-Path "..\Core\Core.vcxproj"
```

如果返回 `False`，就是这个问题。

**解决方案**：
- 如果使用子模块：`git submodule update --init --recursive`
- 如果手动管理：按照 `ADD_DEPENDENCIES.md` 添加依赖

### 2. PropertySheets 引用路径错误

**症状**：
- 项目通过 PropertySheets 设置 `$(OutDir)` 或 `$(IntDir)`
- 如果 .props 文件缺失或其中引用的路径不存在
- 可能导致 MSBuild 认为"无需构建"或输出路径非法

**检查**：
```xml
<!-- 在 NNOGameServer.vcxproj 中 -->
<Import Project="..\PropertySheets\GeneralSettings.props" />
```

确保 PropertySheets 文件存在，并且内容合理。

**状态**：✅ 已修复路径（`..\..\` → `..\`）

### 3. Pre-build event 命令失败但被忽略

**症状**：
- 运行生成头文件的脚本（如 structparser.exe）
- 如果脚本因缺少工具或路径错误而失败
- 某些配置下 MSBuild 会中止构建，但日志不明显

**检查 build.log**：
```
Error executing pre-build command.
The command "..." exited with code 9009.
```

**状态**：✅ 已修改为可选（如果 structparser.exe 不存在会跳过）

### 4. 项目中没有有效的源文件

**症状**：
- 如果 `NNOGameServer.vcxproj` 的 `<ClCompile>` 列表为空
- 或所有源文件被条件编译排除
- MSBuild 会认为"无事可做"，直接结束

**检查**：
```powershell
Select-String -Path "NNOGameServer.vcxproj" -Pattern "<ClCompile.*Include"
```

应返回多个源文件条目。

## ✅ 最高效排查步骤

### 步骤 1：确认子模块已拉取

```bash
git submodule status
```

如果输出以 `-` 开头（如 `-e8a3c... CrossRoads`）→ 未检出

执行：
```bash
git submodule update --init --recursive
```

### 步骤 2：检查关键文件是否存在

```powershell
# PowerShell
@(
    "..\CrossRoads\GameServerLib\GameServerLib.vcxproj",
    "..\Core\Core.vcxproj",
    "PropertySheets\GeneralSettings.props",
    "NNOLogging.c"
) | ForEach-Object {
    if (Test-Path $_) {
        Write-Host "✅ $_ exists" -ForegroundColor Green
    } else {
        Write-Host "❌ MISSING: $_" -ForegroundColor Red
    }
}
```

### 步骤 3：检查项目引用

```powershell
# 从项目文件中提取所有项目引用
$projContent = Get-Content "NNOGameServer.vcxproj" -Raw
$refs = [regex]::Matches($projContent, '<ProjectReference\s+Include="([^"]+)"')
foreach ($ref in $refs) {
    $path = $ref.Groups[1].Value
    if (Test-Path $path) {
        Write-Host "✅ $path" -ForegroundColor Green
    } else {
        Write-Host "❌ MISSING: $path" -ForegroundColor Red
    }
}
```

### 步骤 4：手动触发 MSBuild 并捕获详细日志

```bash
msbuild NNOGameServer.vcxproj /p:Configuration=Debug /p:Platform=Win32 /v:detailed /fl /flp:logfile=full_build.log
```

然后搜索 `full_build.log` 中的：
- `error`
- `not found`
- `skipped`
- `pre-build`
- `CrossRoads`

## 📊 工作流改进

新的工作流现在会：

1. ✅ **构建前验证**：
   - 检查源文件列表
   - 验证关键文件存在
   - 检查项目引用
   - 验证 PropertySheets

2. ✅ **检测静默失败**：
   - 检查是否有编译活动
   - 检查是否有链接活动
   - 检测跳过的任务
   - 识别项目引用错误

3. ✅ **详细错误报告**：
   - 项目引用错误
   - Pre-build 事件错误
   - PropertySheets 错误
   - 头文件错误

## 💡 总结

| 现象 | 最可能原因 | 解决方案 |
|------|-----------|---------|
| Build "succeeds" but no .exe | 子模块缺失 → 项目引用断裂 | `git submodule update --init --recursive` 或添加依赖 |
| PropertySheets 加载失败 | 路径宏未定义 | 检查 .props 文件和项目结构 |
| Pre-build event 失败 | 脚本/工具缺失 | 查看 build.log 中 pre-build 错误 |
| 无源文件编译 | 项目配置错误 | 检查 .vcxproj 中的 `<ClCompile>` |

## 🔍 快速诊断命令

```bash
# 1. 检查子模块状态
git submodule status

# 2. 检查关键文件
Test-Path "..\CrossRoads\GameServerLib\GameServerLib.vcxproj"
Test-Path "..\Core\Core.vcxproj"

# 3. 检查源文件
Select-String -Path "NNOGameServer.vcxproj" -Pattern "<ClCompile.*Include" | Measure-Object

# 4. 检查项目引用
Select-String -Path "NNOGameServer.vcxproj" -Pattern "<ProjectReference" | ForEach-Object { $_.Line }
```

## 📝 下一步

1. **运行诊断命令**确认问题
2. **查看最新的构建日志**（工作流现在会显示详细验证信息）
3. **根据诊断结果**：
   - 如果缺少依赖 → 按照 `ADD_DEPENDENCIES.md` 添加
   - 如果子模块未初始化 → 运行 `git submodule update --init --recursive`
   - 如果项目引用错误 → 修复路径或添加缺失的项目

