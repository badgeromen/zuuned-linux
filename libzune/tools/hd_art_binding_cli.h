/* Private one-artist experiment. Durable rollback precedes every DA97 write.
 * Native-ABI journal is local research evidence, not a portable file format. */
#ifndef ZUNETOOL_HD_ART_BINDING_CLI_H
#define ZUNETOOL_HD_ART_BINDING_CLI_H
#include "hd_art_journal.h"
static int cmd_hd_art_bind(int argc,char **argv) {
    if(argc!=5){fprintf(stderr,"usage: zunetool hd-art-bind <handle> <exact-name> <new-backup-file>\n");return 2;}
    char *end=NULL;errno=0;
    unsigned long raw=strtoul(argv[2],&end,10);
    if(errno||!end||*end||argv[2][0]<'1'||argv[2][0]>'9'||!raw||raw>=UINT32_MAX)return 2;
    ZuneDeviceHandle dev=connect_or_die();HdArtistBindingSnapshot snapshot;
    HdArtistBindingResult result=hd_artist_binding_snapshot(dev,(uint32_t)raw,argv[3],&snapshot);
    if(result==HD_ART_BIND_CONFIRMED) {
        if(hd_art_journal_save(argv[4],&snapshot)) {
            fprintf(stderr,"Could not persist exclusive recovery journal. No write attempted.\n");
            zune_sever(dev);return 1;
        }
        result=hd_artist_binding_apply(dev,&snapshot,hd_art_test_guid_wire);
    }
    printf("artist-binding\t%s\nresponse\t0x%04X\nrouting-guid\t%s\n",
           hd_art_binding_result(result),dev->ptp.last_response,hd_art_test_guid);
    zune_sever(dev);return result==HD_ART_BIND_CONFIRMED?0:1;
}
static int cmd_hd_art_restore(int argc,char **argv) {
    HdArtJournal journal;
    if(argc!=3||hd_art_journal_load(argv[2],&journal)) {
        fprintf(stderr,"usage: zunetool hd-art-restore <valid-local-backup-file>\n");return 2;
    }
    ZuneDeviceHandle dev=connect_or_die();
    HdArtistBindingResult result=hd_artist_binding_restore(dev,&journal.snapshot,journal.guid);
    printf("artist-binding-restore\t%s\nresponse\t0x%04X\n",hd_art_binding_result(result),dev->ptp.last_response);
    zune_sever(dev);return result==HD_ART_BIND_CONFIRMED?0:1;
}
#endif
