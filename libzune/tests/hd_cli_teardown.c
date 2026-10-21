#include "../tools/hd_cli_teardown.h"
#include <assert.h>
static int normal_closes, usb_closes, checks;
static ZuneDBLibrary borrowed;
static int close_usb(zune_usb_handle_t *usb) {
    assert(usb->backend_data);
    free(usb->backend_data); usb->backend_data=NULL;
    ++usb_closes;return 0;
}
static const zune_usb_backend_t backend={.close=close_usb};
void zune_sever(ZuneDevice *dev) {
    ++normal_closes;
    close_usb(&dev->usb);
    free(dev->name);free(dev->model);free(dev->serial);free(dev->storage_desc);free(dev);
}
static ZuneDevice *device(void) {
    ZuneDevice *dev=calloc(1,sizeof(*dev));assert(dev);
    dev->name=strdup("name");dev->model=strdup("model");dev->serial=strdup("private");dev->storage_desc=strdup("storage");
    dev->usb.backend=&backend;dev->usb.backend_data=malloc(32);assert(dev->usb.backend_data);
    dev->cached_library=&borrowed;return dev;
}
static void check(int ok){assert(ok);++checks;}
int main(void) {
    hd_cli_sever(NULL,0);hd_cli_sever(NULL,1);check(!normal_closes&&!usb_closes);
    hd_cli_sever(device(),0);check(normal_closes==1&&usb_closes==1);
    hd_cli_sever(device(),1);check(normal_closes==1&&usb_closes==2);
    ZuneDevice *dev=device();close_usb(&dev->usb);dev->usb.backend=NULL;
    hd_cli_sever(dev,1);check(normal_closes==1&&usb_closes==3);
    check(!hd_cli_transaction_uncertain(0,PTP_RC_OK));
    check(!hd_cli_transaction_uncertain(-1,0x2005));
    check(!hd_cli_transaction_uncertain(-1,0xa801));
    check(hd_cli_transaction_uncertain(-1,PTP_RC_OK));
    check(hd_cli_transaction_uncertain(-1,0));
    check(hd_cli_transaction_uncertain(-1,PTP_ERROR_IO));
    check(hd_cli_transaction_uncertain(-1,PTP_ERROR_TIMEOUT));
    check(hd_cli_transaction_uncertain(-1,PTP_ERROR_CANCEL));
    printf("HD CLI teardown: %d checks passed; no USB\n",checks);
    return 0;
}
