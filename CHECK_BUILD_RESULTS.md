# 如何检查构建结果

## 从 GitHub Actions 查看构建状态

### 1. 查看工作流运行列表

访问：https://github.com/yanlongyang806-cyber/wd03/actions

你会看到：
- ✅ 绿色勾 = 工作流步骤成功（但可能没有生成输出文件）
- ❌ 红色叉 = 工作流步骤失败
- ⚪ 灰色圆圈 = 工作流步骤完成（中性状态）

### 2. 查看具体构建日志

点击任意构建运行，查看详细日志：

**关键检查点：**

1. **依赖检查步骤**
   - 查找 "=== Dependency Check ==="
   - 确认 CrossRoads、Core、libs 是否都找到

2. **构建步骤**
   - 查找 "=== Build Analysis ==="
   - 检查：
     - `Compilation Activity: true` ✅
     - `Linking Activity: true` ✅
     - `Error Count: 0` ✅
     - `Outputs Found: true` ✅

3. **输出文件检查**
   - 查找 "=== Checking for Build Outputs ==="
   - 应该看到：`OK: Found target executable at: Debug\GameServer.exe`

### 3. 常见问题

#### 问题 1: 工作流显示成功但无输出文件

**症状：**
- 工作流步骤显示 ✅ 成功
- 但 "Checking for Build Outputs" 显示 "ERROR: No build output files found!"

**原因：**
- libs 目录缺失
- 项目引用找不到
- 编译被跳过（静默失败）

**解决：**
- 检查构建日志中的 "Dependency Check" 部分
- 确认所有项目引用是否找到
- 添加缺失的依赖

#### 问题 2: 构建失败但退出码为 0

**症状：**
- MSBuild 返回退出码 0（成功）
- 但没有编译任何文件

**原因：**
- 依赖缺失导致 MSBuild 跳过编译
- 预构建步骤失败但被忽略

**解决：**
- 查看构建日志中的错误信息
- 检查 "Project reference missing" 错误
- 添加缺失的依赖

### 4. 下载构建产物

如果构建成功，可以下载构建产物：

1. 点击构建运行
2. 滚动到底部
3. 在 "Artifacts" 部分下载 `build-outputs`

### 5. 查看构建日志文件

构建日志会自动上传为 artifact：

1. 点击构建运行
2. 在 "Artifacts" 部分下载 `build-log`
3. 打开 `build.log` 查看详细错误

## 当前状态检查清单

- [ ] 访问 GitHub Actions 页面
- [ ] 点击最新的构建运行
- [ ] 查看 "Dependency Check" 步骤
- [ ] 查看 "Build Analysis" 步骤
- [ ] 查看 "Checking for Build Outputs" 步骤
- [ ] 检查是否有错误信息
- [ ] 下载 build-log artifact（如果有）

## 如果构建失败

1. **查看错误信息**
   - 在构建日志中搜索 "ERROR" 或 "CRITICAL"
   - 查看最后 30 行日志（工作流会自动显示）

2. **检查依赖**
   - 运行 `.\quick_diagnosis.ps1`（本地）
   - 或查看 GitHub Actions 中的依赖检查输出

3. **修复问题**
   - 根据错误信息修复
   - 通常需要添加缺失的依赖（libs）

4. **重新触发构建**
   - 推送新的提交
   - 或手动触发工作流

## 相关文档

- `BUILD_STATUS_SUMMARY.md` - 构建状态总结
- `MISSING_LIBS.md` - libs 缺失说明
- `HOW_TO_VIEW_BUILD_LOG.md` - 如何查看构建日志

