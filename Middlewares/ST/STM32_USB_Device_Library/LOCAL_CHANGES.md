# STM32 USB Device Library — local changes

SOUP item: STMicroelectronics `stm32-mw-usb-device`
(https://github.com/STMicroelectronics/stm32-mw-usb-device).

Upstream version in this tree: **v2.11.6** (`Core/` and `Class/CDC/`; only the CDC
class is built). This is the version pinned by STM32CubeF0 v1.12.0.

Updated from v2.5.3 on 2026-11-16. v2.5.3 passed the host's `wLength` unbounded in
`USBD_CDC_Setup`: OUT class requests were received into the 512-byte `hcdc->data`
buffer, and IN class requests sent `wLength` bytes from it. Upstream fixed this in
v2.8.0 ("Fix USB buffer overflow vulnerability for CDC, ..."); v2.11.6 bounds both
directions (`USB_MAX_EP0_SIZE`, `CDC_REQ_MAX_DATA_SIZE`). It also brings the
`usbd_ctlreq.c` fixes:

- `USBD_StdEPReq` checks the endpoint index against 15. With the CDC endpoints in use,
  a GET_STATUS to endpoint 0x91 passed the `is_used` check for EP1 and then indexed
  `ep_in[0x11]`.
- `USBD_GetString` is bounded by the destination length.

Every file is byte-identical to upstream v2.11.6; there are no local changes in this
directory. (The `TransmitCplt` callback that v2.5.3 needed as a local change is
upstream in v2.11.6.) Verify with a `diff` against the upstream tag before recording a
new version here. The generator `scripts/gen_sbom.py` pins these files by hash
(`scripts/sbom-manifest.json`), so any change fails CI until the manifest is reviewed.

## Glue outside this directory (CubeMX `USB_DEVICE/Target`)

- `usbd_conf.c`: `USBD_LL_Transmit` / `USBD_LL_PrepareReceive` take a `uint32_t` size
  (v2.11 API).
- `usbd_conf.h`: `USBD_memset` / `USBD_memcpy` alias `memset` / `memcpy`. CubeMX
  generated them as empty for the old library, which made v2.11's
  `USBD_memset(hcdc, 0, ...)` in `USBD_CDC_Init` a no-op, so the CDC handle would keep
  stale state (e.g. `TxState`) across a USB reconnect.
