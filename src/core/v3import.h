#pragma once

#include "settings.h"

#include <QString>
#include <QStringList>

#include <map>
#include <vector>

namespace sp {

// Minimal reader for the ConfigObj files Sonde Predict v3 wrote (config.ini):
// [section] / [[subsection]], key = value, quoted values, comma lists, # comments.
namespace configobj {

struct Section {
    std::vector<std::pair<QString, QString>> values; // in file order
    std::vector<std::pair<QString, Section>> sections;

    QString value(const QString &key, const QString &fallback = {}) const;
    bool has(const QString &key) const;
    const Section *section(const QString &name) const;
};

// Returns the root section; `error` is set (and parsing stops) on malformed input.
Section parse(const QString &text, QString *error = nullptr);

// Splits a ConfigObj list value ("0, 1, 2" or "11," for one item); quotes are removed.
QStringList toList(const QString &value);

} // namespace configobj

struct V3ImportResult {
    QString error;        // set when nothing could be imported
    QStringList messages; // what was imported, and anything that needs the user's attention
    int locationsAdded = 0;
    int locationsSkipped = 0;
    bool scheduleImported = false;
};

// Merges a v3 config.ini into `settings`. Locations whose name already exists are kept as
// they are. The v3 descent rate and gain are kept as the Legacy descent model; they become
// the active model only if they differ from v3's defaults (i.e. were tuned by the user).
V3ImportResult importV3Config(const QString &path, AppSettings &settings);

} // namespace sp
