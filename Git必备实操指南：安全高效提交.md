# Git \+ GitHub 必备实操指南：安全高效提交与忽略

# Git \+ GitHub 必备实操指南：只提交想要的、忽略不想的、看清改了啥

我给你整理一套**最实用、最不踩坑、工作中天天用**的 Git 命令，全部是**新手能直接照抄**的版本，不讲虚的理论，只讲怎么安全提交代码。

## 一、先搞懂 3 个核心状态（不踩坑的基础）

你只需要记住这 3 条：

1. **工作区**：你电脑上改的文件

2. **暂存区**：`git add` 后准备提交的文件

3. **本地仓库**：`git commit` 后保存的版本

4. **远程仓库**：`git push` 后传到 GitHub 的代码

---

# 二、最常用：查看当前改了什么（提交前必看！）

## 1\. 查看哪些文件被修改 / 新增 / 删除

```bash
git status
```

会显示：

- 红色：**还没 add** 的修改

- 绿色：**已经 add** 准备提交

## 2\. 查看文件具体改了什么（超级重要）

```bash
git diff
```

- 红色 `\-`：删除的行

- 绿色 `\+`：新增的行

只看**某个文件**的改动：

```bash
git diff 文件名
```

已经 `add` 但还没 `commit` 的文件，想看改动：

```bash
git diff --staged
```

---

# 三、只提交「指定文件 / 指定目录」不提交乱七八遭的

这是新手最容易把垃圾文件提交上去的坑！

## 1\. 提交单个文件

```bash
git add 文件名
```

例子：

```bash
git add index.html
git add src/main.js
```

## 2\. 提交整个目录（比如 src 目录）

```bash
git add src/
```

## 3\. 提交多个指定文件

```bash
git add 文件1 文件2 文件3
```

## 4\. 提交所有修改（⚠️ 谨慎使用！）

```bash
git add .
```

⚠️ 不建议新手盲目用 `git add \.`，容易把配置文件、日志、缓存提交上去。

---

# 四、必须会：忽略文件（\.gitignore）避免提交垃圾

这是 GitHub 提交**最关键的一步**，90% 的坑都来自没忽略不该提交的文件。

## 1\. 创建忽略文件

在项目根目录创建：

```Plain Text
.gitignore
```

## 2\. 直接复制我这个通用模板（前端 / 后端都能用）

```Plain Text
# 编辑器自动生成
.idea/
.vscode/
*.swp
*.swo
*~

# 系统文件
.DS_Store
Thumbs.db

# 依赖包
node_modules/
vendor/
composer.lock
package-lock.json
yarn.lock

# 构建产物
dist/
build/
out/
output/

# 日志
logs/
*.log
npm-debug.log*

# 环境配置（绝对不能提交！）
.env
.env.local
.env.development
.env.production

# 测试/缓存
coverage/
.temp/
```

## 3\. 忽略规则写法

- 忽略文件：`文件名`

- 忽略目录：`目录名/`

- 忽略某类文件：`\*\.log` `\*\.zip`

- 不忽略某个文件（例外）：`\!/src/config\.js`

## 4\. 已经提交过的文件，想以后忽略

```bash
git rm --cached 要忽略的文件
```

然后把它加入 `\.gitignore` 即可。

---

# 五、提交 \+ 推送到 GitHub（安全流程）

## 标准不踩坑流程（背下来）

```bash
# 1. 查看改了什么
git status

# 2. 只添加你想提交的文件
git add 你要提交的文件/目录

# 3. 再确认一遍要提交什么（绿色就是要提交）
git status

# 4. 提交到本地仓库（必须写说明）
git commit -m "这里写你做了什么修改"

# 5. 推送到 GitHub
git push
```

---

# 六、高级实用技巧（工作必备）

## 1\. 查看历史提交记录

```bash
git log
```

简洁版：

```bash
git log --oneline
```

## 2\. 查看某次提交具体改了什么

```bash
git show 提交ID
```

## 3\. 撤销 add（不想提交了）

```bash
git reset HEAD 文件名
```

## 4\. 撤销文件修改（恢复成未修改）

⚠️ 会丢失修改，谨慎使用

```bash
git checkout -- 文件名
```

## 5\. 只撤销最近一次 commit（不删除代码）

```bash
git reset --soft HEAD^
```

---

# 七、最容易踩的 5 个大坑（我帮你避开）

1. **盲目用 git add \.** → 会提交密码、缓存、依赖

2. **没写 \.gitignore** → 账号密码泄露、仓库巨大、冲突爆炸

3. **不看 git status 直接提交** → 不知道自己提交了啥

4. **commit 不写说明** → 以后根本找不到改了什么

5. **直接在 main 分支开发** → 一出错全完蛋

---

# 八、我给你一个「万能安全提交模板」

以后提交代码直接按这个顺序敲：

```bash
git status
git add 你要的文件
git status
git commit -m "feat: 新增XX功能 / fix: 修复XXbug"
git push
```

---

## 总结（你只需要记住这 4 组命令）

1. **看修改**：`git status` / `git diff`

2. **提交指定文件**：`git add 文件/目录`

3. **忽略文件**：`\.gitignore`（必须用）

4. **安全提交**：add → status → commit → push

如果你愿意，我还能根据你的 \\*\\* 项目类型（前端 / 后端 / Java/Python/ 小程序）\\*\\* 给你生成专属的 `\.gitignore`，让你永远不会提交不该提交的文件！

> （注：文档部分内容可能由 AI 生成）
