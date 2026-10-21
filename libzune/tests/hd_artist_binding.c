/* Experimental DA97 binding: strict transaction allowlist, no USB. */
#include "../tools/hd_artist_binding.h"
#include <assert.h>
static ZuneDevice device;
static HdArtistBindingSnapshot live;
static unsigned checks, calls, writes, read_fail_at;
static unsigned descriptor_mode, malformed_prop, malformed_mode, cancel_at;
static unsigned write_mode, post_write_change;
static uint8_t next_guid[16];
static void put16(uint8_t *p, unsigned v) { p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8); }
static void put32(uint8_t *p, uint32_t v) { put16(p,v);put16(p+2,v>>16); }
static void wire_ascii(uint8_t *p, uint16_t *n, const char *text) {
    size_t count=strlen(text); assert(count<=254); *n=(uint16_t)(3+2*count);p[0]=(uint8_t)(count+1);
    for(size_t i=0;i<count;++i) put16(p+1+2*i,(unsigned char)text[i]);
    put16(p+1+2*count,0);
}
static void check(int ok,const char *label) {
    if(!ok){fprintf(stderr,"FAIL %s\n",label);exit(1);}++checks;
}
static void reset(void) {
    memset(&device,0,sizeof(device));memset(&live,0,sizeof(live));
    device.device_family=ZUNE_FAMILY_PAVO;device.mtpz_authenticated=1;
    device.storage_id=0x10001;device.serial="TEST-DEVICE-A";
    live.version=1;live.handle=7;live.storage_id=device.storage_id;live.format=0xb218;
    strcpy(live.serial,device.serial);wire_ascii(live.name_wire,&live.name_len,"Artist");
    wire_ascii(live.filename_wire,&live.filename_len,"Artist.art");
    live.persistent_id[0]=0x51;live.da99[0]=0x72; /* preserved, not necessarily zero */
    memset(next_guid,0x42,16);calls=writes=read_fail_at=descriptor_mode=malformed_prop=malformed_mode=cancel_at=write_mode=post_write_change=0;
}
int ptp_transaction(ptp_session_t *session,uint16_t op,uint32_t *params,int count,
                    uint16_t flags,uint8_t *send,uint64_t send_len,uint8_t **data,uint32_t *len,
                    uint16_t *response,uint32_t *response_params,int *response_count) {
    assert(session==&device.ptp && count==2 && params && !response_params && !response_count);
    ++calls;*response=session->last_response=0x2001;
    if(op==MTP_OC_SetObjectPropValue) {
        assert(flags==PTP_DP_SENDDATA&&params[0]==7&&params[1]==0xda97&&send&&send_len==16&&!data&&!len);
        ++writes;
        if(write_mode==1){*response=session->last_response=0x2005;return -1;}
        if(write_mode==2){*response=session->last_response=PTP_ERROR_IO;return -1;}
        if(write_mode!=4)memcpy(live.original_da97,send,16);
        if(write_mode==3){*response=session->last_response=0x2002;return -1;}
        if(write_mode==5){*response=session->last_response=0x2005;return -1;}
        if(write_mode==6){read_fail_at=calls+1;return 0;}
        if(write_mode==7){device.cancel_requested=1;return 0;}
        if(write_mode==8){device.serial="TEST-DEVICE-B";return 0;}
        if(write_mode==9){read_fail_at=calls+1;*response=session->last_response=0x2005;return -1;}
        switch(post_write_change) {
        case 1:live.name_wire[1]^=1;break;
        case 2:live.filename_wire[1]^=1;break;
        case 3:live.persistent_id[0]^=1;break;
        case 4:live.da99[0]^=1;break;
        case 5:live.storage_id++;break;
        case 6:live.format=0x3009;break;
        default:break;
        }
        return 0;
    }
    assert(flags==PTP_DP_GETDATA&&!send&&!send_len&&data&&len);
    *data=NULL;*len=0;
    if(calls==read_fail_at){*response=session->last_response=PTP_ERROR_IO;return -1;}
    uint8_t value[1024]={0};uint32_t size=0;
    if(op==MTP_OC_GetObjectPropDesc) {
        assert(params[0]==0xda97&&params[1]==0xb218);
        put16(value,0xda97);put16(value+2,0xa);value[4]=1;size=26;
        if(descriptor_mode==1)value[0]^=1;
        if(descriptor_mode==2)put16(value+2,6);
        if(descriptor_mode==3)value[4]=0;
        if(descriptor_mode==4)value[25]=1;
        if(descriptor_mode==5)size=25;
        if(descriptor_mode==6)size=27;
    } else {
        assert(op==MTP_OC_GetObjectPropValue&&params[0]==7);
        switch(params[1]) {
        case 0xdc02:put16(value,live.format);size=2;break;
        case 0xdc01:put32(value,live.storage_id);size=4;break;
        case 0xdc44:memcpy(value,live.name_wire,live.name_len);size=live.name_len;break;
        case 0xdc07:memcpy(value,live.filename_wire,live.filename_len);size=live.filename_len;break;
        case 0xdc41:memcpy(value,live.persistent_id,16);size=16;break;
        case 0xda99:memcpy(value,live.da99,16);size=16;break;
        case 0xda97:memcpy(value,live.original_da97,16);size=16;break;
        default:assert(!"nonallowlisted property");
        }
        if(params[1]==malformed_prop) {
            if(malformed_mode==1)--size;
            if(malformed_mode==2)++size;
            if(malformed_mode==3){value[0]=255;size=5;}
            if(malformed_mode==4)put16(value+size-2,1);
            if(malformed_mode==5)put16(value+1,0xd800);
            if(malformed_mode==6)put16(value+1,0);
        }
    }
    *data=malloc(size);assert(*data);memcpy(*data,value,size);*len=size;
    if(calls==cancel_at)device.cancel_requested=1;
    return 0;
}
static HdArtistBindingSnapshot snapshot(void) {
    HdArtistBindingSnapshot s;
    check(hd_artist_binding_snapshot(&device,7,"Artist",&s)==HD_ART_BIND_CONFIRMED,"snapshot verified");
    check(calls==8&&!writes,"snapshot only eight bounded reads");return s;
}
static void rejected_snapshot(const char *name,const char *label) {
    HdArtistBindingSnapshot s;memset(&s,0xaa,sizeof(s));
    check(hd_artist_binding_snapshot(&device,7,name,&s)==HD_ART_BIND_REJECTED,label);
    HdArtistBindingSnapshot zero={0};check(!memcmp(&s,&zero,sizeof(s))&&!writes,"rejected snapshot erased and never writes");
}
static int guard_before_write(void *context) {
    unsigned limit=*(unsigned*)context;return calls<limit;
}
int main(void) {
    reset();HdArtistBindingSnapshot s=snapshot();
    check(!memcmp(s.da99,live.da99,16)&&!memcmp(s.persistent_id,live.persistent_id,16),"snapshot retains DA99 and persistent ID");
    check(hd_artist_binding_apply(&device,&s,next_guid)==HD_ART_BIND_CONFIRMED&&writes==1,"one verified in-place apply");
    check(!memcmp(live.original_da97,next_guid,16)&&!memcmp(live.da99,s.da99,16),"only routing property changed");
    check(hd_artist_binding_apply(&device,&s,next_guid)==HD_ART_BIND_REJECTED&&writes==1,"repeat apply never retries write");
    check(hd_artist_binding_restore(&device,&s,next_guid)==HD_ART_BIND_CONFIRMED&&writes==2,"conditional restore verified");
    check(!hd_bind_nonzero(live.original_da97),"restore writes exact original zero");
    check(hd_artist_binding_restore(&device,&s,next_guid)==HD_ART_BIND_REJECTED&&writes==2,"repeat restore does not write");
    for(unsigned f=1;f<=6;++f){reset();descriptor_mode=f;rejected_snapshot("Artist","invalid descriptor fails closed");}
    const unsigned props[]={0xdc02,0xdc01,0xdc44,0xdc07,0xdc41,0xda99,0xda97};
    for(unsigned i=0;i<sizeof(props)/sizeof(*props);++i)for(unsigned f=1;f<=2;++f){
        reset();malformed_prop=props[i];malformed_mode=f;rejected_snapshot("Artist","wrong property length rejected");
    }
    for(unsigned prop=0;prop<2;++prop)for(unsigned f=3;f<=6;++f){
        reset();malformed_prop=prop?0xdc07:0xdc44;malformed_mode=f;rejected_snapshot("Artist","malformed UTF16 rejected");
    }
    for(unsigned f=1;f<=8;++f){reset();read_fail_at=f;rejected_snapshot("Artist","every snapshot read failure stops");}
    for(unsigned f=1;f<=8;++f){reset();cancel_at=f;rejected_snapshot("Artist","snapshot cancellation stops");}
    reset();rejected_snapshot("artist","name case must match exactly");
    reset();rejected_snapshot("Artist ","name whitespace must match exactly");
    reset();rejected_snapshot("","empty expected name rejected");
    reset();rejected_snapshot(NULL,"missing expected name rejected");
    reset();live.original_da97[0]=1;rejected_snapshot("Artist","existing nonzero binding preserved");
    reset();memset(live.persistent_id,0,16);rejected_snapshot("Artist","missing persistent identity refused");
    reset();live.format=0x3009;rejected_snapshot("Artist","wrong object category rejected");
    reset();live.storage_id++;rejected_snapshot("Artist","wrong object storage rejected");
    reset();device.device_family=ZUNE_FAMILY_DRACO;rejected_snapshot("Artist","non HD rejected");check(!calls,"family check precedes reads");
    reset();device.mtpz_authenticated=0;rejected_snapshot("Artist","unauthenticated rejected");check(!calls,"auth check precedes reads");
    reset();device.serial="";rejected_snapshot("Artist","empty serial rejected");check(!calls,"missing device identity precedes reads");
    reset();device.disconnecting=1;rejected_snapshot("Artist","disconnect rejected");check(!calls,"disconnect precedes reads");
    for(unsigned f=1;f<=9;++f) {
        reset();s=snapshot();
        switch(f){case 1:live.name_wire[1]^=1;break;case 2:live.filename_wire[1]^=1;break;
        case 3:live.persistent_id[0]^=1;break;case 4:live.da99[0]^=1;break;
        case 5:live.storage_id++;break;case 6:live.format=0x3009;break;
        case 7:device.serial="TEST-DEVICE-B";break;case 8:live.original_da97[0]=1;break;
        case 9:device.storage_id++;break;}
        check(hd_artist_binding_apply(&device,&s,next_guid)==HD_ART_BIND_REJECTED&&!writes,"stale preflight never writes");
    }
    for(unsigned f=1;f<=8;++f){reset();s=snapshot();read_fail_at=calls+f;
        check(hd_artist_binding_apply(&device,&s,next_guid)==HD_ART_BIND_REJECTED&&!writes,"preflight read failure never writes");}
    for(unsigned f=1;f<=8;++f){reset();s=snapshot();cancel_at=calls+f;
        check(hd_artist_binding_apply(&device,&s,next_guid)==HD_ART_BIND_REJECTED&&!writes,"preflight cancellation never writes");}
    for(unsigned f=1;f<=9;++f){reset();s=snapshot();write_mode=f;
        HdArtistBindingResult expected=f==1?HD_ART_BIND_REFUSED:HD_ART_BIND_UNCERTAIN;
        check(hd_artist_binding_apply(&device,&s,next_guid)==expected&&writes==1,"write failure/no effect/readback failure classified without retry");}
    for(unsigned f=1;f<=6;++f){reset();s=snapshot();post_write_change=f;
        check(hd_artist_binding_apply(&device,&s,next_guid)==HD_ART_BIND_UNCERTAIN&&writes==1,"postwrite identity drift remains uncertain");}
    for(unsigned f=1;f<=9;++f){reset();s=snapshot();memcpy(live.original_da97,next_guid,16);
        switch(f){case 1:live.name_wire[1]^=1;break;case 2:live.filename_wire[1]^=1;break;
        case 3:live.persistent_id[0]^=1;break;case 4:live.da99[0]^=1;break;
        case 5:live.storage_id++;break;case 6:live.format=0x3009;break;
        case 7:device.serial="TEST-DEVICE-B";break;case 8:live.original_da97[0]^=1;break;case 9:descriptor_mode=3;break;}
        check(hd_artist_binding_restore(&device,&s,next_guid)==HD_ART_BIND_REJECTED&&!writes,"restore refuses changed identity/current binding");}
    reset();s=snapshot();uint8_t zero_guid[16]={0};
    check(hd_artist_binding_apply(&device,&s,zero_guid)==HD_ART_BIND_REJECTED&&!writes,"zero routing GUID rejected");
    check(hd_artist_binding_apply(&device,&s,NULL)==HD_ART_BIND_REJECTED&&!writes,"missing GUID rejected");
    check(hd_artist_binding_restore(&device,&s,zero_guid)==HD_ART_BIND_REJECTED&&!writes,"zero restore GUID rejected");
    s.version=2;check(hd_artist_binding_apply(&device,&s,next_guid)==HD_ART_BIND_REJECTED&&!writes,"unknown snapshot version rejected");
    reset();s=snapshot();s.name_len=512;check(hd_artist_binding_apply(&device,&s,next_guid)==HD_ART_BIND_REJECTED&&!writes,"corrupt saved wire length rejected");
    reset();s=snapshot();memset(s.serial,'x',sizeof(s.serial));check(hd_artist_binding_apply(&device,&s,next_guid)==HD_ART_BIND_REJECTED&&!writes,"unterminated saved serial rejected");
    reset();s=snapshot();s.original_da97[0]=1;check(hd_artist_binding_restore(&device,&s,next_guid)==HD_ART_BIND_REJECTED&&!writes,"nonzero original snapshot rejected");
    reset();put16(live.name_wire+1,0xd83d);put16(live.name_wire+3,0xde80);put16(live.name_wire+5,0);live.name_wire[0]=3;live.name_len=7;
    check(hd_artist_binding_snapshot(&device,7,"\xf0\x9f\x9a\x80",&s)==HD_ART_BIND_CONFIRMED,"supplementary Unicode exact name accepted");
    reset();char long_name[255];memset(long_name,'A',254);long_name[254]=0;
    wire_ascii(live.name_wire,&live.name_len,long_name);
    check(hd_artist_binding_snapshot(&device,7,long_name,&s)==HD_ART_BIND_CONFIRMED,"maximum MTP name length accepted");
    for(unsigned allowed_reads=0;allowed_reads<=8;++allowed_reads){
        reset();s=snapshot();unsigned limit=calls+allowed_reads;
        check(hd_artist_binding_apply_guarded(&device,&s,next_guid,guard_before_write,&limit)==HD_ART_BIND_REJECTED&&!writes,"signal/deadline guard rechecked after identity reads before actual setter");
    }
    printf("HD artist binding: %u checks passed; no USB, only DA97 setter allowed\n",checks);
}
