#include "thumbnailcache.h"

#include <QBuffer>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QSet>

#include <algorithm>

#include <utime.h>

namespace image {
namespace {

const QString Extension = QStringLiteral(".jpg");

/*! Largest source image we will decode at all, as a guard against decompression bombs. */
constexpr int MaxSourceDimension = 8000;

} // namespace

ThumbnailCache::ThumbnailCache(const QString &directory, qint64 maxBytes)
    : m_directory(directory)
    , m_maxBytes(maxBytes > 0 ? maxBytes : DefaultMaxBytes)
{
}

bool ThumbnailCache::open()
{
    QDir dir(m_directory);
    if (!dir.exists() && !dir.mkpath(QStringLiteral(".")))
        return false;
    measure();
    return true;
}

void ThumbnailCache::measure()
{
    m_usedBytes = 0;
    const QDir dir(m_directory);
    const QFileInfoList files =
        dir.entryInfoList(QStringList() << QStringLiteral("*") + Extension, QDir::Files);
    for (const QFileInfo &info : files)
        m_usedBytes += info.size();
}

void ThumbnailCache::setMaxBytes(qint64 bytes)
{
    if (bytes <= 0)
        return;
    m_maxBytes = bytes;
    evictToFit();
}

int ThumbnailCache::count() const
{
    const QDir dir(m_directory);
    return dir.entryList(QStringList() << QStringLiteral("*") + Extension, QDir::Files).size();
}

QString ThumbnailCache::pathFor(qint64 articleId) const
{
    return m_directory + QLatin1Char('/') + QString::number(articleId) + Extension;
}

bool ThumbnailCache::contains(qint64 articleId) const
{
    return QFileInfo::exists(pathFor(articleId));
}

QString ThumbnailCache::store(qint64 articleId, const QByteArray &imageData)
{
    if (imageData.isEmpty())
        return QString();

    QBuffer buffer;
    buffer.setData(imageData);
    if (!buffer.open(QIODevice::ReadOnly))
        return QString();

    QImageReader reader(&buffer);
    reader.setAutoTransform(true);

    // Refusing an image whose header declares no size, rather than decoding it to find
    // out: the check below is the only thing standing between a feed-supplied URL and a
    // full-size decode, and Qt 5.6 has no QImageReader::setAllocationLimit to fall back
    // on (that arrived in 5.14). A picture we cannot measure is a picture we do not take.
    const QSize sourceSize = reader.size();
    if (!sourceSize.isValid())
        return QString();
    if (sourceSize.width() > MaxSourceDimension || sourceSize.height() > MaxSourceDimension)
        return QString();

    // Scaling during decode, so the full-size image is never materialised. This is the
    // difference between a smooth list and a stuttering one on a phone.
    if (sourceSize.width() > TargetSize || sourceSize.height() > TargetSize)
        reader.setScaledSize(sourceSize.scaled(TargetSize, TargetSize, Qt::KeepAspectRatio));

    QImage decoded = reader.read();
    if (decoded.isNull())
        return QString();
    if (decoded.width() > TargetSize || decoded.height() > TargetSize) {
        // A format the reader could not scale while decoding.
        decoded = decoded.scaled(TargetSize, TargetSize, Qt::KeepAspectRatio,
                                 Qt::SmoothTransformation);
    }

    const QString path = pathFor(articleId);
    // pathFor() is deterministic, so re-storing an article overwrites its file. The size
    // already counted has to come off first, or the total drifts up on every rewrite and
    // eviction starts firing below the configured cap.
    const qint64 replaced = QFileInfo::exists(path) ? QFileInfo(path).size() : 0;
    if (!decoded.save(path, "JPEG", JpegQuality))
        return QString();

    m_usedBytes += QFileInfo(path).size() - replaced;
    evictToFit();
    // Eviction may have just removed what we wrote, if the cap is smaller than one file.
    return QFileInfo::exists(path) ? path : QString();
}

void ThumbnailCache::touch(qint64 articleId)
{
    // Eviction orders by modification time, so reading a thumbnail has to update it or
    // the most-viewed articles would be evicted first.
    //
    // Opening and closing the file does not do that -- nothing is written -- and
    // QFileDevice::setFileTime only exists from Qt 5.10, while Sailfish is on 5.6. utime()
    // with a null time sets both timestamps to now, which is exactly what is wanted.
    const QString path = pathFor(articleId);
    if (!QFileInfo::exists(path))
        return;
    ::utime(QFile::encodeName(path).constData(), nullptr);
}

void ThumbnailCache::remove(qint64 articleId)
{
    const QString path = pathFor(articleId);
    const QFileInfo info(path);
    if (!info.exists())
        return;
    const qint64 size = info.size();
    if (QFile::remove(path))
        m_usedBytes -= size;
}

int ThumbnailCache::evictToFit()
{
    if (m_usedBytes <= m_maxBytes)
        return 0;

    const QDir dir(m_directory);
    QFileInfoList files =
        dir.entryInfoList(QStringList() << QStringLiteral("*") + Extension, QDir::Files);
    std::sort(files.begin(), files.end(), [](const QFileInfo &a, const QFileInfo &b) {
        return a.lastModified() < b.lastModified();
    });

    int removed = 0;
    for (const QFileInfo &info : files) {
        if (m_usedBytes <= m_maxBytes)
            break;
        const qint64 size = info.size();
        if (QFile::remove(info.absoluteFilePath())) {
            m_usedBytes -= size;
            ++removed;
        }
    }
    return removed;
}

int ThumbnailCache::removeAllExcept(const QVector<qint64> &keepArticleIds)
{
    QSet<QString> keep;
    for (const qint64 id : keepArticleIds)
        keep.insert(QString::number(id) + Extension);

    const QDir dir(m_directory);
    const QFileInfoList files =
        dir.entryInfoList(QStringList() << QStringLiteral("*") + Extension, QDir::Files);

    int removed = 0;
    for (const QFileInfo &info : files) {
        if (keep.contains(info.fileName()))
            continue;
        const qint64 size = info.size();
        if (QFile::remove(info.absoluteFilePath())) {
            m_usedBytes -= size;
            ++removed;
        }
    }
    return removed;
}

void ThumbnailCache::clear()
{
    removeAllExcept(QVector<qint64>());
    m_usedBytes = 0;
}

} // namespace image
