#include "setup_strings.h"

#include <windows.h>

#include "setup_common.h"

namespace pulse::setup {
namespace {

constexpr const wchar_t* kTable[][3] = {
    {L"Pulse 安装程序", L"Pulse 安裝程式", L"Pulse Setup"},  // WindowTitle
    {L"为 Windows 打造的现代文件管理器 —— 浏览、搜索、整理，更顺手。", L"為 Windows 打造的現代檔案管理員 —— 瀏覽、搜尋、整理，更順手。", L"A modern file manager for Windows — browse, search and organize, effortlessly."},  // Tagline
    {L"立即安装", L"立即安裝", L"Install now"},  // Install
    {L"立即升级", L"立即升級", L"Upgrade now"},  // Upgrade
    {L"安装到", L"安裝到", L"Install to"},  // InstallTo
    {L"需要 %1 MB", L"需要 %1 MB", L"%1 MB required"},  // Needs
    {L"我已阅读并同意", L"我已閱讀並同意", L"I accept the"},  // Agree
    {L"许可协议", L"授權合約", L"License Agreement"},  // License
    {L"自定义安装", L"自訂安裝", L"Custom install"},  // Custom
    {L"安装位置", L"安裝位置", L"Location"},  // SecLocation
    {L"程序目录", L"程式目錄", L"App folder"},  // AppDir
    {L"升级时会沿用当前目录", L"升級時會沿用目前目錄", L"Upgrades keep the current folder"},  // AppDirSub
    {L"浏览…", L"瀏覽…", L"Browse…"},  // Browse
    {L"%1 可用 %2", L"%1 可用 %2", L"%2 free on %1"},  // SpaceFree
    {L"搜索与索引", L"搜尋與索引", L"Search & indexing"},  // SecSearch
    {L"启用全盘文件索引", L"啟用全磁碟檔案索引", L"Enable full-disk file index"},  // OptIndex
    {L"推荐", L"建議", L"Recommended"},  // Recommended
    {L"安装 PulseIndex 后台服务，秒级搜索全部 NTFS 磁盘", L"安裝 PulseIndex 背景服務，秒級搜尋所有 NTFS 磁碟", L"Installs the PulseIndex service for instant search across NTFS drives"},  // OptIndexSub
    {L"索引位置", L"索引位置", L"Index location"},  // IndexDir
    {L"服务器文件夹可稍后在设置中添加", L"伺服器資料夾可稍後在設定中新增", L"Server folders can be added later in Settings"},  // IndexDirSub
    {L"其他", L"其他", L"Other"},  // SecOther
    {L"开机自动启动 Pulse", L"開機自動啟動 Pulse", L"Launch Pulse at sign-in"},  // OptStartup
    {L"登录 Windows 后在托盘中待命", L"登入 Windows 後在系統匣待命", L"Waits in the tray after you sign in"},  // OptStartupSub
    {L"创建桌面快捷方式", L"建立桌面捷徑", L"Create desktop shortcut"},  // OptDesktop
    {L"同时会添加到开始菜单", L"同時會新增到開始功能表", L"Also added to the Start menu"},  // OptDesktopSub
    {L"返回", L"返回", L"Back"},  // Back
    {L"取消", L"取消", L"Cancel"},  // Cancel
    {L"开", L"開", L"On"},  // On
    {L"关", L"關", L"Off"},  // Off
    {L"解压程序文件", L"解壓縮程式檔案", L"Extracting files"},  // StepExtract
    {L"设置文件关联与快捷方式", L"設定檔案關聯與捷徑", L"Setting associations & shortcuts"},  // StepShortcuts
    {L"注册 PulseIndex 服务", L"註冊 PulseIndex 服務", L"Registering PulseIndex service"},  // StepIndex
    {L"缓存右键菜单项", L"快取右鍵選單項目", L"Caching context-menu verbs"},  // StepVerbs
    {L"完成", L"完成", L"Finishing"},  // StepFinish
    {L"全盘搜索，快到不用等", L"全磁碟搜尋，快到不用等", L"Search that doesn't make you wait"},  // S1Title
    {L"基于 NTFS 主文件表的实时索引，输入即出结果，支持拼音与内容搜索。", L"基於 NTFS 主檔案表的即時索引，輸入即出結果，支援拼音與內容搜尋。", L"Live NTFS index with results as you type, including pinyin and content search."},  // S1Text
    {L"空格键快速预览", L"空白鍵快速預覽", L"Press Space to preview"},  // S2Title
    {L"图片、视频、PDF、Office 与代码文件，无需打开程序就能看清内容。", L"圖片、影片、PDF、Office 與程式碼檔案，無須開啟程式就能看清內容。", L"Images, video, PDF, Office and code — see what's inside without opening an app."},  // S2Text
    {L"双栏与标签页", L"雙欄與分頁", L"Dual panes & tabs"},  // S3Title
    {L"拖放、批量重命名、管理员授权操作，整理文件一气呵成。", L"拖放、批次重新命名、系統管理員授權操作，整理檔案一氣呵成。", L"Drag and drop, batch rename and elevated operations, all in one flow."},  // S3Text
    {L"季度报告", L"季度報告", L"quarterly report"},  // SearchDemo
    {L"Pulse 已准备就绪", L"Pulse 已準備就緒", L"Pulse is ready"},  // DoneTitle
    {L"安装完成。索引服务已在后台启动，首次建立索引通常只需几十秒。", L"安裝完成。索引服務已在背景啟動，首次建立索引通常只需幾十秒。", L"Setup is complete. The index service is running; the first index usually takes under a minute."},  // DoneSub
    {L"安装完成。随时可以在设置中启用全盘索引。", L"安裝完成。隨時可以在設定中啟用全磁碟索引。", L"Setup is complete. You can enable the full-disk index in Settings at any time."},  // DoneSubNoIndex
    {L"已升级到 %1", L"已升級到 %1", L"Upgraded to %1"},  // DoneTitleUp
    {L"你的设置、标签和索引都已保留。", L"你的設定、標籤和索引都已保留。", L"Your settings, tags and index were kept."},  // DoneSubUp
    {L"索引服务运行中", L"索引服務執行中", L"Index service running"},  // SumIndex
    {L"索引服务未能启动，可在设置中重试", L"索引服務未能啟動，可在設定中重試", L"Index service did not start; retry in Settings"},  // SumIndexFailed
    {L"右键菜单已注册", L"右鍵選單已註冊", L"Context menu registered"},  // SumMenu
    {L"开机启动", L"開機啟動", L"Launch at sign-in"},  // SumStart
    {L"关闭", L"關閉", L"Close"},  // Close
    {L"启动 Pulse", L"啟動 Pulse", L"Launch Pulse"},  // Launch
    {L"Pulse 正在复制文件", L"Pulse 正在複製檔案", L"Pulse is copying files"},  // BusyTitle
    {L"有一个复制或移动任务尚未完成，升级会中断它。请等任务结束后点“重试”，安装程序不会强行关闭 Pulse。", L"有一個複製或移動工作尚未完成，升級會中斷它。請等工作結束後按「重試」，安裝程式不會強制關閉 Pulse。", L"A copy or move task is still running and an upgrade would interrupt it. Wait for it to finish, then click Retry. Setup never force-closes Pulse."},  // BusyText
    {L"重试", L"重試", L"Retry"},  // Retry
    {L"要取消安装吗？", L"要取消安裝嗎？", L"Cancel setup?"},  // CancelTitle
    {L"已复制的文件会被清理，电脑不会留下任何改动。", L"已複製的檔案會被清除，電腦不會留下任何變更。", L"Copied files will be removed and nothing will be left on this PC."},  // CancelText
    {L"继续安装", L"繼續安裝", L"Keep installing"},  // KeepInstalling
    {L"取消安装", L"取消安裝", L"Cancel setup"},  // CancelYes
    {L"安装没有完成", L"安裝沒有完成", L"Setup did not finish"},  // ErrorTitle
    {L"原来的 Pulse 保持不变，可以稍后重试。", L"原來的 Pulse 保持不變，可以稍後重試。", L"Your existing Pulse was left unchanged. You can try again later."},  // ErrorRolledBack
    {L"打开安装日志", L"開啟安裝記錄", L"Open setup log"},  // OpenLog
    {L"请选择一个有效的本地文件夹。", L"請選擇一個有效的本機資料夾。", L"Please choose a valid local folder."},  // BadDir
    {L"磁盘空间不足。", L"磁碟空間不足。", L"Not enough disk space."},  // NoSpace
    {L"最小化", L"最小化", L"Minimize"},  // Minimize
    {L"Pulse 卸载程序", L"Pulse 解除安裝程式", L"Pulse Uninstall"},  // UnWindowTitle
    {L"卸载 Pulse", L"解除安裝 Pulse", L"Uninstall Pulse"},  // UnTitle
    {L"将停止并移除 PulseIndex 服务，然后删除程序文件。", L"將停止並移除 PulseIndex 服務，然後刪除程式檔案。", L"Stops and removes the PulseIndex service, then deletes the app files."},  // UnSub
    {L"同时删除设置、缓存和索引数据", L"同時刪除設定、快取和索引資料", L"Also remove settings, caches and index data"},  // UnClean
    {L"推荐。自定义索引目录只会删除 Pulse 创建的文件", L"建議。自訂索引目錄只會刪除 Pulse 建立的檔案", L"Recommended. Custom index folders only lose Pulse-owned files"},  // UnCleanSub
    {L"PulseIndex 服务", L"PulseIndex 服務", L"PulseIndex service"},  // UnService
    {L"已安装 · 卸载时会自动停止并移除", L"已安裝 · 解除安裝時會自動停止並移除", L"Installed · stopped and removed automatically"},  // UnServiceOn
    {L"未安装", L"未安裝", L"Not installed"},  // UnServiceNone
    {L"卸载", L"解除安裝", L"Uninstall"},  // UnBtn
    {L"已卸载。感谢使用 Pulse。", L"已解除安裝。感謝使用 Pulse。", L"Uninstalled. Thanks for using Pulse."},  // UnDone
    {L"已卸载。部分文件正在使用，将在重启电脑后删除。", L"已解除安裝。部分檔案正在使用中，將在重新啟動電腦後刪除。", L"Uninstalled. Some files are in use and will be removed after a restart."},  // UnDoneReboot
    {L"卸载未完成，Pulse 没有被移除。", L"解除安裝未完成，Pulse 沒有被移除。", L"Uninstall did not finish. Pulse was not removed."},  // UnFailed
    {L"有一个复制或移动任务尚未完成，卸载会中断它。请等任务结束后点“重试”，卸载程序不会强行关闭 Pulse。", L"有一個複製或移動工作尚未完成，解除安裝會中斷它。請等工作結束後按「重試」，解除安裝程式不會強制關閉 Pulse。", L"A copy or move task is still running and uninstalling would interrupt it. Wait for it to finish, then click Retry. Pulse is never force-closed."},  // UnBusyText
    {L"Pulse 打开命令已移除，但旧版本没有保留完整的原始关联，无法确认全部恢复。现有第三方设置和备份已保留。", L"Pulse 開啟命令已移除，但舊版沒有保留完整的原始關聯，無法確認全部還原。現有第三方設定與備份已保留。", L"Pulse open commands were removed, but old versions did not retain complete original associations. Other settings and backups were preserved."},  // IntegrationIncomplete
    {L"部分打开命令仍指向 Pulse，卸载后这些入口可能无法打开。请重新安装 Pulse 后在系统集成设置中恢复，或修复 Windows 文件关联。", L"部分開啟命令仍指向 Pulse，解除安裝後這些入口可能無法開啟。請重新安裝 Pulse 後在系統整合設定中還原，或修復 Windows 檔案關聯。", L"Some open commands still point to Pulse. After uninstall they may stop working. Reinstall Pulse to restore integration, or repair Windows file associations."},  // IntegrationFailed
};
static_assert(sizeof(kTable) / sizeof(kTable[0]) == static_cast<size_t>(Str::Count));

}  // namespace

Lang DetectLanguage(const wchar_t* inno_lang) {
    if (inno_lang && *inno_lang) {
        const std::wstring name = inno_lang;
        if (EqualsNoCase(name, L"chinesesimp")) return Lang::ZhHans;
        if (EqualsNoCase(name, L"chinesetrad")) return Lang::ZhHant;
        if (EqualsNoCase(name, L"english")) return Lang::En;
    }
    const LANGID id = GetUserDefaultUILanguage();
    if (PRIMARYLANGID(id) != LANG_CHINESE) return Lang::En;
    switch (SUBLANGID(id)) {
        case SUBLANG_CHINESE_TRADITIONAL: case SUBLANG_CHINESE_HONGKONG: case SUBLANG_CHINESE_MACAU:
            return Lang::ZhHant;
        default:
            return Lang::ZhHans;
    }
}

const wchar_t* Text(Lang lang, Str id) { return kTable[static_cast<int>(id)][static_cast<int>(lang)]; }

}  // namespace pulse::setup
