#ifndef COLORANALYZER_H
#define COLORANALYZER_H

#include <QObject>

class coloranalyzer : public QObject
{
    Q_OBJECT
public:
    explicit coloranalyzer(QObject *parent = nullptr);

signals:

};

#endif // COLORANALYZER_H
