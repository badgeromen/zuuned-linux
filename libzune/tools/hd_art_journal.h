/* Shared private research journal and exact experimental identity. */
#ifndef ZUNETOOL_HD_ART_JOURNAL_H
#define ZUNETOOL_HD_ART_JOURNAL_H
#include "hd_artist_binding.h"
#include <fcntl.h>
#include <errno.h>
/* Reserved for this ONE artist experiment; never use for bulk assignment. */
static const char hd_art_test_guid[]="7c516f70-fb43-4d54-9cd3-5a4f6cf07890";
/* Windows GUID wire order: first three components little endian. */
static const uint8_t hd_art_test_guid_wire[16]={0x70,0x6f,0x51,0x7c,0x43,0xfb,0x54,0x4d,0x9c,0xd3,0x5a,0x4f,0x6c,0xf0,0x78,0x90};
typedef struct {
    char magic[8];
    uint32_t snapshot_bytes;
    uint8_t guid[16];
    HdArtistBindingSnapshot snapshot;
} HdArtJournal;
static int hd_art_journal_save(const char *path,const HdArtistBindingSnapshot *snapshot) {
    HdArtJournal journal={0};memcpy(journal.magic,"ZUHDB1",6);
    journal.snapshot_bytes=sizeof(journal.snapshot);journal.snapshot=*snapshot;
    memcpy(journal.guid,hd_art_test_guid_wire,16);
    int fd=open(path,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);if(fd<0)return -1;
    const uint8_t *p=(const uint8_t*)&journal;size_t written=0;int ok=1;
    while(written<sizeof(journal)) {
        ssize_t n=write(fd,p+written,sizeof(journal)-written);
        if(n<0 && errno==EINTR)continue;
        if(n<=0){ok=0;break;}written+=(size_t)n;
    }
    if(fsync(fd))ok=0;
    if(close(fd))ok=0;
    char *dir=strdup(path);if(!dir)return -1;
    char *slash=strrchr(dir,'/');
    if(slash) {if(slash==dir)slash[1]=0;else *slash=0;}else {free(dir);dir=strdup(".");}
    if(!dir)return -1;
    fd=open(dir,O_RDONLY|O_DIRECTORY);free(dir);
    if(fd<0)return -1;
    if(fsync(fd))ok=0;
    if(close(fd))ok=0;
    return ok?0:-1;
}
static int hd_art_journal_load(const char *path,HdArtJournal *journal) {
    int fd=open(path,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);struct stat st;
    if(fd<0)return -1;
    int ok=!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_uid==geteuid()&&
        !(st.st_mode&0077)&&st.st_size==(off_t)sizeof(*journal);
    size_t got=0;
    while(ok&&got<sizeof(*journal)) {
        ssize_t n=read(fd,(uint8_t*)journal+got,sizeof(*journal)-got);
        if(n<0&&errno==EINTR)continue;
        if(n<=0){ok=0;break;}got+=(size_t)n;
    }
    close(fd);
    return ok&&!memcmp(journal->magic,"ZUHDB1\0\0",8)&&
        journal->snapshot_bytes==sizeof(journal->snapshot)&&
        !memcmp(journal->guid,hd_art_test_guid_wire,16)&&
        hd_bind_valid_snapshot(&journal->snapshot)?0:-1;
}
static const char *hd_art_binding_result(HdArtistBindingResult result) {
    switch(result) {
        case HD_ART_BIND_CONFIRMED:return "confirmed";
        case HD_ART_BIND_REFUSED:return "refused-and-unchanged";
        case HD_ART_BIND_UNCERTAIN:return "uncertain; keep recovery journal";
        default:return "rejected-before-write";
    }
}
#endif
