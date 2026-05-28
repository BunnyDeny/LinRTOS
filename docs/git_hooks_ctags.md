# 🔧 Git Hooks 自动更新 ctags 教程

## 背景

每次提交代码、拉取远端更新、切换分支后，`tags` 文件会变过时，导致符号跳转定位不准。通过 Git Hook 在合适的时机自动运行 `ctags -R .`，一劳永逸。

## 三个 Hook 的触发时机

| Hook | 触发场景 | 为什么需要 |
|------|---------|-----------|
| `post-commit` | 本地 `git commit` 后 | 📝 新提交可能改了符号，tags 需更新 |
| `post-merge` | `git pull` / `git merge` 后 | 🌐 远端有新代码合入 |
| `post-checkout` | `git checkout` / `git switch` 后 | 🔀 分支切换后文件树可能不同 |

## 第一步：确认 ctags 已安装

```bash
which ctags
```
如果没安装：

```bash
# Ubuntu / Debian
sudo apt install universal-ctags

# macOS
brew install universal-ctags
```

## 一键脚本（懒人版）

```bash
for h in post-commit post-merge post-checkout; do
  cat > .git/hooks/"$h" << 'SCRIPT'
#!/bin/sh
cd "$(git rev-parse --show-toplevel)" && ctags -R . 2>/dev/null &
SCRIPT
  chmod +x .git/hooks/"$h"
done
```

## 原理说明

```
git commit
  ↓
提交完成
  ↓
触发 .git/hooks/post-commit
  ↓
cd 到仓库根目录
  ↓
ctags -R .    ← 全量扫描，生成 tags 文件
  ↓
&             ← 后台执行，不阻塞终端
```

- 🏃 `&` 让 ctags 在后台运行，不会卡住你的终端
- 📍 `git rev-parse --show-toplevel` 确保无论在仓库的哪个子目录都能正确生成 tags
- 🤫 `2>/dev/null` 抑制 ctags 的警告信息

## 注意事项

1. 📁 `tags` 已被 `.gitignore` 忽略，不会被意外提交
2. 🔍 如果项目很大（>100MB 源码），`ctags -R .` 可能需要几秒，去掉 `&` 可看到进度
3. 🗑️ 删除 hook 直接 `rm .git/hooks/post-commit` 即可
