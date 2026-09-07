#include "selection.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <unistd.h>

namespace Mux {
namespace {
constexpr qint64 maximumSize = 4096;
bool knownMode(Mode mode) {
    return mode == Mode::Hybrid || mode == Mode::Discrete || mode == Mode::Integrated;
}
bool knownIdentity(const Status &status) {
    return status.valid && status.expectedHardware && status.firmwareAvailable && status.firmwareValid &&
        status.model == QLatin1String("Vector 16 HX AI A2XWIG") &&
        status.board == QLatin1String("MS-15M3") && status.bios == QLatin1String("E15M3IMS.116");
}
bool safeFile(const QString &path, QString &error) {
    const QFileInfo info(path);
    if (info.isSymLink() || (info.exists() && (!info.isFile() || info.ownerId() != getuid()))) {
        error = QStringLiteral("draft_unsafe_file");
        return false;
    }
    return true;
}
}

Selection Selection::bind(Mode mode, const Status &status, const QString &bootId) {
    return {mode, status.current, status.target, status.model, status.board, status.bios, bootId};
}

bool Selection::valid() const {
    static const QRegularExpression bootPattern(QStringLiteral("^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$"));
    return knownMode(mode) && knownMode(current) && current == target && mode != current &&
        model == QLatin1String("Vector 16 HX AI A2XWIG") && board == QLatin1String("MS-15M3") &&
        bios == QLatin1String("E15M3IMS.116") && bootPattern.match(bootId).hasMatch();
}

bool Selection::matchesBaseline(const Status &status, const QString &boot) const {
    return valid() && knownIdentity(status) && bootId == boot && model == status.model && board == status.board &&
        bios == status.bios && current == status.current && target == status.target &&
        !status.pendingShutdown && status.blockCode != QLatin1String("recovery_required") && status.newSwitchSupported &&
        (mode != Mode::Discrete || status.discreteSupported) && (mode != Mode::Integrated || status.integratedSupported);
}

bool Selection::matchesApplied(const Status &status, const QString &boot) const {
    return valid() && knownIdentity(status) && bootId == boot && model == status.model && board == status.board &&
        bios == status.bios && current == status.current && status.target == mode && status.routinePending() &&
        status.blockCode == QLatin1String("pending_shutdown") && status.newSwitchSupported &&
        (mode != Mode::Discrete || status.discreteSupported) && (mode != Mode::Integrated || status.integratedSupported);
}

QString SelectionStore::defaultPath() {
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/selection.json");
}

bool SelectionStore::load(std::optional<Selection> &selection, QString &error) const {
    selection.reset();
    error.clear();
    if (!safeFile(m_path, error)) return false;
    if (!QFileInfo::exists(m_path)) return true;
    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly)) { error = QStringLiteral("draft_read_failed"); return false; }
    const auto bytes = file.read(maximumSize + 1);
    if (bytes.size() > maximumSize) { error = QStringLiteral("draft_too_large"); return false; }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        error = QStringLiteral("draft_invalid_json"); return false;
    }
    const auto object = document.object();
    const QStringList expected{QStringLiteral("schema_version"), QStringLiteral("mode"), QStringLiteral("current"),
        QStringLiteral("target"), QStringLiteral("model"), QStringLiteral("board"), QStringLiteral("bios"), QStringLiteral("boot_id")};
    if (object.size() != expected.size() || !object.value("schema_version").isDouble() ||
        object.value("schema_version").toDouble() != 1) {
        error = QStringLiteral("draft_invalid_schema"); return false;
    }
    for (const auto &key : expected) {
        if (key != QLatin1String("schema_version") && !object.value(key).isString()) {
            error = QStringLiteral("draft_invalid_schema"); return false;
        }
    }
    Selection value{parseMode(object.value("mode").toString()), parseMode(object.value("current").toString()),
        parseMode(object.value("target").toString()), object.value("model").toString(), object.value("board").toString(),
        object.value("bios").toString(), object.value("boot_id").toString()};
    if (!value.valid()) { error = QStringLiteral("draft_invalid_binding"); return false; }
    selection = value;
    return true;
}

bool SelectionStore::save(const Selection &selection, QString &error) const {
    error.clear();
    if (!selection.valid()) { error = QStringLiteral("draft_invalid_binding"); return false; }
    if (!safeFile(m_path, error)) return false;
    const auto parent = QFileInfo(m_path).absolutePath();
    if (!QDir().mkpath(parent)) { error = QStringLiteral("draft_directory_failed"); return false; }
    const QJsonObject object{{"schema_version", 1}, {"mode", selection.mode == Mode::Hybrid ? QStringLiteral("ms-hybrid") : modeArgument(selection.mode)},
        {"current", selection.current == Mode::Hybrid ? QStringLiteral("ms-hybrid") : modeArgument(selection.current)},
        {"target", selection.target == Mode::Hybrid ? QStringLiteral("ms-hybrid") : modeArgument(selection.target)},
        {"model", selection.model}, {"board", selection.board}, {"bios", selection.bios}, {"boot_id", selection.bootId}};
    const auto bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
    QSaveFile file(m_path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || !file.setPermissions(QFile::ReadOwner | QFile::WriteOwner) ||
        file.write(bytes) != bytes.size() || !file.commit()) {
        error = QStringLiteral("draft_write_failed"); return false;
    }
    return true;
}

bool SelectionStore::clear(QString &error) const {
    error.clear();
    if (!safeFile(m_path, error)) return false;
    if (QFileInfo::exists(m_path) && !QFile::remove(m_path)) {
        error = QStringLiteral("draft_remove_failed"); return false;
    }
    return true;
}
}
