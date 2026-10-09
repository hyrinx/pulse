# 发布和自动更新

源码与发布页：<https://github.com/jimmgreen/pulse>。

从 1.0.3 开始，客户端启动约 15 秒后检查更新，持续运行时每 6 小时再检查一次。检查在后台进行；有新版本时显示可点击的提示，同一运行会话不会重复提示同一个版本。

从 1.0.51 起，点击一次“更新并重启”后会自动下载并校验安装包，等待文件操作和索引迁移完成，保存窗口与标签页状态，再静默安装并重新打开。下载及等待阶段可取消；暂停或等待冲突处理的文件操作须由用户正常继续或取消，更新不会替用户中断它。会话保存失败会中止升级，保留当前窗口。升级重启会恢复本次标签和分栏，即使普通启动偏好是打开默认位置，也不修改该偏好。

Windows 必要的管理员确认仍会出现。客户端通过普通启动安装器，由安装程序自行提权，成功后使用原用户身份启动 `--restore-update-session`。静默更新仅替换原机器安装目录，不自动把免安装版或其他目录迁移成安装版。安装器替换文件前仍执行安全退出握手，自动等待繁忙窗口；不支持握手或保存失败的窗口会阻止升级，不强杀。旧版客户端升级到本版时仍使用旧版入口，新流程在新版客户端中生效。

不想收到更新提醒时，可以在「设置 → 关于与诊断」关闭「自动检查更新」：关闭后不再后台检查，也不弹出新版本提示；「检查更新」按钮仍可手动使用。

1.0.2 及更早的安装包未配置更新源，需要手动安装一次 1.0.3 或更新版本。

## 发布新版本

1. 修改 `version.txt`，例如改为 `1.0.4`。
2. 在 `docs/releases/1.0.4.md` 写本次更新内容。
3. 提交、推送代码，再推送同名版本标签：

```powershell
git add version.txt docs/releases/1.0.4.md
git commit -m "release: prepare 1.0.4"
git push origin main
git tag v1.0.4
git push origin v1.0.4
```

GitHub Actions 的 **Build and publish Pulse** 工作流会构建、测试并打包两种版本。两者全部通过后，才发布 Release 并设为最新版本。单纯提交源码或上传 Actions 构建产物不会触发客户端更新。

| 客户端 | 安装包 | 更新清单 |
| --- | --- | --- |
| Windows 10 / Windows 11 x64 | `PulseSetup-版本.exe` | `update-manifest.json` |
| Windows 8.1 x64 | `PulseSetup-版本-win81.exe` | `update-manifest-win81.json` |

Windows 10 / 11 x64 免安装版 `Pulse-版本-portable-win-x64.zip` 由普通版构建一并打包，与安装包使用同一份通过测试的正式构建；Windows 8.1 版不提供免安装包。

Release 正文自动提供上述系统说明与下载入口。不要重复覆盖已经公开发布的版本；修复发布问题时递增版本号。

## 更新校验

更新清单和安装包默认依次尝试 `https://ghproxy.net/`、`https://gh-proxy.com/` 加原始 Release 文件 URL，最后回退到 GitHub。无需开关或配置。网络错误或非 200 响应时切换到下一来源，每个来源最多请求一次；取消、写入失败和校验失败不会触发来源切换。切换前清空已下载内容，避免拼接不同来源的响应。

只对不含凭据或查询参数的 GitHub Release 文件地址启用公共加速，其他自定义更新域名仍直接访问。ghproxy.net 和 gh-proxy.com 是第三方公共服务，可用性由其运营方决定；未来官方对象存储/CDN 可通过现有清单地址配置接入。

清单采用 ECDSA P-256 签名，客户端内置的公钥位于 `cmake/update-public-key.txt`。签名覆盖版本号、最低系统版本、下载地址和安装包 SHA-256。客户端拒绝未通过签名、校验和不匹配、旧版本或不适用的系统版本；下载只允许 HTTPS，支持 GitHub 的 HTTPS 重定向。

签名私钥保存在仓库的 Actions Secret `PULSE_UPDATE_PRIVATE_KEY` 中，备份应保存在仓库之外，不进入 Git。后续发布沿用此密钥和公钥，避免已安装客户端无法验证新版。

CI 使用 v143 和静态 VC 运行库，LumaText 使用仓库中 `third_party/lumatext` 的固定 SDK。`cmake/lumatext-sdk.json` 固定 `sdk-manifest.json` 的 SHA-256，`scripts/verify_lumatext_sdk.ps1` 在构建前逐一核对 DLL、导入库、头文件、CMake 导出和许可证，避免开发包与正式包混用。构建不依赖本机路径或私有 LumaText 仓库。SDK 仅供构建使用；普通用户下载 Release 顶部对应系统的安装包。

## 1.0.51 定向验证

本地 `pulse` 及受影响测试目标构建无编译警告。更新检查器 40、安装器/校验 47、会话保存与启动偏好 8、本地化 71、实际文件操作退出握手 22 条检查通过。`pulse_content_progress_ui_test --update-only` 的 1313 条检查通过，覆盖等待、启动安装、安装中，简体/繁体/英文、浅深主题、100%/150% 缩放及 360/720/1100 DIP 宽度；已目视等待和安装卡片截图。

Inno Setup 脚本编译及隔离的关闭进程、原目录升级测试通过，包含忙碌超过 10 秒仍自动等待、保存失败拒绝关闭、静默模式不强杀旧版窗口。旧的 `test_installer_upgrade_prefs.ps1` 因引用 HEAD 已不存在的 `DeleteFolderOpenOverride` 无法运行，未修改或弱化它来制造通过结果。上述测试不执行真实安装；真实 UAC、覆盖安装和原用户重启的端到端链路未在本机验证。
