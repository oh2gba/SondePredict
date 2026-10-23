#include "v3import.h"

#include <QFile>

#include <algorithm>

namespace sp {

namespace configobj {

namespace {

QString unquote(QString s)
{
    s = s.trimmed();
    if (s.size() >= 2 && (s.front() == u'"' || s.front() == u'\'') && s.back() == s.front())
        return s.mid(1, s.size() - 2);
    return s;
}

// Drops a trailing "# comment" that is not inside quotes.
QString stripComment(const QString &s)
{
    QChar quote;
    for (qsizetype i = 0; i < s.size(); ++i) {
        const QChar c = s[i];
        if (!quote.isNull()) {
            if (c == quote)
                quote = QChar();
        } else if (c == u'"' || c == u'\'') {
            quote = c;
        } else if (c == u'#') {
            return s.left(i);
        }
    }
    return s;
}

// True if the value is exactly one quoted string (as opposed to a list of them).
bool isSingleQuoted(const QString &s)
{
    if (s.size() < 2 || (s.front() != u'"' && s.front() != u'\''))
        return false;
    return s.indexOf(s.front(), 1) == s.size() - 1;
}

} // namespace

QString Section::value(const QString &key, const QString &fallback) const
{
    for (const auto &[k, v] : values) {
        if (k == key)
            return v;
    }
    return fallback;
}

bool Section::has(const QString &key) const
{
    for (const auto &kv : values) {
        if (kv.first == key)
            return true;
    }
    return false;
}

const Section *Section::section(const QString &name) const
{
    for (const auto &[n, s] : sections) {
        if (n == name)
            return &s;
    }
    return nullptr;
}

Section parse(const QString &text, QString *error)
{
    Section root;
    std::vector<Section *> stack{&root};
    const QStringList lines = text.split(u'\n');

    for (qsizetype n = 0; n < lines.size(); ++n) {
        const QString line = stripComment(lines[n]).trimmed();
        if (line.isEmpty())
            continue;

        if (line.startsWith(u'[')) {
            qsizetype depth = 0;
            while (depth < line.size() && line[depth] == u'[')
                ++depth;
            qsizetype closing = 0;
            while (closing < line.size() && line[line.size() - 1 - closing] == u']')
                ++closing;
            if (closing != depth || line.size() <= 2 * depth || static_cast<std::size_t>(depth) > stack.size()) {
                if (error)
                    *error = QStringLiteral("line %1: bad section header %2").arg(n + 1).arg(line);
                return root;
            }
            stack.resize(static_cast<std::size_t>(depth));
            Section *parent = stack.back();
            parent->sections.emplace_back(unquote(line.mid(depth, line.size() - 2 * depth)), Section{});
            stack.push_back(&parent->sections.back().second);
            continue;
        }

        const qsizetype eq = line.indexOf(u'=');
        if (eq <= 0) {
            if (error)
                *error = QStringLiteral("line %1: expected key = value").arg(n + 1);
            return root;
        }
        const QString key = unquote(line.left(eq));
        const QString raw = line.mid(eq + 1).trimmed();
        stack.back()->values.emplace_back(key, isSingleQuoted(raw) ? unquote(raw) : raw);
    }
    return root;
}

QStringList toList(const QString &value)
{
    QStringList items;
    QString current;
    QChar quote;
    for (const QChar c : value) {
        if (!quote.isNull()) {
            if (c == quote)
                quote = QChar();
            current += c;
        } else if (c == u'"' || c == u'\'') {
            quote = c;
            current += c;
        } else if (c == u',') {
            items << unquote(current);
            current.clear();
        } else {
            current += c;
        }
    }
    if (!current.trimmed().isEmpty())
        items << unquote(current);
    items.removeAll(QString());
    return items;
}

} // namespace configobj

namespace {

bool toBool(const QString &s, bool fallback)
{
    const QString v = s.trimmed().toLower();
    if (v == QLatin1String("true") || v == QLatin1String("yes") || v == QLatin1String("on") || v == QLatin1String("1"))
        return true;
    if (v == QLatin1String("false") || v == QLatin1String("no") || v == QLatin1String("off") || v == QLatin1String("0"))
        return false;
    return fallback;
}

std::optional<double> toNumber(const QString &s)
{
    bool ok;
    const double v = s.trimmed().toDouble(&ok);
    return ok ? std::optional<double>(v) : std::nullopt;
}

std::optional<Location> makeLocation(const QString &name, const QString &lat, const QString &lon, const QString &alt)
{
    const auto la = toNumber(lat), lo = toNumber(lon);
    if (name.trimmed().isEmpty() || !la || !lo || *la < -90 || *la > 90 || *lo < -180 || *lo > 180)
        return std::nullopt;
    Location l;
    l.name = name.trimmed();
    l.position = {*la, *lo};
    l.altitude = toNumber(alt).value_or(0.0);
    return l;
}

} // namespace

V3ImportResult importV3Config(const QString &path, AppSettings &settings)
{
    V3ImportResult result;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        result.error = QStringLiteral("cannot open %1: %2").arg(path, file.errorString());
        return result;
    }
    QString parseError;
    const configobj::Section root = configobj::parse(QString::fromUtf8(file.readAll()), &parseError);
    if (!parseError.isEmpty()) {
        result.error = QStringLiteral("%1 is not a Sonde Predict v3 config file (%2)").arg(path, parseError);
        return result;
    }
    if (!root.section(QStringLiteral("track_settings")) && !root.section(QStringLiteral("locations"))) {
        result.error = QStringLiteral("%1 has no [track_settings] or [locations]; is it a Sonde Predict v3 config.ini?").arg(path);
        return result;
    }

