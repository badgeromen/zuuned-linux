#include "../src/zune_internal.h"
#include <assert.h>
static ZuneDevice *connect_or_die(void) { abort(); }
#include "../tools/hd_art_binding_cli.h"
int main(void) {
    char dir[]="/tmp/hd-art-journal.XXXXXX",path[128],linkpath[128];assert(mkdtemp(dir));
    snprintf(path,sizeof(path),"%s/recovery",dir);snprintf(linkpath,sizeof(linkpath),"%s/link",dir);
    HdArtistBindingSnapshot s={0};s.version=HD_ART_BIND_VERSION;s.handle=7;s.storage_id=1;
    s.format=MTP_OFC_AbstractArtist;s.name_len=s.filename_len=5;
    const uint8_t name[]={2,'A',0,0,0};memcpy(s.name_wire,name,5);memcpy(s.filename_wire,name,5);
    s.persistent_id[0]=1;strcpy(s.serial,"fixture-device");assert(hd_bind_valid_snapshot(&s));
    assert(!hd_art_journal_save(path,&s));struct stat st;assert(!stat(path,&st));assert((st.st_mode&0777)==0600);
    HdArtJournal j;assert(!hd_art_journal_load(path,&j));assert(!memcmp(&j.snapshot,&s,sizeof(s)));
    assert(hd_art_journal_save(path,&s)<0);assert(!symlink(path,linkpath));
    assert(hd_art_journal_load(linkpath,&j)<0);assert(hd_art_journal_save(linkpath,&s)<0);
    assert(!chmod(path,0644));assert(hd_art_journal_load(path,&j)<0);assert(!chmod(path,0600));
    int fd=open(path,O_WRONLY);assert(fd>=0);assert(pwrite(fd,"!",1,0)==1);close(fd);
    assert(hd_art_journal_load(path,&j)<0);
    fd=open(path,O_WRONLY);assert(fd>=0);assert(!ftruncate(fd,1));close(fd);assert(hd_art_journal_load(path,&j)<0);
    assert(!unlink(linkpath));assert(!mkfifo(linkpath,0600));assert(hd_art_journal_load(linkpath,&j)<0);
    assert(!unlink(linkpath));assert(!unlink(path));assert(!rmdir(dir));
    puts("HD artwork journal: durable roundtrip, permissions, overwrite/symlink/corruption/truncation checks passed; no USB");
}
