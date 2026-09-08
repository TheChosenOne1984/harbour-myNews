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

### Granting Sailjail permissions without tapping

Changing `Permissions=` in the `.desktop` makes Sailjail show its "myNews needs following
access permission(s)" dialog on the next start, and there is no way to press Accept from
here. The decision lives in a plain INI file, so write it directly:

```sh
ssh $SSHOPT -p 2223 root@127.0.0.1 '
  systemctl stop sailjaild
  sed -i "/\[harbour-mynews\]/,/^$/{s/^Allowed=0/Allowed=1/;
          s/^Granted=$/Granted=Internet;WebView;Documents;/}" \
      /home/.system/var/lib/sailjail/settings/user-100000.settings
  systemctl start sailjaild'
```

`Granted=` must list exactly what the `.desktop` asks for. The daemon rewrites the file,
so stop it first.

### Screenshots

From the host, without touching the guest:

```sh
VBoxManage controlvm "SailfishOS-5.1.0.11" screenshotpng out.png
```

### The web view in the emulator

`Sailfish.WebView` is not in the i486 target, so it cannot be built or checked against
there; it arrives in the *emulator image* only once `sailfish-browser` is installed
(`pkcon install sailfish-browser` as root). After that articles do render.

They render exactly once per boot. The second and further launches segfault in
`GeckoWorkerThre` inside `libxul.so`, which kills the whole app because the engine is
in-process:

```
kernel: GeckoWorkerThre[…]: segfault at 6c … in libxul.so
```

This is the emulator's software-rendered Gecko, not the app: it reproduces on unmodified
commits. Reboot the emulator (`systemctl reboot` over SSH) for each article-view check,
and do not read a dead process after a successful screenshot as an app bug.

### The article view does not scroll in the emulator

Reproducible, measured, and not yet explained. The engine scrolls correctly; the
compositor does not follow.

`QuickMozView.scrollableOffset.y` tracks a drag smoothly from 0 to the end of the
gesture and stays there, and `scrollTo()`, `scrollBy()` and `window.scrollTo()` move it
just as well. But the painted web layer is placed at the *document* origin rather than
the viewport origin: content at document position d appears at screen position d, and
only the band Gecko currently has painted is drawn at all. Measured against the
ambience showing through the unpainted area, with the view filling the page:

| `scrollableOffset.y` | first painted screen row |
|---------------------:|-------------------------:|
| 600                  | 597                      |
| 1200                 | 1199                     |
| 2000                 | off screen               |

So the reader sees the article stand still while an empty strip grows from the top.

Ruled out, each by experiment rather than argument:

* the pulley menu and its `Flickable.DragOverBounds` (removed; unchanged),
* `chromeGestureEnabled`, Gecko's dynamic toolbar (disabled; unchanged),
* the view sitting below a `PageHeader` at non-zero y (filled the page; unchanged),
* `layers.async-pan-zoom.enabled` and `apz.enabled` (set false; unchanged),
* the gesture itself -- programmatic scrolling displaces the layer identically.

Binding the view's `y` to `-scrollableOffset.y` cancels the displacement exactly, which
confirms the mechanism, but it is not a fix: moving the item moves Qt's input mapping
with it, trading the rendering error for an equal error in where touches land.

sailfish-browser scrolls the same page on the same emulator, but that is not a
counter-example: it renders through `QOpenGLWebPage` into its own window, not through
`QuickMozView` and the scene graph. No other component on the image uses `QuickMozView`,
so the emulator offers no second opinion.

**Untested on hardware.** The device check is two minutes: open any article, drag, and
see whether the text moves. Until then treat the built-in reader as unverified and use
Settings → "open articles in the system browser", which bypasses the view entirely.

## Tests

Headless, and they run on the real target through scratchbox2's qemu — same Qt, same
libraries as the device, no device required:

```sh
cd tests
sfdk -c target=SailfishOS-5.1.0.11-aarch64 build
sfdk tools exec SailfishOS-5.1.0.11-aarch64 $PWD/tst_mynews
```

The fetcher tests start a stub HTTP server on localhost, so nothing reaches the network.

## Development tools

Neither is packaged for Harbour. Both build the same way as the tests.

- `tools/cluster-harness` — tunes the clustering parameters against a corpus. See
  [CLUSTERING.md](CLUSTERING.md).
- `tools/ingest-cli` — drives fetch, parse, store and cluster against real feeds. See
  [INGESTION.md](INGESTION.md).

Remember to clean between targets here too; these build in-source as well.

## The emulator needs more memory than it ships with

The emulator VM is created with 1024 MB, which is not enough to render a web page: the
article view is a full Gecko engine in the app's own process, and it dies with
`MOZ_CRASH([unhandlable oom] Failed to allocate new chunk during GC)`, taking the app with
it. Give it more before testing anything that opens an article:

```sh
sfdk emulator stop
sfdk emulator set vm.memory-size=3072
sfdk emulator start
```

Even then the built-in view is unreliable in the emulator — the plan said so from the
start, and nothing here contradicts it. That is what the settings switch "open articles in
the system browser" is for, and it is also the honest answer for a device where a heavy
page misbehaves.

## Running the app against a local catalogue

Nothing is bundled, so a fresh install fetches the catalogue from the address in the
settings. During development that address can point at your own machine:

```sh
cd catalog && python3 -m http.server 8899 &
```

The emulator reaches the host at **10.0.2.2** (the NAT gateway), not at the host-only
address. Point the app at it before the first start:

```sh
ssh $SSHOPT -p 2223 defaultuser@127.0.0.1 \
  "mkdir -p ~/.config/harbour-mynews && \
   printf '[catalog]\nurl=http://10.0.2.2:8899/catalog.json\n' \
     > ~/.config/harbour-mynews/harbour-mynews.conf"
```

Where the app keeps its files inside the emulator, all within the Sailjail whitelist:

| What | Path |
| --- | --- |
| Database | `~/.local/share/harbour-mynews/harbour-mynews/mynews.db` |
| Catalogue cache | `~/.cache/harbour-mynews/harbour-mynews/catalog.json` |
| Settings | `~/.config/harbour-mynews/harbour-mynews.conf` |

Delete all three to reproduce a first start.

**Verifying QML.** QML errors only surface at runtime, and nothing in the emulator can be
tapped: `VBoxManage` sends keystrokes but not taps, and the pointing device cannot be
driven from the guest either — writing to `/dev/input/eventN` sends events *to* a device
rather than injecting them, and real injection would need `/dev/uinput` plus a compiled
helper, which lipstick's `VBoxTouch` plugin may not even listen to. So to check that a
page renders, temporarily add timers to `qml/harbour-mynews.qml` that push it with
plausible parameters, screenshot, and revert the file afterwards.

**Sailjail re-asks whenever the permission set changes.** Adding a permission to the
`.desktop` file revokes the stored grant, and the prompt cannot be answered without a tap.
Editing `/home/.system/var/lib/sailjail/settings/user-100000.settings` does not help:
`sailjaild` re-derives the entry when it starts and resets `Allowed` and `Granted`. That
is correct behaviour — an app that asks for more in an update has to ask the user again —
but it means a screenshot run after a permission change needs the `.desktop` file
temporarily reduced to the permissions the page under test actually needs.

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
