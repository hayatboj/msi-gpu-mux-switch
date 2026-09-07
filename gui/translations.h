#pragma once

#include "status.h"
#include <QString>

namespace Mux {
enum class Language { English, Turkish };
enum class Text {
    Subtitle, Demo, CurrentMode, Unknown, Hybrid, Discrete, Integrated,
    HybridDescription, DiscreteDescription, IntegratedDescription, SelectMode,
    Active, Select, InternalPanel, Firmware, Hardware, AcConnected, Battery,
    Compatible, Unsupported, PendingTitle,
    PendingDetail, Shutdown, Refresh, Refreshing, Open, Quit, LanguageMenu,
    Autostart, Settings, Ready, Reading, BackendMissing, StatusFailed,
    FirmwareUnavailable, UnsupportedHardware, UnsupportedBios, UnknownMode,
    AcRequired, PendingBlock, Busy, ApplyTitle, ApplyDescription,
    ApplyRisk, ConfirmApply, Cancel, Applying, ApplySuccess,
    ApplyFailed, ApplyUncertain, Details, Close, ShutdownTitle, ShutdownConfirm,
    ShutdownFailed, DemoApply, Error, AutostartFailed, AlreadyRunning,
    Version, NoPanel, Disabled, PermissionDenied, InvalidResponse,
    NoChanges, NotificationsUnavailable, ShowAfterStart, RecoveryRequired, AcpiUnavailable,
    About, WhatsNew, ReducedMotion, CurrentToTarget, HeroPendingDetail, PowerCyclePending,
    LiveMode, PowerOptions, PowerSuccessTitle, PowerSuccessDetail, Restart,
    Later, PowerRequestSent, PowerRequestCancelled, PowerRequestDenied, PowerRequestUnavailable,
    PowerRequestFailed, PowerRequestDemo, Target, GnomeSetup, GnomeSetupDetail,
    DraftTitle, DraftDetail, DraftSelected, ClearDraft, DraftPowerTitle, DraftPowerDetail,
    DraftSaveFailed, CurrentToSelection, HeroDraftDetail, SelectionSaved, Count
};
QString tr(Text key, Language language);
QString modeName(Mode mode, Language language);
QString blockText(const Status &status, Language language);
Language systemLanguage();
}