    const auto addLocation = [&](const Location &l) {
        if (settings.location(l.name)) {
            ++result.locationsSkipped;
            result.messages << QStringLiteral("Location \"%1\" already exists; kept the existing one.").arg(l.name);
            return;
        }
        settings.locations << l;
        ++result.locationsAdded;
    };

    // Locations, in file order (v3 listed them in its drop-downs in this order).
    QStringList v3Order;
    if (const auto *locations = root.section(QStringLiteral("locations"))) {
        for (const auto &[key, s] : locations->sections) {
            auto l = makeLocation(s.value(QStringLiteral("name")), s.value(QStringLiteral("lat")),
                                  s.value(QStringLiteral("lon")), s.value(QStringLiteral("alt")));
            if (!l) {
                result.messages << QStringLiteral("Skipped location [[%1]]: missing name or invalid coordinates.").arg(key);
                continue;
            }
            l->warningDistanceKm = toNumber(s.value(QStringLiteral("warning"))).value_or(l->warningDistanceKm);
            v3Order << l->name;
            addLocation(*l);
        }
    }

    if (const auto *track = root.section(QStringLiteral("track_settings"))) {
        FlightProfile &p = settings.profile;
        p.ascentRate = toNumber(track->value(QStringLiteral("balloon_asc_rate"))).value_or(p.ascentRate);
        p.burstAltitude = toNumber(track->value(QStringLiteral("balloon_burst_alt"))).value_or(p.burstAltitude);

        const auto rate = toNumber(track->value(QStringLiteral("balloon_desc_rate")));
        const auto gain = toNumber(track->value(QStringLiteral("balloon_desc_gain")));
        p.legacyDescentRate = rate.value_or(40.0);
        p.legacyDescentGain = gain.value_or(1.0);
        if ((rate && *rate != 40.0) || (gain && *gain != 1.0)) {
            p.descentModel = DescentModel::Legacy;
            result.messages << QStringLiteral("Your v3 descent settings (rate %1 m/s, gain %2) are active as the Legacy descent model.")
                                   .arg(p.legacyDescentRate).arg(p.legacyDescentGain);
        } else {
            result.messages << QStringLiteral("v3 used its default descent settings; the new density-based descent model stays active.");
        }

        const QString grid = track->value(QStringLiteral("grid_size"));
        if (grid == QLatin1String("0p25") || grid == QLatin1String("0p50") || grid == QLatin1String("1p00"))
            settings.wind.gridStep = QString(grid).replace(u'p', u'.').toDouble();
    }

