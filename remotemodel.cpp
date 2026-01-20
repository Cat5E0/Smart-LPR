#include "remotemodel.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QBuffer>
#include <QDebug>
#include <QDateTime>
#include <QUuid>
#include <QUrlQuery>
#include <QMessageAuthenticationCode>
#include <QCryptographicHash>

RemoteModel::RemoteModel(QObject *parent) : QObject(parent) {
    netManager = new QNetworkAccessManager(this);
}
RemoteModel::~RemoteModel() {}

void RemoteModel::recognizeLicensePlate(const QImage &image) {
    if(image.isNull()) { emit errorOccurred("Image is Null"); return; }

    // --- [优化1] 智能缩放：防止云端暴力压缩 ---
    // CCPD原图分辨率较高，直接上传会被云端强制压缩导致模糊。
    // 主动缩放到 OCR 最佳宽度 (约 2048px)，并使用高质量插值。
    QImage uploadImg = image;
    if (uploadImg.width() > 2048) {
        uploadImg = uploadImg.scaledToWidth(2048, Qt::SmoothTransformation);
    }
    // ---------------------------------------

    currentImageData.clear();
    QBuffer buffer(&currentImageData);
    buffer.open(QIODevice::WriteOnly);

    // --- [优化2] 格式优化：使用 PNG 无损格式 ---
    // 避免 JPG 压缩带来的边缘噪点（Ringing Artifacts）
    uploadImg.save(&buffer, "PNG");
    // ---------------------------------------

    if(currentImageData.size() == 0) { emit errorOccurred("Image Buffer Empty"); return; }

    emit statusLog(QString("Uploading... (%1 KB)").arg(currentImageData.size() / 1024.0));

    // [注意] 后缀名改为 .png
    QString objectName = "lpr_auto_" + QUuid::createUuid().toString(QUuid::Id128) + ".png";
    QString host = QString("%1.oss-%2.aliyuncs.com").arg(bucketName).arg(region);
    QString ossUrl = "https://" + host + "/" + objectName;

    QDateTime now = QDateTime::currentDateTimeUtc();
    QString isoDate = now.toString("yyyyMMddTHHmmssZ");
    QString dateShort = now.toString("yyyyMMdd");

    QString canonicalUri = "/" + bucketName + "/" + objectName;

    // [注意] Content-Type 必须与实际上传格式一致，否则签名会挂
    QString canonicalHeaders = "content-type:image/png\n"
                               "host:" + host + "\n"
                               "x-oss-content-sha256:UNSIGNED-PAYLOAD\n"
                               "x-oss-date:" + isoDate + "\n";
    QString additionalHeaders = "content-type;host;x-oss-content-sha256;x-oss-date";
    QString canonicalRequest = "PUT\n" + canonicalUri + "\n\n" + canonicalHeaders + "\n" + additionalHeaders + "\nUNSIGNED-PAYLOAD";
    QString stringToSign = "OSS4-HMAC-SHA256\n" + isoDate + "\n" + QString("%1/%2/oss/aliyun_v4_request").arg(dateShort).arg(region) + "\n" + sha256Hex(canonicalRequest.toUtf8());

    QByteArray keySecret = ("aliyun_v4" + accessKeySecret).toUtf8();
    QByteArray dateKey = hmacSha256(keySecret, dateShort.toUtf8());
    QByteArray regionKey = hmacSha256(dateKey, region.toUtf8());
    QByteArray serviceKey = hmacSha256(regionKey, "oss");
    QByteArray signingKey = hmacSha256(serviceKey, "aliyun_v4_request");
    QString ossSignature = hmacSha256(signingKey, stringToSign.toUtf8()).toHex();
    QString authHeader = QString("OSS4-HMAC-SHA256 Credential=%1/%2/%3/oss/aliyun_v4_request,AdditionalHeaders=%4,Signature=%5")
                         .arg(accessKeyId).arg(dateShort).arg(region).arg(additionalHeaders).arg(ossSignature);

    QNetworkRequest request((QUrl(ossUrl)));
    // [注意] 请求头也要改为 image/png
    request.setHeader(QNetworkRequest::ContentTypeHeader, "image/png");
    request.setRawHeader("Host", host.toUtf8());
    request.setRawHeader("x-oss-date", isoDate.toUtf8());
    request.setRawHeader("x-oss-content-sha256", "UNSIGNED-PAYLOAD");
    request.setRawHeader("Authorization", authHeader.toUtf8());

    QNetworkReply *reply = netManager->put(request, currentImageData);
    reply->setProperty("uploaded_url", ossUrl);
    connect(reply, &QNetworkReply::finished, this, &RemoteModel::onOssUploadFinished);
}

