// Store's cloud upload entry point. The protocol lives in CloudUploader; this
// file only owns the QML-facing task lifetime and the cloud-slot refresh.
#include "store.h"

#include "clouduploader.h"

void Store::uploadCloudSong(const QString &filePath)
{
    if (!m_client->hasMusicU()) {
        emit cloudUploadFailed(QStringLiteral("登录后才能上传到云盘"));
        return;
    }
    if (m_cloudUploader) {
        emit cloudUploadFailed(QStringLiteral("已有上传任务在进行"));
        return;
    }

    auto *uploader = new CloudUploader(m_client, this);
    m_cloudUploader = uploader;

    connect(uploader, &CloudUploader::progress, this, &Store::cloudUploadProgress);
    connect(uploader, &CloudUploader::finished, this,
            [this, uploader](double songId) {
        m_cloudUploader = nullptr;
        emit cloudUploadFinished(songId);
        refreshCloud();
        uploader->deleteLater();
    });
    connect(uploader, &CloudUploader::failed, this,
            [this, uploader](const QString &message) {
        m_cloudUploader = nullptr;
        emit cloudUploadFailed(message);
        uploader->deleteLater();
    });

    uploader->upload(filePath);
}

void Store::cancelCloudUpload()
{
    if (m_cloudUploader)
        m_cloudUploader->cancel();
}
