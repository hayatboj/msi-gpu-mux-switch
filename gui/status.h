#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

namespace Mux {
enum class Mode { Unknown, Hybrid, Discrete, Integrated };
QString modeArgument(Mode mode);
Mode parseMode(const QString &value);

struct InternalDisplay {
    QString connector;
    QString vendor;
    QString driver;
    bool connected = false;
    bool enabled = false;
};

struct Status {
    bool valid = false;
    QString error;
    QString model;
    QString board;
    QString bios;
    bool expectedHardware = false;
    bool firmwareAvailable = false;
    bool firmwareValid = false;
    Mode current = Mode::Unknown;
    Mode target = Mode::Unknown;
    bool discreteSupported = false;
    bool integratedSupported = false;
    bool newSwitchSupported = false;
    bool acPower = false;
    bool switchingSupported = false;
    bool pendingShutdown = false;
    QString blockReason;
    QString blockCode;
    QVector<InternalDisplay> displays;

    bool canSwitch(Mode mode, bool busy = false) const;
    bool routinePending() const;
    static Status parse(const QByteArray &json);
    static Status demo();
};
}