void RemoteModel::onOssUploadFinished() {
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    if(!reply) return;
    if(reply->error() != QNetworkReply::NoError) {
        emit errorOccurred("OSS Upload Failed: " + reply->errorString());
        reply->deleteLater(); return;
    }
    QString uploadedUrl = reply->property("uploaded_url").toString();
    reply->deleteLater();

    emit statusLog("Upload OK. Requesting OCR...");

    QMap<QString, QString> params;
    params.insert("AccessKeyId", accessKeyId);
    params.insert("Action", "RecognizeLicensePlate");
    params.insert("Format", "JSON");
    params.insert("ImageURL", uploadedUrl);
    params.insert("RegionId", "cn-shanghai");
    params.insert("SignatureMethod", "HMAC-SHA1");
    params.insert("SignatureNonce", QUuid::createUuid().toString().remove('{').remove('}'));
    params.insert("SignatureVersion", "1.0");
    params.insert("Timestamp", QDateTime::currentDateTimeUtc().toString("yyyy-MM-ddTHH:mm:ssZ"));
    params.insert("Version", "2019-12-30");

    QString apiSignature = calculateApiSignature(params, accessKeySecret);
    params.insert("Signature", apiSignature);

    QUrlQuery bodyQuery;
    QMapIterator<QString, QString> i(params);
    while (i.hasNext()) {
        i.next();
        bodyQuery.addQueryItem(i.key(), i.value());
    }

    QNetworkRequest apiReq(QUrl("https://ocr.cn-shanghai.aliyuncs.com/"));
    apiReq.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    QNetworkReply *apiReply = netManager->post(apiReq, bodyQuery.toString(QUrl::FullyEncoded).toUtf8());
    connect(apiReply, &QNetworkReply::finished, this, &RemoteModel::onApiFinished);
}

void RemoteModel::onApiFinished() {
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    if(!reply) return;

    QByteArray res = reply->readAll();

    if(reply->error() == QNetworkReply::NoError) {
        QJsonDocument doc = QJsonDocument::fromJson(res);
        QJsonObject root = doc.object();

        if(root.contains("Data")) {
            QJsonObject dataObj = root["Data"].toObject();
            QJsonObject plateObj;
            if(dataObj.contains("Plates")) {
                QJsonValue pv = dataObj["Plates"];
                if (pv.isArray() && !pv.toArray().isEmpty()) plateObj = pv.toArray().first().toObject();
                else if (pv.isObject()) plateObj = pv.toObject();
            }
            if(plateObj.isEmpty() && dataObj.contains("PlateNumber")) plateObj = dataObj;

            if(!plateObj.isEmpty() && plateObj.contains("PlateNumber")) {
                QString plate = plateObj["PlateNumber"].toString();
                double confidence = plateObj["Confidence"].toDouble();
                QRect rect(0,0,0,0);
                if(plateObj.contains("Roi")) {
                    QJsonObject roi = plateObj["Roi"].toObject();
                    rect.setRect(roi["X"].toInt(), roi["Y"].toInt(), roi["W"].toInt(), roi["H"].toInt());
                }
                emit recognitionFinished(plate, confidence, rect);
            } else {
                 emit errorOccurred("No Plate Found");
            }
        } else {
            emit errorOccurred("API Error: " + QString(res));
        }
    } else {
        emit errorOccurred("Net Error: " + QString(res));
    }
    reply->deleteLater();
}

// [核心修复] RFC 3986 编码
QString RemoteModel::percentEncode(const QString &input) {
    QByteArray ba = input.toUtf8();
    QByteArray res;
    for (int i = 0; i < ba.size(); ++i) {
        char c = ba[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            res.append(c);
        } else {
            res.append('%');
            res.append(QString::number((unsigned char)c, 16).toUpper().rightJustified(2, '0'));
        }
    }
    return QString(res);
}

QString RemoteModel::calculateApiSignature(QMap<QString, QString> params, const QString &secret) {
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

QByteArray RemoteModel::hmacSha256(const QByteArray &key, const QByteArray &data) {
    return QMessageAuthenticationCode::hash(data, key, QCryptographicHash::Sha256);
}
QString RemoteModel::sha256Hex(const QByteArray &data) {
    return QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex();
}
