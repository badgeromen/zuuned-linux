/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "common.h"
int main(int argc, char **argv)
{
    if (argc > 1) {
        puts("Usage: device_info\nConnect and read basic information; does not prove successful authentication.");
        return argc == 2 && !strcmp(argv[1], "--help") ? 0 : 2;
    }
    ZuneDeviceHandle dev = zune_breach();
    if (!dev) return failure(NULL, "Connect");
    printf("Name: %s\nModel: %s\nBattery: %u%%\n", text(zune_get_name(dev)),
           text(zune_get_model(dev)), (unsigned)zune_get_battery(dev));
    zune_sever(dev);
    return 0;
}
