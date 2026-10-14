/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "common.h"
static int part_order(const void *a, const void *b)
{
    const ZuneAudiobookPart *x = a, *y = b;
    if (x->parent_id != y->parent_id) return x->parent_id < y->parent_id ? -1 : 1;
    if (x->part_number != y->part_number) return x->part_number < y->part_number ? -1 : 1;
    return x->item_id < y->item_id ? -1 : x->item_id != y->item_id;
}
int main(int argc, char **argv)
{
    if (argc > 1) {
        puts("Usage: spoken_inventory\nRead shows, episodes, ordered book parts and firmware state. Never deletes media.");
        return argc == 2 && !strcmp(argv[1], "--help") ? 0 : 2;
    }
    ZuneDeviceHandle dev = zune_breach();
    if (!dev) return failure(NULL, "Connect");
    ZunePodcastSeries *shows = NULL;
    ZunePodcastEpisode *episodes = NULL;
    ZuneAudiobookPart *parts = NULL;
    ZunePodcastState *states = NULL;
    ZunePodcastEpisodeSource *sources = NULL;
    int ns = 0, ne = 0, nb = 0, nt = 0, nu = 0, result = 1;
    if (zune_get_podcast_series(dev, &shows, &ns)) { failure(dev, "Shows unavailable"); goto done; }
    if (zune_get_podcast_episodes(dev, &episodes, &ne)) { failure(dev, "Episodes unavailable"); goto done; }
    if (zune_get_audiobook_parts(dev, &parts, &nb)) { failure(dev, "Books unavailable"); goto done; }
    for (int i = 0; i < ns; ++i)
        printf("show=%" PRIu32 " name=%s\n", shows[i].item_id, text(shows[i].name));
    for (int i = 0; i < ne; ++i) {
        const char *name = episodes[i].series_name;
        if (!name || !*name) for (int j = 0; j < ns; ++j)
            if (shows[j].item_id == episodes[i].series_id) { name = shows[j].name; break; }
        printf("episode=%" PRIu32 " show=%" PRIu32 " (%s) title=%s\n",
               episodes[i].item_id, episodes[i].series_id, text(name), text(episodes[i].title));
    }
    if (nb > 1) qsort(parts, (size_t)nb, sizeof(*parts), part_order);
    for (int i = 0; i < nb; ++i)
        printf("book-folder=%" PRIu32 " part=%" PRIu32 " id=%" PRIu32 " title=%s\n",
               parts[i].parent_id, parts[i].part_number, parts[i].item_id, text(parts[i].title));
    if (zune_get_podcast_states(dev, &states, &nt)) { failure(dev, "Listening state unknown; preserve episodes"); goto done; }
    if (zune_get_podcast_episode_sources(dev, &sources, &nu)) { failure(dev, "Source identities unavailable"); goto done; }
    for (int i = 0; i < nt; ++i) {
        const ZunePodcastEpisode *ep = NULL;
        const ZunePodcastEpisodeSource *source = NULL;
        for (int j = 0; j < ne; ++j)
            if (episodes[j].item_id == states[i].item_id && episodes[j].series_id == states[i].series_id &&
                episodes[j].duration_ms == states[i].duration_ms && !strcmp(text(episodes[j].title), text(states[i].title))) ep = &episodes[j];
        for (int j = 0; j < nu; ++j)
            if (sources[j].item_id == states[i].item_id && sources[j].series_id == states[i].series_id &&
                ep && sources[j].parent_id == ep->parent_id) source = &sources[j];
        printf("state id=%" PRIu32 " marked-played=%u bookmark-ms=%" PRIu32 " live-correlation=%s\n",
               states[i].item_id, (unsigned)states[i].marked_played, states[i].bookmark_ms,
               source ? "matched (not deletion permission)" : "unknown; preserve");
    }
    result = 0;
done:
    zune_free_podcast_episode_sources(sources, nu);
    zune_free_podcast_states(states, nt);
    zune_free_audiobook_parts(parts, nb);
    zune_free_podcast_episodes(episodes, ne);
    zune_free_podcast_series(shows, ns);
    zune_sever(dev);
    return result;
}
