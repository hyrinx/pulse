#include "ops_manager.h"
#include "elevated_delete.h"
#include "elevated_transfer_client.h"
#include "transfer_rate_estimator.h"
#include "../common/localization.h"
#include "../common/runtime_log.h"
#include <sherrors.h>

namespace pulse::ops {
namespace {
bool PermissionFailure(HRESULT hr) {
    return hr == E_ACCESSDENIED || hr == HRESULT_FROM_WIN32(ERROR_PRIVILEGE_NOT_HELD) ||
        hr == HRESULT_FROM_WIN32(ERROR_ELEVATION_REQUIRED) ||
        hr == static_cast<HRESULT>(0x80270021L) || hr == static_cast<HRESULT>(0x80270022L);
}
// Localized text for the failures users actually hit; the elevated helper
// has no localization and FormatMessage knows nothing about COPYENGINE_E_*.
std::wstring KnownDeleteError(HRESULT hr) {
    switch (static_cast<uint32_t>(hr)) {
    case 0x80070005u:  // E_ACCESSDENIED
    case 0x80270021u:  // COPYENGINE_E_ACCESS_DENIED_SRC
    case 0x80270022u:  // COPYENGINE_E_ACCESS_DENIED_DEST
        return l10n::Pick(L"访问被拒绝。该项目可能受系统保护、权限设置不允许删除，或正被其他程序使用。",
                          L"Access denied. The item may be protected, its permissions may not allow deletion, or another program may be using it.");
    case 0x80070020u:  // ERROR_SHARING_VIOLATION
    case 0x80070021u:  // ERROR_LOCK_VIOLATION
    case 0x80270027u:  // COPYENGINE_E_SHARING_VIOLATION_SRC
    case 0x80270028u:  // COPYENGINE_E_SHARING_VIOLATION_DEST
        return l10n::Pick(L"该项目正被其他程序使用。关闭相关程序后重试。",
                          L"The item is in use by another program. Close it and try again.");
    case static_cast<uint32_t>(COPYENGINE_E_RECYCLE_BIN_NOT_FOUND):
        return l10n::Pick(L"此位置无法安全地移到回收站，项目未被删除。",
                          L"This location cannot be safely sent to the Recycle Bin. The item was not deleted.");
    default:
        return {};
    }
}
std::wstring DeleteError(const ShellTransferResult& result) {
    if (auto known = KnownDeleteError(result.hr); !known.empty()) return known;
    if (!result.error.empty()) return result.error;
    if (result.hr == HRESULT_FROM_WIN32(ERROR_CANCELLED))
        return l10n::Pick(L"未获得管理员权限。可以重试授权，或跳过当前项。",
                         L"Administrator permission was not granted. Retry or skip this item.");
    wchar_t* text = nullptr;
    std::wstring error;
    if (FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, static_cast<DWORD>(result.hr), 0,
            reinterpret_cast<LPWSTR>(&text), 0, nullptr) && text) error = text;
    if (text) LocalFree(text);
    if (!error.empty()) return error;
    wchar_t code[16];
    swprintf_s(code, L"0x%08X", static_cast<unsigned>(result.hr));
    return std::wstring(l10n::Pick(L"无法删除当前项（错误 ", L"Could not delete this item (error ")) + code +
           l10n::Pick(L"）。", L").");
}
}

