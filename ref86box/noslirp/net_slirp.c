/*
 * 86Box    A hypervisor and IBM PC system emulator.
 *
 *          Stub replacement for src/network/net_slirp.c.
 *
 *          NOT PART OF UPSTREAM 86Box. Used by the AWE32Emu project only to
 *          avoid pulling in libslirp (and through it glib) for a build whose
 *          sole purpose is capturing EMU8000 register writes. Networking is
 *          never enabled in ref86box/vm/*.cfg, so net_slirp_drv is never
 *          selected and none of these functions can run.
 *
 *          Nothing here touches sound. Apply with ref86box/apply_noslirp.sh
 *          and revert with `git checkout src/network` in the 86Box clone.
 */
#include <stdint.h>
#include <stdio.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/plat.h>
#include <86box/network.h>
#include <86box/plat_unused.h>

static void
net_slirp_in_available(UNUSED(void *priv))
{
    /* unreachable: SLiRP networking is not built in */
}

static void *
net_slirp_init(UNUSED(const netcard_t *card), UNUSED(const uint8_t *mac_addr),
               UNUSED(void *priv), char *netdrv_errbuf)
{
    if (netdrv_errbuf != NULL)
        snprintf(netdrv_errbuf, NET_DRV_ERRBUF_SIZE,
                 "SLiRP support was left out of this build");
    return NULL;
}

static void
net_slirp_close(UNUSED(void *priv))
{
    /* unreachable: SLiRP networking is not built in */
}

const netdrv_t net_slirp_drv = {
    .notify_in = &net_slirp_in_available,
    .init      = &net_slirp_init,
    .close     = &net_slirp_close,
    .priv      = NULL
};
