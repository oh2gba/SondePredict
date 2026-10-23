#pragma once

#include "windfield.h"

#include <QString>

#include <functional>
#include <memory>
#include <vector>

namespace sp {

// How a fetch ended.
struct FetchResult {
    QString error;             // empty = success
    bool limitReached = false; // the source's hourly or daily quota is used up: stop asking
};

// Something that can load wind data into a WindField (Open-Meteo, a test fake, later
// possibly a GRIB archive).
class WindSource {
public:
    using Done = std::function<void(const FetchResult &result)>;

    virtual ~WindSource() = default;

    // Loads `nodes` for every hour in [firstHour, lastHour] (unix hours) into `field` and
    // then calls `done`, possibly asynchronously. The source shares ownership of `field`
    // until it is done, so a cancelled caller may drop its own reference at any time.
    virtual void fetch(const std::vector<NodeIndex> &nodes, qint64 firstHour, qint64 lastHour,
                       std::shared_ptr<WindField> field, Done done) = 0;

    // Whether fetching more hours for the same nodes costs (almost) nothing, so a batch may
    // load the hours of later launches in advance (true for Open-Meteo, which charges per
    // location; not for MEPS, where each hour is a separate request).
    virtual bool extraHoursAreFree() const { return false; }

    // Receives a line for each notable step (requests, waits, retries) for an activity log.
    void setLogger(std::function<void(const QString &line)> logger) { m_logger = std::move(logger); }

protected:
    void log(const QString &line) const
    {
        if (m_logger)
            m_logger(line);
    }

private:
    std::function<void(const QString &)> m_logger;
};

} // namespace sp
