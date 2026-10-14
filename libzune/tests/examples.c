/* SPDX-License-Identifier: GPL-3.0-or-later
 * Public example contract tests. Every device symbol is implemented below;
 * no protocol/authentication/USB implementation is linked or invoked. */
#include "zune.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int example_device_info(int, char **);
int example_spoken_inventory(int, char **);
int example_podcast_send(int, char **);
int example_audiobook_send(int, char **);
typedef int (*Entry)(int, char **);
static int token;
#define DEVICE ((ZuneDeviceHandle)&token)
static struct {
    int connect_fail, inventory_fail, empty_inventory, missing_show, correlation;
    int lookup_fail, absent, relookup_fail, forge_status, send_status, brand_status, finalize_status;
    uint32_t failure_id;
    int breaches, severs, reads, allocations, frees, finds, folders, forges, sends, brands, finalizes;
    uint32_t sent_parent, sent_show, brand_target;
    char sent_name[128], sent_key[80], sent_title[128];
    unsigned sent_number;
} m;
static int checks;
static char output[16384];
static void check(int ok, const char *label)
{
    ++checks;
    if (!ok) { fprintf(stderr, "FAIL %s\nCaptured output:\n%s\n", label, output); exit(1); }
    printf("PASS %s\n", label);
}
static void reset(void) { memset(&m, 0, sizeof(m)); output[0] = 0; }
static int run(Entry entry, char **argv)
{
    int argc = 0; while (argv[argc]) ++argc;
    fflush(NULL);
    FILE *capture = tmpfile();
    if (!capture) { perror("tmpfile"); exit(2); }
    int saved_out = dup(STDOUT_FILENO), saved_err = dup(STDERR_FILENO);
    if (saved_out < 0 || saved_err < 0 || dup2(fileno(capture), STDOUT_FILENO) < 0 ||
        dup2(fileno(capture), STDERR_FILENO) < 0) { perror("capture"); exit(2); }
    int status = entry(argc, argv);
    fflush(NULL);
    if (dup2(saved_out, STDOUT_FILENO) < 0 || dup2(saved_err, STDERR_FILENO) < 0) exit(2);
    close(saved_out); close(saved_err);
    rewind(capture); size_t n = fread(output, 1, sizeof(output)-1, capture); output[n] = 0;
    fclose(capture);
    return status;
}
static int writes(void) { return m.folders + m.forges + m.sends + m.brands + m.finalizes; }
static void *owned(size_t count, size_t size)
{
    void *p = calloc(count, size); if (!p) exit(2); ++m.allocations; return p;
}
static int read_failed(void) { return ++m.reads == m.inventory_fail; }
static void write_file(const char *path, const void *bytes, size_t size)
{
    FILE *f = fopen(path, "wb"); if (!f || fwrite(bytes, 1, size, f) != size) exit(2); fclose(f);
}

