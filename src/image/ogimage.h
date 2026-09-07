#ifndef IMAGE_OGIMAGE_H
#define IMAGE_OGIMAGE_H

#include <QByteArray>
#include <QString>
#include <QUrl>

namespace image {

/*!
 * \brief Finds the article image in a page's HTML.
 *
 * Only what the publisher put there for exactly this purpose: `og:image`, then Twitter's
 * equivalent, then a `<link rel="image_src">`. Deliberately *not* a walk over the
 * document looking for the biggest picture — that is reader-mode extraction by another
 * name, and section 11 rules it out.
 *
 * \param html    the page, or as much of it as was read
 * \param baseUrl the article's own address, for resolving a relative image path
 */
QString extractPreviewImage(const QByteArray &html, const QUrl &baseUrl = QUrl());

/*!
 * \brief How much of a page has to be read before giving up on the metadata.
 *
 * The tags live in <head>, so a news page's first 200 kB is generous. Reading further
 * costs the publisher bandwidth for something that will not be there.
 */
constexpr int MaxHtmlBytes = 200 * 1024;

} // namespace image

#endif // IMAGE_OGIMAGE_H
