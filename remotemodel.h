#ifndef REMOTEMODEL_H
#define REMOTEMODEL_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QImage>
#include <QMap>
#include <QByteArray>
#include <QRect>

class RemoteModel : public QObject
{
    Q_OBJECT
public:
    explicit RemoteModel(QObject *parent = nullptr);
    ~RemoteModel();

    void recognizeLicensePlate(const QImage &image);

signals:
    void recognitionFinished(QString plateNumber, double confidence, QRect rect);
    void errorOccurred(QString errorMsg);
    void statusLog(QString msg);

private slots:
    void onOssUploadFinished();
    void onApiFinished();

private:
    QNetworkAccessManager *netManager;

    const QString accessKeyId = "LTAI5tRktaGKnLTgxUb4FVRw";
    const QString accessKeySecret = "gNASf41xb4CnUZ7rXfPV4wpjyv7x2U";
    const QString bucketName = "qt-ubuntu";
    const QString region = "cn-shanghai";

    QByteArray currentImageData;

    QString calculateApiSignature(QMap<QString, QString> params, const QString &secret);
    QString percentEncode(const QString &input);
    QByteArray hmacSha256(const QByteArray &key, const QByteArray &data);
    QString sha256Hex(const QByteArray &data);
};

#endif // REMOTEMODEL_H
