#include "status.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace Mux {
QString modeArgument(Mode mode) {
    switch (mode) {
    case Mode::Hybrid: return QStringLiteral("mshybrid");
    case Mode::Discrete: return QStringLiteral("discrete");
    case Mode::Integrated: return QStringLiteral("integrated");
    case Mode::Unknown: return {};
    }
    return {};
}

Mode parseMode(const QString &value) {
    if (value == QLatin1String("ms-hybrid")) return Mode::Hybrid;
    if (value == QLatin1String("discrete")) return Mode::Discrete;
    if (value == QLatin1String("integrated")) return Mode::Integrated;
    return Mode::Unknown;
}

bool Status::canSwitch(Mode mode, bool busy) const {
    if (!valid || !expectedHardware || !firmwareAvailable || !firmwareValid ||
        !newSwitchSupported || !switchingSupported || !acPower || pendingShutdown ||
        busy || bios != QLatin1String("E15M3IMS.116") || current == Mode::Unknown || target == Mode::Unknown ||
        target != current || mode == current)
        return false;
    if (mode == Mode::Hybrid) return true;
    if (mode == Mode::Discrete) return discreteSupported;
    if (mode == Mode::Integrated) return integratedSupported;
    return false;
}

bool Status::routinePending() const {
    return valid && firmwareAvailable && firmwareValid && current != Mode::Unknown &&
        target != Mode::Unknown && pendingShutdown && blockCode != QLatin1String("recovery_required");
}

Status Status::parse(const QByteArray &json) {
    Status result;
    if (json.size() > 1024 * 1024) { result.error = QStringLiteral("response_too_large"); return result; }
    QJsonParseError parseError;
    const auto doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        result.error = QStringLiteral("invalid_json"); return result;
    }
    const auto root = doc.object();
    if (root.value("schema_version").toInt(-1) != 1 || !root.value("machine").isObject() ||
        !root.value("firmware").isObject()) {
        result.error = QStringLiteral("unsupported_schema"); return result;
    }
    const auto machine = root.value("machine").toObject();
    result.model = machine.value("model").toString();
    result.board = machine.value("board").toString();
    result.bios = machine.value("bios").toString();
    result.expectedHardware = machine.value("expected_hardware").toBool(false);
    const auto firmware = root.value("firmware").toObject();
    result.firmwareAvailable = firmware.value("available").toBool(false);
    if (result.firmwareAvailable && firmware.value("value").isObject()) {
        const auto value = firmware.value("value").toObject();
        result.current = parseMode(value.value("current_mode").toString());
        result.target = parseMode(value.value("selected_target_mode").toString());
        const auto attributes = value.value("attributes");
        const bool validAttributes = attributes.isString() ? attributes.toString() == QLatin1String("0x00000007") :
            attributes.isDouble() && attributes.toDouble(-1) == 7;
        result.firmwareValid = value.value("length").toInt(-1) == 20 && validAttributes;
        result.newSwitchSupported = value.value("new_switch_supported").toBool(false);
        result.discreteSupported = value.value("discrete_supported").toBool(false);
        result.integratedSupported = value.value("integrated_supported").toBool(false);
    }
    result.acPower = root.value("ac_power_online").toBool(false);
    result.switchingSupported = root.value("switching_supported").toBool(false);
    result.blockReason = root.value("switching_block_reason").toString();
    result.blockCode = root.value("switching_block_code").toString();
    // An explicit false may indicate recovery is needed and shutdown is unsafe
    // as a next-step recommendation. Only old snapshots use target mismatch.
    result.pendingShutdown = root.value("pending_shutdown").isBool() ? root.value("pending_shutdown").toBool() :
        (result.current != Mode::Unknown && result.target != Mode::Unknown && result.current != result.target);
    const auto displays = root.value("internal_displays").toArray();
    for (const auto &entry : displays) {
        if (!entry.isObject()) continue;
        const auto item = entry.toObject();
        InternalDisplay display;
        display.connector = item.value("connector").toString();
        display.vendor = item.value("vendor").toString();
        display.driver = item.value("driver").toString();
        display.connected = item.value("status").toString() == QLatin1String("connected");
        display.enabled = item.value("enabled").isBool() ? item.value("enabled").toBool() :
            item.value("enabled").toString() == QLatin1String("enabled");
        result.displays.append(display);
    }
    result.valid = true;
    return result;
}

Status Status::demo() {
    return parse(R"({"schema_version":1,"machine":{"model":"Vector 16 HX AI A2XWIG","board":"MS-15M3","bios":"E15M3IMS.116","expected_hardware":true},"firmware":{"available":true,"value":{"length":20,"attributes":"0x00000007","current_mode":"ms-hybrid","selected_target_mode":"ms-hybrid","new_switch_supported":true,"discrete_supported":true,"integrated_supported":true}},"ac_power_online":true,"switching_supported":true,"pending_shutdown":false,"internal_displays":[{"connector":"eDP-1","status":"connected","enabled":true,"vendor":"Intel","driver":"i915"}]})");
}
}
