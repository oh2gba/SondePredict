#pragma once

#include "windfield.h"

#include <QString>

#include <functional>
#include <memory>
#include <vector>

namespace sp {

// Something that can load wind data into a WindField (Open-Meteo, a test fake, later
// possibly a GRIB archive).
class WindSource {
public:
    using Done = std::function<void(const QString &error)>; // empty error = success

    virtual ~WindSource() = default;

    // Loads `nodes` for every hour in [firstHour, lastHour] (unix hours) into `field` and
    // then calls `done`, possibly asynchronously. The source shares ownership of `field`
    // until it is done, so a cancelled caller may drop its own reference at any time.
    virtual void fetch(const std::vector<NodeIndex> &nodes, qint64 firstHour, qint64 lastHour,
                       std::shared_ptr<WindField> field, Done done) = 0;
};

} // namespace sp
