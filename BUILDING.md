# Building, deploying and testing myNews

Practical recipes. For *why* the platform behaves the way it does — Qt version, FTS5,
WebView, Harbour rules — see [task0-findings.md](task0-findings.md).

## Prerequisites

Sailfish SDK 3.13.5 in `~/SailfishOS`, Docker build engine, targets for
`SailfishOS-5.1.0.11` in `aarch64` (device) and `i486` (emulator). VirtualBox 7.2.6 for
the emulator.

```sh
export PATH="$HOME/SailfishOS/bin:$PATH"
sfdk engine start
```

There is no 5.2 SDK target; 5.1.0.11 is what we build against. See task0-findings.md §0.

## Build

```sh
sfdk -c target=SailfishOS-5.1.0.11-aarch64 build     # device
sfdk -c target=SailfishOS-5.1.0.11-i486    build     # emulator
```

> **Clean between targets.** `sfdk` builds in-source, so object files from a previous
> target are silently reused and produce a broken package — an i486 RPM that requires a
> 64-bit `libstdc++`, for instance. Before switching architecture:
>
> ```sh
> rm -f Makefile .qmake.stash *.o moc_*.cpp moc_*.h harbour-mynews
> rm -rf RPMS documentation.list
> ```

## Harbour validation

```sh
sfdk -c target=SailfishOS-5.1.0.11-aarch64 check
```

Must end in `Validation succeeded` with zero rpmlint errors. The remaining
`unstripped-binary-or-object` warning is produced by `sfdk`'s own `QMAKE_STRIP=:` and is
expected for local builds.

## Emulator

```sh
sfdk emulator start
sfdk emulator stop
```

`kvm_intel` may stay loaded — on kernels since 6.x KVM only claims VMX when a guest is
actually created, so VirtualBox and KVM coexist. Only an actually running KVM guest
would conflict.

### Deploying to the emulator

`sfdk deploy --sdk` **does not work** with the Docker build engine: the engine is a
container on a bridge network and the emulator a VirtualBox VM on a host-only network,
with no route between them. Deploy from the host instead:

```sh
K=~/SailfishOS/vmshare/ssh/private_keys/sdk
SSHOPT="-i $K -o IdentitiesOnly=yes -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null"
scp $SSHOPT -P 2223 RPMS/harbour-mynews-*.i486.rpm root@127.0.0.1:/tmp/
ssh $SSHOPT -p 2223 root@127.0.0.1 "rpm -Uvh --force /tmp/harbour-mynews-*.i486.rpm"
```

`IdentitiesOnly=yes` is required, otherwise the local SSH agent offers too many keys and
the emulator answers *Too many authentication failures*.

### Running it

The app needs the compositor's session environment:

```sh
ssh $SSHOPT -p 2223 defaultuser@127.0.0.1 '
  export XDG_RUNTIME_DIR=/run/user/100000
  export DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/100000/dbus/user_bus_socket
  export WAYLAND_DISPLAY=../../display/wayland-0
  invoker --type=silica-qt5 -s /usr/bin/harbour-mynews'
```

Set `LANG`/`LC_ALL` in that same command to exercise the "System" language path — note
that SSH forwards your *host* locale by default, which is easy to mistake for the
emulator's own.

Stop it with `pkill -x harbour-mynews`. Do not use `pkill -f harbour-mynews` over SSH:
the pattern matches the remote shell running your own command and kills the session.

### The Sailjail permission prompt

On first launch Sailjail asks the user to grant each permission declared in the
`.desktop` file. `VBoxManage` can send keystrokes but not taps, so an automated run
blocks on that dialog. Pre-grant it instead:

```sh
# on the emulator, as root
systemctl stop sailjaild
# in /home/.system/var/lib/sailjail/settings/user-100000.settings, under
# [harbour-mynews]:  Allowed=1  Agreed=1  Granted=Internet;
systemctl start sailjaild
```

### Screenshots

From the host, without touching the guest:

```sh
VBoxManage controlvm "SailfishOS-5.1.0.11" screenshotpng out.png
```

## Translations

`CONFIG += sailfishapp_i18n` runs `lupdate` over `src/` and `qml/` and `lrelease` on
every file in `TRANSLATIONS`, installing the `.qm` files to
`/usr/share/harbour-mynews/translations/`.

Adding a language:

1. `cp translations/harbour-mynews-de.ts translations/harbour-mynews-<code>.ts` and
   translate it (Qt Linguist, or by hand).
2. Add the file to `TRANSLATIONS` in `harbour-mynews.pro`.

No code change: `LanguageManager` discovers installed `.qm` files at runtime and the
settings page lists whatever it finds. English is the source language and the fallback.
