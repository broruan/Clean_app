# Security policy / 安全政策

## English

Security reports are most useful against the latest published version, with the exact tag or commit, Ubuntu version, architecture, reproduction steps, and impact. Older versions may not receive separate backports. Do not include passwords, authorization tokens, or private user data in a report.

### Reporting a vulnerability

If the repository has GitHub private vulnerability reporting enabled, use **Security → Report a vulnerability** on [this repository](https://github.com/broruan/Clean_app/security). Please use a disposable directory or virtual machine and provide the smallest reproducer possible.

If private reporting is unavailable, open a minimal issue requesting a private reporting channel. Do not publish exploit details, sensitive paths, or a working destructive payload in that initial issue. Ordinary reproducible UI bugs can use the public bug-report template.

No response-time or disclosure-date guarantee is made. A useful coordinated report identifies the affected operation, its expected authorization or file scope, the actual result, and whether the operation was a scan, a preview, or an executed deletion / uninstall.

### Scope and trust boundaries

DiskLens runs with the logged-in user's permissions. Cache cleanup does not elevate privileges. Package removal uses the system's authorization mechanism when required; the application does not ask for or store credentials itself.

Cache cleanup revalidates trusted roots, ownership, mount boundaries, ancestor directories, and file snapshots. Commands use fixed programs and separate argument entries. Destructive operations require a confirmation, but deleted files are not recoverable through DiskLens.

The application is not a sandbox against malicious processes running under the same user account. On POSIX systems, validation and `unlinkat` are separate operations; hostile same-user replacement in that small interval cannot be ruled out atomically. Package preview and execution are likewise separate operations, so avoid concurrent package-manager activity.

Unexpected file access outside configured roots, bypass of protected-package checks, argument injection, unexpected privilege use, and deletion without the intended confirmation are security-relevant reports. An incorrect space estimate alone is normally a correctness issue; include any related unintended destructive effect.

## 简体中文

请优先针对最新发布版本报告问题，提供准确标签或提交、Ubuntu 版本、架构、复现步骤与影响。旧版本不一定单独回补修复。请勿在报告中包含密码、授权令牌或私密用户数据。

### 报告漏洞

如果仓库启用了 GitHub 私密漏洞报告，请在 [仓库 Security 页面](https://github.com/broruan/Clean_app/security) 选择 **Report a vulnerability**。请使用可丢弃目录或虚拟机，提供尽可能小的复现样例。

若无法私密报告，请先提交不含敏感细节的简短 issue，请求私密联系渠道；不要在初始 issue 中公开利用细节、敏感路径或可直接执行的破坏性载荷。普通界面问题可使用公开 bug 模板。

项目不承诺固定响应时间或披露日期。有效报告应明确操作、预期授权或文件范围、实际结果，以及问题出现在扫描、预览还是实际删除 / 卸载阶段。

### 范围与信任边界

DiskLens 使用当前登录用户权限运行，缓存清理不提权。软件包卸载按需使用系统授权机制，程序本身不索取或保存凭据。

缓存清理会复核可信根目录、所有权、挂载边界、祖先目录和文件快照。外部命令使用固定程序与独立参数。破坏性操作需要确认，但 DiskLens 无法恢复已删除文件。

程序不是对抗同一账户下恶意进程的沙箱。POSIX 的校验与 `unlinkat` 并非一个原子操作，无法完全排除同用户进程在该短暂间隔替换文件。软件包预览和执行也彼此分离，请避免并发包管理操作。

超出配置根目录的意外访问、绕过系统软件包保护、参数注入、非预期提权、未按流程确认就删除等问题属于安全相关报告。单纯的空间估算误差通常属于正确性问题；如伴随非预期破坏行为，请一并说明。
