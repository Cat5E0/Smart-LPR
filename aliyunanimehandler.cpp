#include "aliyunanimehandler.h"
#include <QDateTime>
#include <QUuid>
#include <QMessageAuthenticationCode>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QBuffer>
#include <QUrlQuery>
#include <QDebug>

AliyunAnimeHandler::AliyunAnimeHandler(QObject *parent) : QObject(parent)
{
    netManager = new QNetworkAccessManager(this);
}

AliyunAnimeHandler::~AliyunAnimeHandler() {}

void AliyunAnimeHandler::generateStyle(const QImage &image, const QString &style)
{
    if(image.isNull()) {
        emit errorOccurred("图像数据为空");
        return;
    }

    currentStyle = style;
    currentImageData.clear();
    QBuffer buffer(&currentImageData);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "JPG", 85);

    emit statusLog("步骤1/2: 正在上传图片至 OSS...");

    QString objectName = "anime_temp.jpg";
    QString host = QString("%1.oss-cn-shanghai.aliyuncs.com").arg(bucketName);
    QString ossUrl = "http://" + host + "/" + objectName;

    QDateTime now = QDateTime::currentDateTimeUtc();
    QString isoDate = now.toString("yyyyMMddTHHmmssZ");
    QString dateShort = now.toString("yyyyMMdd");

    QString canonicalUri = "/" + bucketName + "/" + objectName;
    QString canonicalHeaders =
        "content-type:image/jpeg\n"
        "host:" + host + "\n"
        "x-oss-content-sha256:UNSIGNED-PAYLOAD\n"
        "x-oss-date:" + isoDate + "\n";

    QString additionalHeaders = "content-type;host;x-oss-content-sha256;x-oss-date";
    QString canonicalRequest = "PUT\n" + canonicalUri + "\n\n" + canonicalHeaders + "\n" + additionalHeaders + "\nUNSIGNED-PAYLOAD";

    QString scope = QString("%1/%2/oss/aliyun_v4_request").arg(dateShort).arg(region);
    QString stringToSign = "OSS4-HMAC-SHA256\n" + isoDate + "\n" + scope + "\n" + sha256Hex(canonicalRequest.toUtf8());

    QByteArray keySecret = ("aliyun_v4" + accessKeySecret).toUtf8();
    QByteArray dateKey = hmacSha256(keySecret, dateShort.toUtf8());
    QByteArray regionKey = hmacSha256(dateKey, region.toUtf8());
    QByteArray serviceKey = hmacSha256(regionKey, "oss");
    QByteArray signingKey = hmacSha256(serviceKey, "aliyun_v4_request");

    QString ossSignature = hmacSha256(signingKey, stringToSign.toUtf8()).toHex();
    QString authHeader = QString("OSS4-HMAC-SHA256 Credential=%1/%2/%3/oss/aliyun_v4_request,AdditionalHeaders=%4,Signature=%5")
                         .arg(accessKeyId).arg(dateShort).arg(region).arg(additionalHeaders).arg(ossSignature);

    QNetworkRequest request((QUrl(ossUrl)));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "image/jpeg");
    request.setRawHeader("Host", host.toUtf8());
    request.setRawHeader("x-oss-date", isoDate.toUtf8());
    request.setRawHeader("x-oss-content-sha256", "UNSIGNED-PAYLOAD");
    request.setRawHeader("Authorization", authHeader.toUtf8());

    // 使用刚才在 generateStyle 中填充的 currentImageData
    QNetworkReply *reply = netManager->put(request, currentImageData);
    reply->setProperty("uploaded_url", ossUrl);
    connect(reply, &QNetworkReply::finished, this, &AliyunAnimeHandler::onOssUploadFinished);
}

void AliyunAnimeHandler::onOssUploadFinished()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    if(!reply) return;

    QByteArray response = reply->readAll();
    if(reply->error() != QNetworkReply::NoError) {
        emit errorOccurred("OSS上传失败: " + response);
        reply->deleteLater();
        return;
    }

    QString uploadedUrl = reply->property("uploaded_url").toString();
    reply->deleteLater();

    emit statusLog("上传成功，正在生成动漫化效果...");

    QMap<QString, QString> params;
    params.insert("AccessKeyId", accessKeyId);
    params.insert("Action", "GenerateHumanAnimeStyle");
    params.insert("AlgoType", currentStyle);
    params.insert("Format", "JSON");
    params.insert("ImageURL", uploadedUrl);
    params.insert("RegionId", "cn-shanghai");
    params.insert("SignatureMethod", "HMAC-SHA1");
    params.insert("SignatureNonce", QUuid::createUuid().toString().remove('{').remove('}'));
    params.insert("SignatureVersion", "1.0");
    params.insert("Timestamp", QDateTime::currentDateTimeUtc().toString("yyyy-MM-ddTHH:mm:ssZ"));
    params.insert("Version", "2019-12-30");

    QString apiSignature = calculateApiSignature(params, accessKeySecret);

    QNetworkRequest apiReq(QUrl("https://facebody.cn-shanghai.aliyuncs.com/"));
    apiReq.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    QUrlQuery body;
    QMapIterator<QString, QString> p(params);
    while (p.hasNext()) {
        p.next();
        body.addQueryItem(p.key(), p.value());
    }
    body.addQueryItem("Signature", apiSignature);

    QNetworkReply *apiReply = netManager->post(apiReq, body.toString(QUrl::FullyEncoded).toUtf8());
    connect(apiReply, &QNetworkReply::finished, this, &AliyunAnimeHandler::onApiFinished);
}

void AliyunAnimeHandler::onApiFinished()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    if(!reply) return;

    QByteArray res = reply->readAll();
    QJsonObject root = QJsonDocument::fromJson(res).object();

    if(root.contains("Data")) {
        QString resultUrl = root["Data"].toObject()["ImageURL"].toString();
        emit generationSuccess(resultUrl);
    } else {
        emit errorOccurred("生成失败: " + QString(res));
    }
    reply->deleteLater();
}

QString AliyunAnimeHandler::calculateApiSignature(QMap<QString, QString> params, const QString &secret)
{
    QStringList canonicalList;
    QMapIterator<QString, QString> i(params);
    while (i.hasNext()) {
        i.next();
        canonicalList << (percentEncode(i.key()) + "=" + percentEncode(i.value()));
    }
    QString canonicalString = canonicalList.join("&");
    QString stringToSign = "POST&" + percentEncode("/") + "&" + percentEncode(canonicalString);

    QString key = secret + "&";
    QByteArray hmac = QMessageAuthenticationCode::hash(stringToSign.toUtf8(), key.toUtf8(), QCryptographicHash::Sha1);
    return hmac.toBase64();
}

QString AliyunAnimeHandler::percentEncode(const QString &input)
{
    QByteArray ba = QUrl::toPercentEncoding(input.toUtf8(), QByteArray("-_.~"));
    QString res = QString(ba);
    res.replace("*", "%2A");
    res.replace("+", "%20");
    res.replace("%7E", "~");
    return res;
}

QByteArray AliyunAnimeHandler::hmacSha256(const QByteArray &key, const QByteArray &data)
{
    return QMessageAuthenticationCode::hash(data, key, QCryptographicHash::Sha256);
}

QString AliyunAnimeHandler::sha256Hex(const QByteArray &data)
{
    return QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex();
}
