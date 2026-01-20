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

    // 1. 尺寸拦截 (本地过滤垃圾图)
    if(image.width() < 64 || image.height() < 24) {
        emit errorOccurred("Image Too Small (Local Filtered)");
        return;
    }

    retryCount = 0; // 重置重试计数

    // 2. 智能缩放
    QImage uploadImg = image;
    if (uploadImg.width() > 2048) {
        uploadImg = uploadImg.scaledToWidth(2048, Qt::SmoothTransformation);
    }

    currentImageData.clear();
    QBuffer buffer(&currentImageData);
    buffer.open(QIODevice::WriteOnly);
    uploadImg.save(&buffer, "PNG"); // 使用 PNG 防止模糊

    if(currentImageData.size() == 0) { emit errorOccurred("Image Buffer Empty"); return; }

    emit statusLog(QString("Uploading... (%1 KB)").arg(currentImageData.size() / 1024.0));

    // 3. 准备上传路径
    QString objectName = "lpr_auto_" + QUuid::createUuid().toString(QUuid::Id128) + ".png";
    QString host = QString("%1.oss-%2.aliyuncs.com").arg(bucketName).arg(region);
    QString ossUrl = "https://" + host + "/" + objectName;

    // --- [修正] 切换回 OSS V1 签名 (更简单稳定) ---
    // 格式: Authorization: OSS AccessKeyId:Signature
    // Signature = Base64( HMAC-SHA1( Secret, Verb + "\n" + Content-Md5 + "\n" + Content-Type + "\n" + Date + "\n" + CanonicalizedResource ) )

    QDateTime now = QDateTime::currentDateTimeUtc().addMSecs(timeOffset);
    QString dateStr = now.toString("ddd, dd MMM yyyy HH:mm:ss 'GMT'"); // RFC 1123 格式

    QString contentType = "image/png";
    QString resource = "/" + bucketName + "/" + objectName;

    QString stringToSign = "PUT\n\n" + contentType + "\n" + dateStr + "\n" + resource;

    QByteArray signature = QMessageAuthenticationCode::hash(stringToSign.toUtf8(), accessKeySecret.toUtf8(), QCryptographicHash::Sha1).toBase64();
    QString authHeader = "OSS " + accessKeyId + ":" + signature;

    QNetworkRequest request((QUrl(ossUrl)));
    request.setHeader(QNetworkRequest::ContentTypeHeader, contentType);
    request.setRawHeader("Host", host.toUtf8());
    request.setRawHeader("Date", dateStr.toUtf8()); // 必须有 Date 头
    request.setRawHeader("Authorization", authHeader.toUtf8());

    QNetworkReply *reply = netManager->put(request, currentImageData);
    reply->setProperty("uploaded_url", ossUrl);
    connect(reply, &QNetworkReply::finished, this, &RemoteModel::onOssUploadFinished);
}

void RemoteModel::onOssUploadFinished() {
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    if(!reply) return;

    if(reply->error() != QNetworkReply::NoError) {
        // 如果上传失败，尝试读取服务器时间校准（防止是因为时间导致的 403）
        parseServerTimeAndAdjust(reply);
        emit errorOccurred("OSS Upload Failed: " + reply->errorString());
        reply->deleteLater();
        return;
    }

    currentUploadedUrl = reply->property("uploaded_url").toString();
    reply->deleteLater();

    emit statusLog("Upload OK. Requesting OCR...");
    performOcrRequest();
}

void RemoteModel::performOcrRequest() {
    QMap<QString, QString> params;
    params.insert("AccessKeyId", accessKeyId);
    params.insert("Action", "RecognizeLicensePlate");
    params.insert("Format", "JSON");
    params.insert("ImageURL", currentUploadedUrl);
    params.insert("RegionId", "cn-shanghai");
    params.insert("SignatureMethod", "HMAC-SHA1");
    params.insert("SignatureNonce", QUuid::createUuid().toString().remove('{').remove('}'));
    params.insert("SignatureVersion", "1.0");

    // [核心] 使用校准后的时间
    params.insert("Timestamp", QDateTime::currentDateTimeUtc().addMSecs(timeOffset).toString("yyyy-MM-ddTHH:mm:ssZ"));
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

    // 检查是否是鉴权错误
    bool isAuthError = false;
    QString errorCode = "";

    if(reply->error() != QNetworkReply::NoError) {
        QJsonDocument doc = QJsonDocument::fromJson(res);
        if(!doc.isNull() && doc.isObject()) {
            errorCode = doc.object()["Code"].toString();
            // 阿里云常见时间错误码
            if(errorCode == "SignatureDoesNotMatch" || errorCode == "RequestTimeTooSkewed") {
                isAuthError = true;
            }
        }
    }

    // [自动重试机制]
    if (isAuthError && retryCount < MAX_RETRIES) {
        qDebug() << "[Auth] Detected Time Skew. Auto-Correcting...";
        parseServerTimeAndAdjust(reply); // 1. 校准时间
        retryCount++;                    // 2. 增加计数
        emit statusLog(QString("Auth Error. Retrying %1/%2...").arg(retryCount).arg(MAX_RETRIES));
        performOcrRequest();             // 3. 立即重试
        reply->deleteLater();
        return;
    }

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

// 解析服务器时间并计算偏差
void RemoteModel::parseServerTimeAndAdjust(QNetworkReply *reply) {
    if(!reply->hasRawHeader("Date")) return;

    QString dateStr = reply->rawHeader("Date");
    QLocale locale(QLocale::English);
    QDateTime serverTime = locale.toDateTime(dateStr, "ddd, d MMM yyyy HH:mm:ss 'GMT'");
    serverTime.setTimeSpec(Qt::UTC);

    if(serverTime.isValid()) {
        QDateTime localTime = QDateTime::currentDateTimeUtc();
        qint64 newOffset = localTime.msecsTo(serverTime);
        timeOffset = newOffset;
        qDebug() << "[Auth] Auto-Calibrated Offset:" << timeOffset << "ms";
    }
}

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
