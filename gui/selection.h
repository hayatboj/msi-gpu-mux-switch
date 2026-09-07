#pragma once

#include "status.h"
#include <optional>
#include <utility>

namespace Mux {
// A per-user UI preference, never authorization for the privileged backend.
struct Selection {
    Mode mode = Mode::Unknown;
    Mode current = Mode::Unknown;
    Mode target = Mode::Unknown;
    QString model;
    QString board;
    QString bios;
    QString bootId;

    static Selection bind(Mode mode, const Status &status, const QString &bootId);
    bool valid() const;
    bool matchesBaseline(const Status &status, const QString &bootId) const;
    bool matchesApplied(const Status &status, const QString &bootId) const;
};

class SelectionStore {
public:
    explicit SelectionStore(QString path) : m_path(std::move(path)) {}
    static QString defaultPath();
    bool load(std::optional<Selection> &selection, QString &error) const;
    bool save(const Selection &selection, QString &error) const;
    bool clear(QString &error) const;
private:
    QString m_path;
};
}