ZuneDeviceHandle zune_breach(void) { ++m.breaches; return m.connect_fail ? NULL : DEVICE; }
void zune_sever(ZuneDeviceHandle d) { if (d == DEVICE) ++m.severs; else exit(2); }
const char *zune_get_name(ZuneDeviceHandle d) { (void)d; return NULL; }
const char *zune_get_model(ZuneDeviceHandle d) { (void)d; return "Fixture Zune"; }
const char *zune_get_serial(ZuneDeviceHandle d) { (void)d; return "fixture-serial"; }
uint8_t zune_get_battery(ZuneDeviceHandle d) { (void)d; return 75; }
const char *zune_get_error(void) { return "fixture failure"; }
uint16_t zune_autopsy(ZuneDeviceHandle d) { (void)d; return 0x02ff; }
int zune_get_podcast_series(ZuneDeviceHandle d, ZunePodcastSeries **out, int *n)
{
    (void)d; *out = NULL; *n = 0; if (read_failed()) return -1;
    if (m.empty_inventory) return 0;
    *out = owned(1, sizeof(**out)); *n = 1;
    **out = (ZunePodcastSeries){.item_id=m.missing_show?78:77,.name=strdup("Live device show"),
        .author=strdup("Author"),.feed_url=strdup("https://example.invalid/feed")};
    return 0;
}
void zune_free_podcast_series(ZunePodcastSeries *p, int n)
{
    if (!p) return;
    for (int i=0; i<n; ++i) { free(p[i].name); free(p[i].author); free(p[i].feed_url); }
    free(p); ++m.frees;
}
int zune_get_podcast_episodes(ZuneDeviceHandle d, ZunePodcastEpisode **out, int *n)
{
    (void)d; *out = NULL; *n = 0; if (read_failed()) return -1;
    if (m.empty_inventory) return 0;
    *out = owned(1, sizeof(**out)); *n = 1;
    **out = (ZunePodcastEpisode){.item_id=88,.parent_id=66,.series_id=77,.duration_ms=3000,
        .title=strdup("Episode"),.author=strdup("Author"),.series_name=strdup("")};
    return 0;
}
void zune_free_podcast_episodes(ZunePodcastEpisode *p, int n)
{
    if (!p) return;
    for (int i=0; i<n; ++i) { free(p[i].title); free(p[i].author); free(p[i].series_name); }
    free(p); ++m.frees;
}
int zune_get_audiobook_parts(ZuneDeviceHandle d, ZuneAudiobookPart **out, int *n)
{
    (void)d; *out = NULL; *n = 0; if (read_failed()) return -1;
    if (m.empty_inventory) return 0;
    *out = owned(3, sizeof(**out)); *n = 3;
    const unsigned parents[] = {10, 9, 9}, numbers[] = {1, 2, 1};
    for (int i=0; i<3; ++i) (*out)[i] = (ZuneAudiobookPart){.item_id=90+(uint32_t)i,
        .parent_id=parents[i],.part_number=numbers[i],.title=strdup("Chapter"),
        .author=strdup("Author"),.book_title=strdup("Book")};
    return 0;
}
void zune_free_audiobook_parts(ZuneAudiobookPart *p, int n)
{
    if (!p) return;
    for (int i=0; i<n; ++i) { free(p[i].title); free(p[i].author); free(p[i].book_title); }
    free(p); ++m.frees;
}
int zune_get_podcast_states(ZuneDeviceHandle d, ZunePodcastState **out, int *n)
{
    (void)d; *out = NULL; *n = 0; if (read_failed()) return -1;
    if (m.empty_inventory) return 0;
    *out = owned(1, sizeof(**out)); *n = 1;
    **out = (ZunePodcastState){.item_id=m.correlation==1?89:88,
        .series_id=m.correlation==2?78:77,.duration_ms=m.correlation==3?3001:3000,
        .bookmark_ms=2500,.raw_flags=0x200,.marked_played=1,
        .title=strdup(m.correlation==4?"Different title":"Episode")};
    return 0;
}
void zune_free_podcast_states(ZunePodcastState *p, int n)
{
    if (!p) return;
    for (int i=0; i<n; ++i) free(p[i].title);
    free(p); ++m.frees;
}
int zune_get_podcast_episode_sources(ZuneDeviceHandle d, ZunePodcastEpisodeSource **out, int *n)
{
    (void)d; *out = NULL; *n = 0; if (read_failed()) return -1;
    if (m.empty_inventory) return 0;
    *out = owned(1, sizeof(**out)); *n = 1;
    **out = (ZunePodcastEpisodeSource){.item_id=88,.series_id=77,.parent_id=m.correlation==5?67:66,
        .feed_url=strdup("https://example.invalid/feed"),.source_url=strdup("https://example.invalid/audio")};
    return 0;
}
void zune_free_podcast_episode_sources(ZunePodcastEpisodeSource *p, int n)
{
    if (!p) return;
    for (int i=0; i<n; ++i) { free(p[i].feed_url); free(p[i].source_url); }
    free(p); ++m.frees;
}
int zune_find_podcast_episode(ZuneDeviceHandle d, const char *feed, const char *source,
                            uint32_t *parent, uint32_t *show, uint32_t *id)
{
    (void)d; ++m.finds; *parent=*show=*id=0;
    if (strcmp(feed,"https://example.invalid/feed") || strcmp(source,"https://example.invalid/audio")) exit(2);
    if (m.lookup_fail || (m.relookup_fail && m.finds>1)) return -1;
    if (m.absent && m.finds==1) return 0;
    *parent=66; *show=77; *id=88; return 0;
}
uint32_t zune_forge_folder(ZuneDeviceHandle d, const char *name, uint32_t parent)
{
    (void)d; (void)name; (void)parent; ++m.folders; return 55;
}
int zune_forge_podcast_series(ZuneDeviceHandle d, uint32_t parent, const char *filename,
                            const char *name, const char *author, const char *feed, uint32_t *out)
{
    (void)d; (void)parent; (void)filename; (void)name; (void)author; (void)feed;
    ++m.forges; *out=m.forge_status?m.failure_id:77; return m.forge_status;
}
int zune_smuggle_podcast_episode(ZuneDeviceHandle d, uint32_t parent, const char *file,
                                const ZunePodcastEpisodeInput *episode, uint32_t *out)
{
    (void)d; (void)file; ++m.sends; m.sent_parent=parent; m.sent_show=episode->series_id;
    snprintf(m.sent_name,sizeof(m.sent_name),"%s",episode->series_name);
    *out=m.send_status?m.failure_id:88; return m.send_status;
}
int zune_find_audiobook_part(ZuneDeviceHandle d, const ZuneAudiobookPartInput *p,
                           uint32_t *parent, uint32_t *out)
{
    (void)d; (void)p; ++m.finds; *parent=*out=0;
    if (m.lookup_fail) return -1;
    *parent=44; *out=99; return 0;
}
int zune_smuggle_audiobook_part(ZuneDeviceHandle d, const char *file,
                              const ZuneAudiobookPartInput *p, uint32_t *out)
{
    (void)d; (void)file; ++m.sends;
    snprintf(m.sent_key,sizeof(m.sent_key),"%s",p->book_key);
    snprintf(m.sent_title,sizeof(m.sent_title),"%s",p->title); m.sent_number=p->part_number;
    *out=m.send_status?m.failure_id:99; return m.send_status;
}
int zune_brand(ZuneDeviceHandle d, uint32_t id, const uint8_t *jpeg, size_t n)
{
    (void)d; if (n!=4 || jpeg[0]!=0xff || jpeg[1]!=0xd8) exit(2);
    ++m.brands; m.brand_target=id; return m.brand_status;
}
int zune_finalize(ZuneDeviceHandle d) { (void)d; ++m.finalizes; return m.finalize_status; }

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    char audio[1024], cover[1024], bad_cover[1024], missing[1024];
    snprintf(audio,sizeof(audio),"%s/prepared.mp3",argv[1]);
    snprintf(cover,sizeof(cover),"%s/cover.jpg",argv[1]);
    snprintf(bad_cover,sizeof(bad_cover),"%s/bad.jpg",argv[1]);
    snprintf(missing,sizeof(missing),"%s/missing.mp3",argv[1]);
    write_file(audio,"caller-prepared immutable fixture",33);
    const uint8_t jpeg[]={0xff,0xd8,0xff,0xd9}; write_file(cover,jpeg,sizeof(jpeg));
    write_file(bad_cover,"not a jpeg",10);
    char key[]="0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    char *info[]={"device_info",NULL}, *inventory[]={"spoken_inventory",NULL};
    char *pod[]={"podcast_send","--send",audio,"https://example.invalid/feed",
        "https://example.invalid/audio","Requested renamed show","Author","Episode","3000",cover,NULL};
    char *book[]={"audiobook_send","--send",audio,key,"Book","Author","Chapter","2","3000",cover,NULL};
    Entry entries[]={example_device_info,example_spoken_inventory,example_podcast_send,example_audiobook_send};
    for (int i=0; i<4; ++i) {
        char *help[]={"example","--help",NULL}, *bad[]={"example","--invalid",NULL};
        reset(); check(run(entries[i],help)==0 && !m.breaches,"help never connects");
        reset(); check(run(entries[i],bad)==2 && !m.breaches,"invalid arguments never connect");
    }
    for (int kind=0; kind<2; ++kind) {
        char **args=kind?book:pod; Entry entry=kind?example_audiobook_send:example_podcast_send;
        char *saved=args[8]; args[8]="-1"; reset();
        check(run(entry,args)==2 && !m.breaches,"negative duration rejected before connection"); args[8]=saved;
        saved=args[2]; args[2]=missing; reset();
        check(run(entry,args)==2 && !m.breaches,"missing prepared input rejected before connection"); args[2]=saved;
        saved=args[9]; args[9]=bad_cover; reset();
        check(run(entry,args)==2 && !m.breaches,"invalid cover rejected before connection"); args[9]=saved;
        reset(); m.connect_fail=1;
        check(run(entry,args)==1 && m.breaches==1 && !m.severs && !writes(),"connection failure releases preloaded cover without device work");
    }
    char *saved=book[3]; book[3]="not-a-stable-book-key"; reset();
    check(run(example_audiobook_send,book)==2 && !m.breaches,"invalid book identity rejected before connection"); book[3]=saved;
    saved=book[7]; book[7]="65536"; reset();
    check(run(example_audiobook_send,book)==2 && !m.breaches,"out-of-range book order rejected before connection"); book[7]=saved;
    reset(); check(run(example_device_info,info)==0 && m.breaches==1 && m.severs==1 && !writes()
        && strstr(output,"Fixture Zune"),"device info releases handle and tolerates missing borrowed name");
    reset(); m.empty_inventory=1;
    check(run(example_spoken_inventory,inventory)==0 && m.reads==5 && !m.allocations && !m.frees && m.severs==1 && !writes()
        && !strstr(output,"unknown"),"known-empty inventory succeeds without interpreting unavailable state as empty");
    for (int stage=1; stage<=5; ++stage) {
        reset(); m.inventory_fail=stage;
        check(run(example_spoken_inventory,inventory)==1 && m.reads==stage && m.allocations==m.frees && m.severs==1 && !writes(),
              "inventory read failure frees earlier owned results, severs and preserves device");
        if (stage==4) check(strstr(output,"Listening state unknown; preserve episodes")!=NULL,"unavailable state is reported as unknown, never completed");
    }
    reset(); check(run(example_spoken_inventory,inventory)==0 && m.allocations==m.frees && m.severs==1 && !writes(),"complete inventory releases all arrays and never mutates device");
    check(strstr(output,"show=77 (Live device show)") && strstr(output,"matched (not deletion permission)")
        && strstr(output,"marked-played=1 bookmark-ms=2500"),"empty firmware series name resolves by ID and nonzero played bookmark stays visible");
    const char *one=strstr(output,"book-folder=9 part=1"), *two=strstr(output,"book-folder=9 part=2"), *other=strstr(output,"book-folder=10 part=1");
    check(one && two && other && one<two && two<other,"book inventory sorted by folder and part order");
    for (int mismatch=1; mismatch<=5; ++mismatch) {
        reset(); m.correlation=mismatch;
        check(run(example_spoken_inventory,inventory)==0 && strstr(output,"unknown; preserve") && !writes() && m.allocations==m.frees,
              "incompatible live identity remains unknown and preserves episode");
    }
    reset(); check(run(example_podcast_send,pod)==0 && m.finds==1 && !m.folders && !m.forges && m.sends==1
        && m.sent_parent==66 && m.sent_show==77 && !strcmp(m.sent_name,"Live device show")
        && m.allocations==m.frees && m.severs==1,"existing podcast uses actual parent/name and still invokes sender validation");
    check(m.brands==1 && m.brand_target==77 && m.finalizes==1,"podcast artwork targets show, not episode, then finalizes");
    reset(); m.absent=1;
    check(run(example_podcast_send,pod)==0 && m.folders==1 && m.forges==1 && m.finds==2 && m.sends==1
        && m.sent_parent==66 && !strcmp(m.sent_name,"Live device show"),"forge reuse re-looks up actual parent instead of requested folder");
    reset(); m.lookup_fail=1;
    check(run(example_podcast_send,pod)==1 && !writes() && m.severs==1,"failed podcast lookup never justifies creation");
    reset(); m.absent=1; m.relookup_fail=1;
    check(run(example_podcast_send,pod)==1 && m.finds==2 && !m.sends && !m.brands && !m.finalizes && m.severs==1,
          "failed post-forge lookup keeps retained objects and prevents episode send");
    reset(); m.inventory_fail=1;
    check(run(example_podcast_send,pod)==1 && !m.sends && m.severs==1,"failed live show inventory prevents episode send");
    reset(); m.missing_show=1;
    check(run(example_podcast_send,pod)==1 && !m.sends && m.allocations==m.frees && m.severs==1,
          "stale show ID cannot borrow a same-feed show's name to send an episode");
    for (int forge=0; forge<2; ++forge) for (int status=-2; status<=-1; ++status) for (int nonzero=0; nonzero<2; ++nonzero) {
        if (status==-2 && nonzero) continue; /* cleaned contract returns no orphan ID */
        reset(); m.absent=forge; m.forge_status=forge?status:0; m.send_status=forge?0:status; m.failure_id=nonzero?123:0;
        check(run(example_podcast_send,pod)==1 && m.severs==1 && !m.brands && !m.finalizes
            && (forge?m.forges==1 && !m.sends:m.sends==1) && m.allocations==m.frees,
              "podcast failure returns promptly without retry, cover or finalize");
        check(strstr(output,"fixture-serial") && (status==-2?strstr(output,"deletion confirmed"):strstr(output,"uncertain"))
            && (!nonzero || strstr(output,"=123")),"podcast distinguishes confirmed cleanup from uncertain zero/nonzero identity");
    }
    reset(); check(run(example_audiobook_send,book)==0 && m.finds==1 && m.sends==1 && !strcmp(m.sent_key,key)
        && m.sent_number==2 && !strcmp(m.sent_title,"Chapter") && m.severs==1,
        "known audiobook identity still invokes sender validation with stable key/order");
    check(m.brands==1 && m.brand_target==99 && m.finalizes==1,"audiobook cover targets actual part, not parent folder");
    reset(); m.lookup_fail=1;
    check(run(example_audiobook_send,book)==1 && !writes() && m.severs==1,"audiobook lookup failure prevents writes");
    for (int nonzero=0; nonzero<2; ++nonzero) {
        reset(); m.send_status=-1; m.failure_id=nonzero?123:0;
        check(run(example_audiobook_send,book)==1 && m.sends==1 && !m.brands && !m.finalizes && m.severs==1
            && strstr(output,"fixture-serial") && strstr(output,nonzero?"part=123":"part=0")
            && strstr(output,"zero ID does not prove cleanup"),"audiobook failure retains exact ID, including uncertain zero, without retry/delete");
    }
    for (int kind=0; kind<2; ++kind) {
        Entry entry=kind?example_audiobook_send:example_podcast_send; char **args=kind?book:pod;
        reset(); m.brand_status=-1;
        check(run(entry,args)==1 && m.sends==1 && m.brands==1 && m.finalizes==1 && m.severs==1 && strstr(output,"retained"),
              "cover failure reports retained audio and still requests finalization");
        reset(); m.finalize_status=-1;
        check(run(entry,args)==1 && m.sends==1 && m.finalizes==1 && m.severs==1 && strstr(output,"finalization failed"),
              "finalization failure reports retained audio without retry");
    }
    printf("%d example contract checks passed; mock public API only, no USB\n",checks);
    return 0;
}
