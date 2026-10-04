/*
 * id3_rewrite.h — Pure C ID3v2.3 tag rewriter
 *
 * Rewrites MP3 files with ID3v2.3 + ID3v1 tags.
 * No ffmpeg, no libav*, no external dependencies.
 * Fixes Zune ignoring ID3v2.4 tags.
 */

#ifndef ID3_REWRITE_H
#define ID3_REWRITE_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Rewrite an MP3 file's ID3 tags to ID3v2.3 + ID3v1.1.
 *
 * The Zune ignores ID3v2.4 tags entirely, displaying "Unknown Artist"
 * and "Unknown Album". This function:
 *   1. Reads existing ID3v2.x tag (v2.2, v2.3, or v2.4)
 *   2. Extracts metadata frames (title, artist, album, genre, track#, art)
 *   3. Finds the start of raw MPEG audio data
 *   4. Writes a new file: ID3v2.3 header + frames + audio + ID3v1 tail
 *
 * If album_artist is non-NULL and non-empty, the TPE1 (artist) frame is
 * overridden with that value so the Zune groups tracks under the album
 * artist rather than splitting on per-track "feat. X" strings.
 *
 * No re-encoding — audio data is copied byte-for-byte.
 *
 * Returns: path to temp file (caller must free() AND unlink()).
 *          NULL on failure.
 */
char *zuuned_retag_mp3(const char *mp3_path, const char *album_artist);

/* Positive disc_number overrides TPOS; zero/negative preserves a valid source
 * disc and total. Invalid source disc tags are omitted. Audio is unchanged. */
char *zuuned_retag_mp3_with_disc(const char *mp3_path, const char *album_artist, int disc_number);

/*
 * Full-field tag edit (UX-2 Edit Info). NULL/empty string fields and
 * track/year <= 0 keep the file's existing frames. Rewrites the file
 * IN PLACE (sibling temp + atomic rename) as ID3v2.3 + ID3v1.1.
 *
 * Returns 0 on success, -1 on failure (not an MP3, I/O error) — the
 * original file is untouched on failure.
 */
typedef struct {
    const char *title;         /* TIT2 */
    const char *artist;        /* TPE1 */
    const char *album_artist;  /* TPE2 */
    const char *album;         /* TALB */
    const char *genre;         /* TCON */
    int track;                 /* TRCK */
    int year;                  /* TYER */
} ZuunedId3Edits;

int zuuned_edit_mp3_tags(const char *mp3_path, const ZuunedId3Edits *edits);

#ifdef __cplusplus
}
#endif

#endif /* ID3_REWRITE_H */