void OpsManager::RunAuthorizedDelete(const OpRequest& req, uint64_t task_id) {
    transfer_cancel_.store(shell_cancel_requested_.load());
    transfer_pause_.store(false);
    transfer_active_.store(true);
    SetStatus([](OpStatus& status) { status.can_pause = false; });
    const bool permanent = req.type == OpType::RealDelete;
    std::vector<std::wstring> completed;
    size_t skipped = 0;
    HRESULT failure = S_OK;
    std::wstring error;
    bool defer_authorization = false;
    std::wstring authorization_error;
    const auto started = GetTickCount64();
    TransferRateEstimator rate;
    rate.SetEtaFloor(0.0);
    rate.Reset(started, 0);
    const auto update_rate = [&](OpStatus& status) {
        // Only confirmed source deletions count, including on the elevated path.
        // Skipped sources reduce remaining work without inflating the delete rate.
        rate.Observe(GetTickCount64(), completed.size(), req.sources.size() - skipped);
        status.items_per_second = rate.speed();
        status.peak_items_per_second = (std::max)(status.peak_items_per_second,
                                                status.items_per_second);
        status.eta_seconds = rate.eta_seconds();
    };
    for (const auto& source : req.sources) {
        if (transfer_cancel_.load()) break;
        SetStatus([&](OpStatus& status) {
            status.current_item = source;
            status.last_error.clear();
            status.phase = OpPhase::Running;
        });
        // After "Retry", stay on the authorization view until the attempt
        // actually deletes something or ends; the helper's "ready" signal and
        // its initial 0% progress otherwise flash the progress/estimate view
        // just before the same error brings the authorization view back.
        bool retrying = false;
        ElevatedTransferCallbacks callbacks;
        callbacks.authorization = [&](bool awaiting) {
            if (retrying && !awaiting) return;
            SetStatus([&](OpStatus& status) {
                status.authorization = awaiting ? AuthorizationState::Requesting : AuthorizationState::None;
                status.authorization_retrying = false;
                status.summary = awaiting
                    ? l10n::Pick(L"正在请求管理员授权…", L"Requesting administrator permission…")
                    : l10n::Pick(L"正在删除…", L"Deleting…");
            });
        };
        auto progress = [&](const std::wstring& item, float percent) {
            if (retrying) {
                if (percent <= 0) return;
                retrying = false;
            }
            SetStatus([&](OpStatus& status) {
                status.authorization = AuthorizationState::None;
                status.authorization_retrying = false;
                status.current_item = item.empty() ? source : item;
                status.completed_items = completed.size();
                status.percent = req.sources.empty() ? 0.0f :
                    static_cast<float>((completed.size() + skipped + (percent < 0 ? 0 : percent / 100.0)) *
                                       100.0 / req.sources.size());
                update_rate(status);
            });
        };
        callbacks.progress = [&](const OpStatus& status) { progress(status.current_item, status.percent); };
        bool authorized = NeedsShellTransfer({source}, L"", true);
        ShellTransferResult result;
        for (;;) {
            if (authorized && defer_authorization) {
                const auto choice = WaitForAuthorization(task_id, authorization_error, source);
                if (choice == AuthorizationChoice::Cancel) break;
                if (choice == AuthorizationChoice::Skip) { ++skipped; result = ShellTransferResult{}; break; }
                defer_authorization = false;
            }
            result = authorized
                ? DeleteWithElevatedHelper({source}, permanent, UiWindow(), transfer_cancel_, callbacks)
                : DeleteWithCurrentToken({source}, permanent, UiWindow(), transfer_cancel_, progress);
            if (!authorized && !result.mutated && PermissionFailure(result.hr) && !transfer_cancel_.load()) {
                authorized = true;
                continue;
            }
            if (SUCCEEDED(result.hr) || result.mutated || transfer_cancel_.load() || !authorized) break;
            authorization_error = DeleteError(result);
            const auto choice = WaitForAuthorization(task_id, authorization_error, source);
            if (choice == AuthorizationChoice::Retry) { retrying = true; continue; }
            if (choice == AuthorizationChoice::Skip) {
                ++skipped;
                defer_authorization = true;
                result = ShellTransferResult{};
            }
            break;
        }
        completed.insert(completed.end(), result.sources.begin(), result.sources.end());
        SetStatus([&](OpStatus& status) {
            // A retried attempt on a reused elevated session never invokes the
            // authorization callback, so leave the Requesting view explicitly.
            status.authorization = AuthorizationState::None;
            status.authorization_retrying = false;
            status.completed_items = completed.size();
            update_rate(status);
        });
        if (result.mutated && (FAILED(result.hr) || result.cancelled)) {
        CompletedOperation refresh;
        refresh.task_id = task_id;
            refresh.type = req.type;
            refresh.sources = {source};
            refresh.refresh_only = true;
            std::lock_guard lock(mutex_);
            completions_.push_back(std::move(refresh));
        }
        if (result.cancelled && !PermissionFailure(result.hr)) transfer_cancel_.store(true);
        if (FAILED(result.hr)) { failure = result.hr; error = DeleteError(result); break; }
    }
    if (!completed.empty()) {
        OpRequest finished = req;
        finished.sources = completed;
        PushUndo(finished, task_id);
    }
    const bool cancelled = transfer_cancel_.load();
    const auto lock_report = FAILED(failure) && !cancelled ? ProbeLock(req, task_id, failure, error) : LockReport{};
    diagnostics::runtime::Event("file_delete_result", {{"task", task_id}, {"permanent", permanent},
        {"hresult", static_cast<uint32_t>(failure)}, {"cancelled", cancelled},
        {"completed", completed.size()}, {"skipped", skipped}, {"elapsed_ms", GetTickCount64() - started}});
    SetStatus([&](OpStatus& status) {
        status.active = false;
        status.authorization = AuthorizationState::None;
        status.can_skip_authorization = false;
        status.can_pause = false;
        ++status.completed_ops;
        status.completed_items = completed.size();
        status.bytes_per_second = 0;
        status.items_per_second = 0;
        status.eta_seconds = 0;
        status.locked_path = lock_report.path;
        status.lock_owners = lock_report.owners;
        if (cancelled || FAILED(failure)) {
            status.phase = OpPhase::Failed;
            status.last_error = cancelled ? l10n::Pick(L"已取消", L"Canceled") : error;
            status.summary = status.last_error;
        } else {
            status.phase = OpPhase::Completed;
            status.percent = 100;
            status.last_error.clear();
            status.summary = skipped ? l10n::Pick(L"删除结束，已跳过 ", L"Deletion finished; skipped ") + std::to_wstring(skipped)
                                     : l10n::Pick(L"删除完成", L"Deletion completed");
        }
    });
    transfer_active_.store(false);
}
}
