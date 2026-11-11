# dm32mayhem.com

The web flasher at <https://dm32mayhem.com>: back up a Baofeng DM-32UV, install DM32Mayhem, and restore the backup.
The browser does the work over Web Serial; the Go server only serves the page, the fixed image and its manifest.

## The flasher

`web/site/js/flasher/` is a vendored copy of RadioFoundry's MIT web flasher, with these changes:

- `app.js` loads one fixed image (`/firmware/dm32mayhem.bin`) instead of offering a feature builder or a build service,
  and puts it on the state when a radio is selected. The **Update** task writes that image over a radio already running
  DM32Mayhem (no feature decisions, no build step), optionally backing up first.
- `app.js` posts its optional flash log and problem reports to this server's `/v1/` endpoints (which discard them).
- `c7000ui.js` drops the build step: the image is already chosen. It also skips the blank-sector size count (the
  read-time estimates use the board's fixed rate) so the write starts at once, and shows what it is doing before the
  first byte.
- `gen_site.py` rebrands the English strings for DM32Mayhem.

The rest of the flasher, including the backup and restore bundles and the HR_C7000 stub protocol, is unchanged. Keep
their SPDX headers; the code is Copyright (c) 2026 The RadioFoundry Authors, MIT.

## Build the site data

The firmware must be built first (`tools/build-bbs.sh`). Then:

```
python3 web/tools/gen_site.py --version v26.11
```

This writes `web/site/data/*.json` (the DM-32UV registry entry, flash tables, the C7 stub, the release), the fixed
image and manifest at `web/site/firmware/`, the English catalog and `index.html`. Run it again after every firmware
change or version bump.

## Run locally

```
cd web
go run . -dev :8080          # http://localhost:8080 (Web Serial needs localhost or https)
```

The browser's simulated radio is at `#sim=dm32-stock` (original firmware) and `#sim=dm32` (DM32Mayhem): the whole
install, backup and restore flow runs with no hardware.

## Deploy

```
./deploy/deploy.sh           # builds, copies the binary and unit, restarts the service
```

The server is the one named `DM32Mayhem` in sshm (host `dm32mayhem.com`). The service runs as `dm32mayhem` with only
`CAP_NET_BIND_SERVICE`; certificates come from Let's Encrypt (account `sarahroselives@protonmail.com`) through Go's
`autocert`, cached in `/var/lib/dm32mayhem/autocert`. The deploy script opens TCP 80 and 443 in ufw.

Check it with:

```
curl -I https://dm32mayhem.com/
curl -s https://dm32mayhem.com/healthz
```

## Patches to reapply if the vendored flasher is refreshed

1. `app.js`: `const API = new URL("/v1/", location.href).href;` (an absolute base: `new URL(path, "/v1/")` throws)
2. `app.js`: `FIXED_FIRMWARE`/`loadFixedFirmware()` and `S.firmware = FIXED_FIRMWARE` in `selectRadio`; load it in
   `main()` after `loadData`; keep the Update task in `startView`, and in `connectForUpdate`/`updateView` drop the
   build step and feature decisions (the fixed image is written as it is).
3. `c7000ui.js`: `INSTALL`/`RECOVER` without `"build"`.
4. `gen_site.py`: the string overrides and the fixed image.