    if (const auto *network = root.section(QStringLiteral("network_settings"))) {
        if (toBool(network->value(QStringLiteral("proxy_enabled")), false)) {
            settings.network.proxyMode = ProxyMode::Manual;
            settings.network.proxyUrl = network->value(QStringLiteral("proxy_address"));
            result.messages << QStringLiteral("Proxy %1 imported.").arg(settings.network.proxyUrl);
        }
    }

    if (const auto *general = root.section(QStringLiteral("general"))) {
        settings.general.overwritePredictions = toBool(general->value(QStringLiteral("overwrite_predictions")), settings.general.overwritePredictions);
        settings.general.useUtc = toBool(general->value(QStringLiteral("use_utc")), settings.general.useUtc);
    }

    if (const auto *schedule = root.section(QStringLiteral("schedule"))) {
        // The schedule carries its own copy of both locations; add them if missing.
        for (const QString &prefix : {QStringLiteral("site"), QStringLiteral("ur")}) {
            const auto l = makeLocation(schedule->value(prefix + QStringLiteral("Loc")), schedule->value(prefix + QStringLiteral("Lat")),
                                        schedule->value(prefix + QStringLiteral("Lon")), schedule->value(prefix + QStringLiteral("Alt")));
            if (l && !settings.location(l->name))
                addLocation(*l);
        }

        ScheduleRule rule;
        BatchSite site;
        site.siteName = schedule->value(QStringLiteral("siteLoc")).trimmed();
        rule.homeName = schedule->value(QStringLiteral("urLoc")).trimmed();
        for (const QString &d : configobj::toList(schedule->value(QStringLiteral("days")))) {
            if (const auto v = toNumber(d))
                rule.days << static_cast<int>(*v);
        }
        // v3 launched on the hour; FMI launches about 30 min before the synoptic hour.
        for (const QString &h : configobj::toList(schedule->value(QStringLiteral("hours")))) {
            if (const auto v = toNumber(h); v && *v >= 0 && *v < 24)
                site.times << QTime(static_cast<int>(*v), 30);
        }
        rule.intervalHours = std::clamp(static_cast<int>(toNumber(schedule->value(QStringLiteral("interval"))).value_or(0)), 0, 168);
        rule.sites << site;

        if (!site.siteName.isEmpty() && !rule.days.isEmpty() && !site.times.isEmpty()) {
            settings.schedule = rule;
            result.scheduleImported = true;
            result.messages << QStringLiteral("Batch schedule imported: %1 → %2, %3 day(s) × %4 launch time(s).")
                                   .arg(site.siteName, rule.homeName).arg(rule.days.size()).arg(site.times.size());
        }
    }

    if (const auto *manual = root.section(QStringLiteral("manual_run"))) {
        const auto pick = [&](const QString &key) -> QString {
            const auto index = toNumber(manual->value(key));
            if (!index || *index < 0 || *index >= v3Order.size())
                return {};
            return v3Order[static_cast<qsizetype>(*index)];
        };
        if (const QString site = pick(QStringLiteral("launch_loc")); !site.isEmpty())
            settings.manualRun.siteName = site;
        if (const QString home = pick(QStringLiteral("your_loc")); !home.isEmpty())
            settings.manualRun.homeName = home;
        if (const auto hour = toNumber(manual->value(QStringLiteral("time"))); hour && *hour >= 0 && *hour < 24) {
            settings.manualRun.hour = static_cast<int>(*hour);
            settings.manualRun.minute = 0; // v3 only had whole hours
        }
    }

    result.messages.prepend(QStringLiteral("Imported %1 location(s) from %2.").arg(result.locationsAdded).arg(path));
    return result;
}

} // namespace sp
