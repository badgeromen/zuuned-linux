/* Real ptp_transaction with scripted USB reads. No device or auth data. */
#include "ptp.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
static unsigned char packets[4][128];
static unsigned sizes[4], total, cursor, writes, checks;
static void put16(unsigned char *p, unsigned v) { p[0]=v; p[1]=v>>8; }
static void put32(unsigned char *p, unsigned v) { put16(p,v); put16(p+2,v>>16); }
static void packet(unsigned i, unsigned type, unsigned code, unsigned tx, unsigned n) {
    put32(packets[i],n); put16(packets[i]+4,type); put16(packets[i]+6,code);
    put32(packets[i]+8,tx); sizes[i]=n; if (total<=i) total=i+1;
}
static int read_usb(zune_usb_handle_t *h, uint8_t ep, uint8_t *p, uint32_t n,
                    uint32_t *actual, uint32_t timeout) {
    (void)h;(void)ep; assert(timeout>0); assert(cursor<total);
    assert(sizes[cursor]<=n); *actual=sizes[cursor];
    memcpy(p,packets[cursor],*actual); ++cursor; return 0;
}
static int write_usb(zune_usb_handle_t *h, uint8_t ep, const uint8_t *p, uint32_t n,
                    uint32_t *actual, uint32_t timeout) {
    (void)h;(void)ep; (void)p; assert(timeout>0); ++writes; *actual=n; return 0;
}
static zune_usb_backend_t backend={.bulk_read=read_usb,.bulk_write=write_usb};
static zune_usb_handle_t usb={.backend=&backend,.bulk_in_ep=0x81,.bulk_out_ep=1,.bulk_out_maxpacket=512};
static void reset(void) { memset(packets,0,sizeof(packets)); memset(sizes,0,sizeof(sizes)); total=cursor=writes=0; }
static void run(unsigned flags, int expected, unsigned nparams, unsigned datalen, unsigned code) {
    ptp_session_t s={.usb=&usb};
    uint8_t *data=NULL; uint32_t len=999,params[5]={0}; int count=99; uint16_t response=99;
    int rc=ptp_transaction(&s,0x922d,NULL,0,flags,NULL,0,&data,&len,&response,params,&count);
    assert((rc==0)==expected); assert(count==(int)nparams); assert(len==datalen);
    assert(response==code); if (nparams==3) assert(params[0]==0&&params[1]==3&&params[2]==0);
    assert(writes==((flags & PTP_DP_SENDDATA) ? 2u : 1u)); if (expected) assert(cursor==total);
    if (!expected && !datalen && flags==PTP_DP_GETDATA_OPTIONAL)
        assert(!s.split_header_data); /* rejected headers cannot mutate framing */
    if (datalen) assert(data&&data[0]==0x7e); else assert(!data);
    free(data); ++checks;
}
static void idle(void) { packet(0,3,0x2001,1,24); put32(packets[0]+16,3); }
int main(void) {
    reset(); packet(0,3,0x2001,1,12); run(PTP_DP_NODATA,1,0,0,0x2001);
    reset(); packet(0,3,0x2005,1,12); run(PTP_DP_NODATA,0,0,0,0x2005);
    reset(); packet(0,3,0x2001,1,12); run(PTP_DP_SENDDATA,1,0,0,0x2001);
    reset(); packet(0,3,0x2005,1,12); run(PTP_DP_SENDDATA,0,0,0,0x2005);
    reset(); idle(); run(PTP_DP_GETDATA_OPTIONAL,1,3,0,0x2001);
    reset(); idle(); run(PTP_DP_GETDATA,0,0,0,0x2001); /* mandatory DATA unchanged */
    reset(); idle(); put16(packets[0]+6,0x2005); run(PTP_DP_GETDATA_OPTIONAL,0,3,0,0x2005);
    reset(); idle(); memcpy(packets[1],packets[0]+12,12); sizes[0]=12;sizes[1]=12;total=2;
    run(PTP_DP_GETDATA_OPTIONAL,1,3,0,0x2001);
    reset(); packet(0,2,0x922d,1,15); packets[0][12]=0x7e;
    packet(1,3,0x2001,1,12); run(PTP_DP_GETDATA_OPTIONAL,1,0,3,0x2001);
    reset(); packet(0,2,0x922d,1,15); sizes[0]=12; packets[1][0]=0x7e;sizes[1]=3;total=2;
    packet(2,3,0x2001,1,12); run(PTP_DP_GETDATA_OPTIONAL,1,0,3,0x2001);
    reset(); packet(0,2,0x922d,1,15); packets[0][12]=0x7e;
    packet(1,3,0x2001,1,12); run(PTP_DP_GETDATA,1,0,3,0x2001);
    reset(); packet(0,2,0x922d,1,15); packets[0][12]=0x7e;
    sizes[1]=0; packet(2,3,0x2001,1,12); /* device ZLP before response */
    run(PTP_DP_GETDATA_OPTIONAL,1,0,3,0x2001);
    for (int fault=0; fault<7; ++fault) {
        reset();idle();
        if(fault==0) put32(packets[0]+8,2); /* stale tx */
        if(fault==1) put32(packets[0],23); /* unaligned */
        if(fault==2) put32(packets[0],36); /* too many params */
        if(fault==3) sizes[0]=20;          /* truncated */
        if(fault==4) put32(packets[0],8);  /* undersized */
        if(fault==5) put16(packets[0]+4,4); /* event, not response */
        if(fault==6) {put32(packets[0],20);sizes[0]=24;} /* trailing */
        run(PTP_DP_GETDATA_OPTIONAL,0,0,0,0);
    }
    for(int fault=0;fault<3;++fault) {
        reset();packet(0,2,0x922d,1,12);
        if(fault==0)put16(packets[0]+6,0x922c);
        if(fault==1)put32(packets[0]+8,2);
        if(fault==2)put32(packets[0],65549); /* reject before allocation */
        run(PTP_DP_GETDATA_OPTIONAL,0,0,0,0);
    }
    reset();packet(0,2,0x922d,1,15);packets[0][12]=0x7e;
    packet(1,3,0x2001,2,12);run(PTP_DP_GETDATA_OPTIONAL,0,0,3,PTP_ERROR_IO);
    reset();packet(0,2,0x922d,1,15);packets[0][12]=0x7e;
    packet(1,3,0x2001,1,24);sizes[1]=12;run(PTP_DP_GETDATA_OPTIONAL,0,0,3,PTP_ERROR_IO);
    /* A wrapper whose response fails after a complete data phase hands back
     * no buffer: its callers return without freeing one (LeakSanitizer
     * reports the old behaviour). */
    reset();packet(0,2,PTP_OC_GetDevicePropValue,1,15);packets[0][12]=0x7e;
    packet(1,3,0x2002,1,12);
    {
        ptp_session_t s={.usb=&usb};
        uint8_t *data=(uint8_t *)&s; uint32_t len=99;
        int rc=ptp_get_device_prop_value(&s,0x5001,&data,&len);
        if (!(rc<0 && data==NULL && len==0))
            fprintf(stderr,"rc=%d data=%p len=%u last=0x%04x\n",rc,(void *)data,len,s.last_response);
        assert(rc<0 && data==NULL && len==0);
        ++checks;
    }
    printf("Optional PTP data: %u USB-scripted cases passed\n",checks);
}
