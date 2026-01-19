#include "logger.h"
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QDebug>

void Logger::log(const QString &fileName, const QString &mode,
                 const QString &plate, const QString &groundTruth, bool isCorrect,
                 double confidence, long preCost, long netCost, long totalCost, bool isSuccess)
{
    // 追加模式写入 benchmark_report.csv
    QFile file("benchmark_report.csv");
    bool needHeader = !file.exists();

    if (file.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream out(&file);

        if (needHeader) {
            out << "Time,FileName,Mode,Result,GroundTruth,Correct,Conf,Pre(ms),Net(ms),Total(ms),API_OK\n";
        }

        out << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss") << ","
            << fileName << ","
            << mode << ","
            << plate << ","
            << groundTruth << ","
            << (isCorrect ? "YES" : "NO") << ","
            << QString::number(confidence, 'f', 2) << ","
            << preCost << ","
            << netCost << ","
            << totalCost << ","
            << (isSuccess ? "OK" : "FAIL") << "\n";

        file.close();
    }
}
