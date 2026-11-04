#pragma once
#include <Epub.h>
#include <GfxRenderer.h>

#include <memory>
#include <optional>
#include <string>

#include "WaddlePosition.h"
#include "KOReaderSyncClient.h"

/**
 * Progress position representation.
 */
struct SavedProgressPosition {
  std::string xpath;  // XPath-like progress string
  float percentage;   // Progress percentage (0.0 to 1.0)
};

/**
 * Maps between Waddle and SavedProgress position formats, such as those used by KOReader.
 *
 * Waddle tracks position as (spineIndex, visibleTextOffset). Page number is
 * derived from the current section layout.
 * SavedProgress uses XPath-like strings + percentage.
 *
 * The section cache records page-start visible offsets during pagination. The
 * same body-text counting rules are used to generate and resolve KOReader
 * XPaths. Percentage remains metadata and a fallback only.
 */
class ProgressMapper {
 public:
  /**
   * Convert Waddle position to SavedProgress format.
   *
   * @param epub The EPUB book
   * @param pos Waddle position
   * @return SavedProgress position
   */
  static SavedProgressPosition toSavedProgress(const std::shared_ptr<Epub>& epub, const WaddlePosition& pos);

  /**
   * Convert SavedProgress position to Waddle format.
   *
   * Note: The returned pageNumber may be approximate since different
   * rendering settings produce different page counts.
   *
   * @param epub The EPUB book
   * @param savedPos SavedProgress position
   * @param renderer GfxRenderer for page count estimation
   * @param currentSpineIndex Index of the currently open spine item (for density estimation)
   * @param totalPagesInCurrentSpine Total pages in the current spine item (for density estimation)
   * @return Waddle position
   */
  static WaddlePosition toWaddle(const std::shared_ptr<Epub>& epub, const SavedProgressPosition& savedPos,
                                         GfxRenderer& renderer, int currentSpineIndex = -1,
                                         int totalPagesInCurrentSpine = 0, int fallbackTotalPages = 0);

  /**
   * Convert a rich position (downloaded from the original sync
   * server) directly to a Waddle position. Its standard KOReader XPath is
   * resolved to a content offset first; legacy spine/page/paragraph hints are
   * used only when that content anchor cannot be applied.
   *
   * @param xpathAlreadyTried when true, skip re-resolving rich.xpath and go straight to the
   *        legacy page hints. The caller sets this when it just resolved the identical XPath via
   *        toWaddle(), so retrying it here would decompress the chapter twice for nothing.
   * @return The position, or std::nullopt when the rich position cannot be
   *         applied (spine out of range, no section cache) and the caller
   *         should fall back to toWaddle().
   */
  static std::optional<WaddlePosition> fromRichPosition(const std::shared_ptr<Epub>& epub,
                                                            const KOReaderRichPosition& rich, GfxRenderer& renderer,
                                                            bool xpathAlreadyTried = false);

 private:
  /**
   * Generate a fallback XPath by streaming the spine item's XHTML and resolving
   * a paragraph/text position from intra-spine progress.
   * Produces a full ancestry path such as
   * /body/DocFragment[3]/body/p[42]/text().17.
   */
  static std::string generateXPath(const std::shared_ptr<Epub>& epub, int spineIndex, float intraSpineProgress);
};
