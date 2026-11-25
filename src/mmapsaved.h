#pragma once
#include <QFile>
#include <QObject>
#include <QString>

class MmapSaved : public QObject {
    Q_OBJECT
    Q_PROPERTY(qint64 position READ position WRITE setPosition)
public:
    explicit MmapSaved(const QString &path, QObject *parent = nullptr)
        : QObject(parent)
    {
        m_file.setFileName(path);
        if (m_file.open(QIODevice::ReadWrite)) {
            m_file.resize(kMapSize);
            m_map = m_file.map(0, kMapSize);
        }
    }

    ~MmapSaved() override
    {
        if (m_map)
            m_file.unmap(m_map);
    }

    qint64 position() const
    {
        if (!m_map)
            return 0;
        return *reinterpret_cast<const qint64 *>(m_map);
    }

    void setPosition(qint64 position)
    {
        if (!m_map)
            return;
        *reinterpret_cast<qint64 *>(m_map) = position;
    }

private:
    static constexpr qint64 kMapSize = sizeof(qint64);

    QFile m_file;
    uchar *m_map = nullptr;
};
