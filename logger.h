#ifndef LOGGER_H
#define LOGGER_H

#include <QString>

class Logger
{
public:
    static void log(const QString &fileName, const QString &mode,
                    const QString &plate, const QString &groundTruth, bool isCorrect,
                    double confidence, long preCost, long netCost, long totalCost, bool isSuccess);
};

#endif // LOGGER_H
