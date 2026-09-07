#ifndef IMAGE_THUMBNAILCACHE_H
#define IMAGE_THUMBNAILCACHE_H

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QVector>
#include <QtGlobal>

namespace image {

/*!
 * \brief Thumbnails on disk, downscaled on the way in.
 *
 * Section 8: decode once, scale to roughly twice the delegate's logical width to cover
 * the display's pixel ratio, re-encode as JPEG, and keep only that. Decoding 1200-pixel
 * originals for a 180-pixel box on every scroll is the fastest way to make a list stutter,
 * and keeping the originals would fill the cache with data nothing ever displays.
 *
 * Bounded by total size with least-recently-used eviction. Lives under
 * QStandardPaths::CacheLocation, so the system may delete it and the app has to cope --
 * which it does: a missing file is simply fetched again.
 *
 * Nothing here re-hosts or proxies anything (section 11). An image is fetched from the
 * publisher's own server and kept locally for offline reading, which is what any reader
 * does.
 */
class ThumbnailCache
{
public:
    /*! Longest edge of a stored thumbnail, in pixels. */
    static constexpr int TargetSize = 360;

    /*! JPEG quality. 80 is the point where artefacts stop being visible at this size. */
    static constexpr int JpegQuality = 80;

    /*! Default cap on the whole cache. */
    static constexpr qint64 DefaultMaxBytes = 100LL * 1024 * 1024;

    explicit ThumbnailCache(const QString &directory, qint64 maxBytes = DefaultMaxBytes);

    /*! Creates the directory and measures what is already there. */
    bool open();

    QString directory() const { return m_directory; }
    qint64 maxBytes() const { return m_maxBytes; }
    void setMaxBytes(qint64 bytes);

    /*! Bytes currently held. */
    qint64 usedBytes() const { return m_usedBytes; }
    int count() const;

    /*! Where the thumbnail for an article would live, whether or not it exists. */
    QString pathFor(qint64 articleId) const;

    /*! True when a thumbnail is on disk for this article. */
    bool contains(qint64 articleId) const;

    /*!
     * \brief Decodes, downscales and stores one image.
     *
     * \return the file path, or an empty string when the data was not a usable image.
     */
    QString store(qint64 articleId, const QByteArray &imageData);

    /*! Marks a thumbnail as used, so eviction takes it last. */
    void touch(qint64 articleId);

    void remove(qint64 articleId);

    /*! Drops least-recently-used files until the cache is back inside its cap. */
    int evictToFit();

    /*! Removes thumbnails whose article is gone, as retention purges the database. */
    int removeAllExcept(const QVector<qint64> &keepArticleIds);

    void clear();

private:
    void measure();

    QString m_directory;
    qint64 m_maxBytes;
    qint64 m_usedBytes = 0;
};

} // namespace image

#endif // IMAGE_THUMBNAILCACHE_H
