# Task 0 — Platform verification findings

**Date:** 2026-09-07
**Method:** two stages. **Stage A** — static inspection of the official SDK build target
`Sailfish_OS-5.1.0.11-Sailfish_SDK_Target-aarch64` and the Jolla release repository metadata,
without installing anything. **Stage B** — Sailfish SDK 3.13.5 installed with the Docker build
engine; findings re-verified by compiling and *running* probe code on the real aarch64 target, and
by putting a WebView-using app through the Harbour validator. §9 below records Stage B.
**Status:** §2 items 1 and 2 answered conclusively and confirmed at runtime; item 3 answered
conclusively at build and Harbour level, open at device-runtime level (no device available).

---

## 0. Correction to an assumption in the plan

`project_description.md` §2 names **Sailfish OS 5.2 (Finlayson)** as the target. 5.2 exists as an
OS release (currently 5.2.0.17, Jolla Phone only, August 2026), but **Jolla has published no 5.2
SDK build target.** The newest available target is **5.1.0.11 aarch64**.

We therefore build against 5.1.0.11 and run on 5.2. This is the normal situation for Sailfish and
is what Harbour expects anyway, but it means everything below describes 5.1, not 5.2.

Toolchain versions: Sailfish SDK **3.13.5** (July 2026), Linux online installer 30 MB, supports
headless installation (`non-interactive=1 accept-licenses=1 build-engine-type=docker`). Docker is
the recommended build engine on Linux.

---

## 1. Qt version — §2 item 1

**Qt 5.6.3** (package `qt5-qtcore 5.6.3+git60-1.19.5.jolla`, `libQt5Core.so.5.6.3`,
`QTCORE_VERSION_STR "5.6.3"`). The plan's concern was justified: Sailfish is still on 5.6.

**Compiler: GCC 13.4.0** (`gcc-c++ 13.4.0+git2`), runtime `libstdc++.so.6.0.32`. So the *language*
is modern even though the *framework* is not — **C++17 is available**, as is most of C++20. Only
Qt's own API surface is limited to 5.6.

Consequences to respect throughout:

| Not available (introduced later) | Use instead |
| --- | --- |
| `QQmlEngine::retranslate()` (5.10) | see §6 below — this one changes the i18n design |
| `QRandomGenerator` (5.10) | `qrand()` / `std::mt19937` |
| `Qt::SkipEmptyParts` (5.14) | `QString::SkipEmptyParts` |
| `QString::split(QChar, Qt::SplitBehavior)` overloads | 5.6 signatures |

Confirmed present and usable: `QXmlStreamReader`, `QtConcurrent`, `QSqlDatabase`,
`QNetworkAccessManager`, `QDateTime::fromString(..., Qt::RFC2822Date)` (needed for RSS `pubDate`),
`QString::normalized(QString::NormalizationForm_KC)` (needed for the clusterer's NFKC step).

QML side: allowed imports top out at **QtQuick 2.6**, consistent with Qt 5.6.

Qt modules shipped in the target: Core, Gui, Network, Sql, Qml, Quick, Concurrent, Xml,
XmlPatterns, DBus, Test, QuickTest, Widgets, OpenGL, Sensors, Compositor, WaylandClient.

---

## 2. SQLite and FTS5 — §2 item 2

**FTS5 is available. Search stays in the MVP.**

- Target ships `sqlite-libs 3.50.4+git1` (`libsqlite3.so.3.50.4`) — a current SQLite, not an old one.
- FTS5 is compiled in: `fts5`, `fts5vocab`, `fts5_api_ptr`, `fts5_locale`, `fts5_insttoken`
  present in the library.
- **The `trigram` tokenizer is present** alongside `unicode61`, `porter` and `ascii`. The
  language-independent tokenizer the plan asks for in §4 is therefore available as specified.
- Critically: `libqsqlite.so` (Qt's QSQLITE driver) has `NEEDED libsqlite3.so.0` — it links the
  **system** SQLite rather than bundling an old copy of its own. FTS5 is thus reachable through
  `QSqlDatabase`, not just through direct C API use. This was the real risk and it is clear.

`libsqlite3.so.0` is also on Harbour's allowed-libraries list, so linking it directly stays an
option if we ever need the C API.

---

## 3. Sailfish.WebView — §2 item 3

Split into two questions; only one can be answered without a device.

### Build level — answered, positive

The WebView is **not preinstalled in the SDK target**, but it is available in the 5.1.0.11/aarch64
repository and installable into the build target:

| Package | Version | Relevance |
| --- | --- | --- |
| `qtmozembed-qt5-devel` | 1.55.10 | provides `pkgconfig(qt5embedwidget)` — the `.pro`/`.spec` dependency §9 asks for |
| `sailfish-components-webview-qt5` | 1.6.9 | the runtime QML plugin |
| `sailfish-components-webview-qt5-devel` | 1.6.9 | provides `pkgconfig(qt5embedwidget)`, `pkgconfig(sailfishwebengine)` |
| `sailfish-components-webview-qt5-popups` / `-pickers` | 1.6.9 | optional companions |

Install into the target with `sfdk tools package-install` before the first build.

### Harbour level — answered, positive

This was an open question in my reading of the plan, and it is now settled from the validator
configuration shipped in the target itself (`/usr/share/sdk-harbour-rpmvalidator/`):

- `allowed_qmlimports.conf`: `Sailfish.WebView 1.0`, `Sailfish.WebView.Controls 1.0`,
  `Sailfish.WebView.Pickers 1.0`, `Sailfish.WebView.Popups 1.0`, `Sailfish.WebEngine 1.0`
- `allowed_libraries.conf`: `libqt5embedwidget.so.1`
- `allowed_requires.conf`: `sailfish-components-webview-qt5` and its `-popups` / `-pickers`
- `allowed_permissions.conf`: `WebView` is a permitted Sailjail permission

**A Harbour app may use the WebView.** Open decision #1 no longer has to trade the article detail
view against Harbour distribution.

### Runtime level — open

Whether the WebView actually runs on the device cannot be verified here: there is no Jolla Phone,
and per the plan's §9 the WebView does not work in the emulator. This stays open until a device is
available.

---

## 4. Harbour compliance of the planned architecture

Everything the design needs is on the allowed lists.

- **Libraries:** `libQt5Core/Gui/Network/Sql/Qml/Quick/Concurrent/Xml/XmlPatterns.so.5` and
  `libsqlite3.so.0` — all allowed. Note the `!((Qt_5_PRIVATE_API))` marker: linking Qt *private*
  API is rejected. We do not, but it rules out some third-party snippets.
- **Sailjail permissions needed:** `Internet` and `WebView`. Both allowed.
- **Organization name** in the `.desktop` `X-Sailjail` section must not be `com.jolla` or
  `org.sailfishos`.
- **Bonus finding, relevant to open decision #3:** `Nemo.Notifications 1.0`, `Nemo.KeepAlive 1.2`
  and `Nemo.Configuration 1.0` are all Harbour-allowed QML imports. Notifications and a degree of
  keep-alive are therefore *not* blocked by choosing Harbour — only a bundled systemd daemon is.
  Open decision #1 can proceed as Harbour-first without foreclosing #3 entirely.

---

## 5. TLS and HTTP

`libQt5Network.so.5.6.3` links **OpenSSL 1.1.1w** (`libssl.so.1.1`), which supports TLS 1.3.
Publisher HTTPS endpoints are reachable. (The target also ships OpenSSL 3.5.6, but Qt does not use
it.) `libz` is linked, so the gzip requirement in §5 is covered by `QNetworkAccessManager`'s
built-in handling.

---

## 6. Consequence for the i18n requirement

The UI-language requirement (English default, German shipped, easily extensible) is unaffected in
its *build* mechanics but constrained in its *runtime switching*.

**Good news:** `sailfishapp_i18n.prf` exists in the target's mkspecs and does exactly what is
needed — runs `lupdate` over `src/` and `qml/`, runs `lrelease`, and installs the `.qm` files to
`/usr/share/$${TARGET}/translations`. So `CONFIG += sailfishapp_i18n` plus a `TRANSLATIONS` list is
the whole build-side story, and adding a language later means adding one `.ts` file.

**Constraint:** `QQmlEngine::retranslate()` does not exist in Qt 5.6 (verified: the symbol is absent
from `qqmlengine.h`). QML bindings will therefore not re-evaluate `qsTr()` after swapping the
`QTranslator`. Three options:

1. **Reload the root QML component** after installing the new translator. One place in the code, no
   per-string discipline required. Costs the page stack — the user lands back on the feed page.
   C++-side models and the database connection survive, so it is cheap.
2. **`emptyString` binding trick** — expose a singleton whose dummy property changes on language
   switch and write every string as `qsTr("…") + tr.e`. Preserves state, but every single string
   must remember it, forever.
3. **Require a restart** with a hint in the settings.

**Recommendation: option 1.** Changing UI language is a rare, deliberate action; returning to the
main page afterwards is acceptable, and it keeps the discipline burden off every future string.

**Second consequence:** Silica's `Format.formatDate(date, Formatter.DurationElapsed)` renders
relative timestamps in the *system* locale, not in the app's chosen UI language — so a user running
an English UI on a German phone would see German time strings. The relative-time formatting in the
feed delegate should be implemented in-app with `qsTr()` plural forms instead.

**Agreed language behaviour:** setting offers `System` / `English` / `Deutsch`, defaulting to
`System`; English is the source and fallback language. Available languages are discovered at
runtime by scanning the installed translations directory, so shipping a new language is a file, not
a code change.

---

## 7. Summary against §2

| # | Question | Answer |
| --- | --- | --- |
| 1 | Qt version in the SDK target | **Qt 5.6.3**, GCC 13.4, C++17 usable, QtQuick ≤ 2.6 |
| 2 | SQLite FTS5 | **Available**, SQLite 3.50.4 with FTS5 *and* the `trigram` tokenizer, reachable via `QSqlDatabase` |
| 3 | Sailfish.WebView on target | **Buildable and Harbour-legal**; installable from the repo. Device runtime unverified — no device |
| — | 5.2 SDK target | **Does not exist.** Build against 5.1.0.11 aarch64 |
| — | Harbour vs. WebView conflict | **No conflict.** Harbour-first stands |

## 8. Still open

1. Does the WebView run on the actual Jolla Phone (needs a device).
2. Whether the app built against 5.1.0.11 behaves identically on 5.2.0.17 (needs a device).
3. Everything under §12 of the plan that Task 0 does not touch: prior-art review, catalogue hosting
   URL, background-refresh strategy.

---

## 9. Stage B — verified by building and running

Sailfish SDK 3.13.5 installed headless into `~/SailfishOS` (~1.7 GB) with the Docker build engine
(`sailfish-sdk-build-engine:kai`, 1.35 GB). Targets installed: `SailfishOS-5.1.0.11` in `aarch64`,
`armv7hl` and `i486`.

```
export PATH="$HOME/SailfishOS/bin:$PATH"
sfdk engine start
sfdk tools list
```

### The target executes code

Scratchbox2 runs aarch64 binaries under qemu inside the build engine — `sfdk tools exec
SailfishOS-5.1.0.11-aarch64 uname -m` prints `aarch64`. **Everything below is therefore an actual
runtime result on the target's own libraries, not an inference from file inspection.** This also
means headless C++ tests (parser, simhash, date handling) can be run against the real target
without a device.

### Probe output

A Qt console app built with `sfdk qmake` / `sfdk make` for the aarch64 target and executed via
`sfdk tools exec`:

```
Qt compile-time : 5.6.3
Qt runtime      : 5.6.3
__cplusplus     : 201703
SQL drivers     : QSQLITE
SQLite version  : 3.50.4

FTS5 checks (through QSqlDatabase / QSQLITE driver):
  [ OK ] plain fts5 table
  [ OK ] fts5 with tokenize='trigram'
  [ OK ] external-content fts5 (plan section 4)
  [ OK ] trigram MATCH found: Grossbrand in der Hamburger Speicherstadt

Other plan-relevant runtime checks:
  NFKC normalize  : "Großbrand ﬁn der Straße" -> "Großbrand fin der Straße"
  RFC2822 pubDate : 2026-09-07T06:30:00Z
  ISO8601 updated : 2026-09-07T08:30:00Z

C++17 checks (Qt 5.6 headers compiled with -std=c++17):
  GCC             : 13.4.0
  from_chars / variant / structured bindings : all OK
  hamming(popcnt) : 8
```

Points worth keeping:

- The **external-content FTS5 table with `tokenize='trigram'`** that §4 of the plan specifies was
  created and queried successfully through `QSqlDatabase`. Searching `speicher` matched
  *Speicherstadt* — the language-independent substring behaviour the design depends on.
- **Qt 5.6 headers compile cleanly under `-std=c++17`** with GCC 13.4. Add
  `QMAKE_CXXFLAGS += -std=c++17` (Qt 5.6's qmake does not understand `CONFIG += c++17`).
- `__builtin_popcountll` is available for the SimHash Hamming distance (§6.4).
- `Qt::RFC2822Date` and `Qt::ISODate` both parse correctly — §5's dual date-format requirement is
  covered by Qt itself.
- `QSQLITE` is the only SQL driver present, which is all we need.

### WebView: Harbour validation passed

The WebView packages are installable into the target:

```
sfdk tools target package-install SailfishOS-5.1.0.11-aarch64 \
    sailfish-components-webview-qt5-devel qtmozembed-qt5-devel
```

After that `pkg-config --modversion qt5embedwidget` reports **1.55.10**, and the QML module
contains `WebView.qml`, `WebViewFlickable.qml` and **`WebViewPage.qml`** — the component §9 of the
plan wants for the article detail page.

A minimal Silica app that imports `Sailfish.WebView 1.0`, links `qt5embedwidget` and declares
`Permissions=Internet;WebView` was built, packaged and run through the Harbour validator:

```
Validation succeeded: .../harbour-task0probe-0.1-1.aarch64.rpm
```

All suites passed — Package name, RPM file name, QML files, Desktop file, Paths, Libraries, Icon,
Symbols, Sandboxing, RPATH, Architecture, Vendor. **A Harbour app may ship the WebView; this is now
demonstrated, not just read off a list.**

### Build-system gotchas found the hard way

1. **`sailfishapp` must be named explicitly in `PKGCONFIG` when combining it with another
   pkg-config dependency.** The documented form

   ```
   CONFIG += sailfishapp link_pkgconfig
   PKGCONFIG += qt5embedwidget
   ```

   fails to link with `undefined reference to SailfishApp::main(int&, char**)`, because
   `link_pkgconfig` is resolved before `sailfishapp.prf` gets to append itself to `PKGCONFIG`.
   Working form:

   ```
   CONFIG += sailfishapp sailfishapp_i18n link_pkgconfig
   PKGCONFIG += sailfishapp qt5embedwidget
   ```

2. **`qt5embedwidget` injects an RPATH into the xulrunner directory.** The Harbour RPATH check
   emits a warning about it but still passes. Watch it if the check ever tightens.

3. Harbour wants icons in **86x86, 108x108, 128x128 and 172x172**; supplying only 86x86 produces
   warnings (not failures). Use `SAILFISHAPP_ICONS = 86x86 108x108 128x128 172x172`.

4. rpmlint flags `no-changelogname-tag` — the `.spec` needs a `%changelog` section.

### i18n pipeline verified end to end

`CONFIG += sailfishapp_i18n` with `TRANSLATIONS += translations/harbour-<name>-de.ts` did exactly
what §6 assumed: `lupdate` scanned `src/` and `qml/`, `lrelease` produced
`harbour-task0probe-de.qm`, and it landed in the package at

```
/usr/share/harbour-task0probe/translations/harbour-task0probe-de.qm
```

Plural forms (`qsTr("%n more source(s)", "", n)`), disambiguation comments and `//:` translator
comments all survived extraction. `Name[de]=` in the `.desktop` file passed the desktop-file check.

**Adding a language really is one file**: drop in `harbour-<name>-<lang>.ts`, list it in
`TRANSLATIONS`, rebuild. The runtime language picker scans the installed translations directory,
so no code changes either.

### Emulator — not installed

The installer skipped the emulator: it is a VirtualBox VM, and VirtualBox is not present on this
machine. VirtualBox needs a root install, which this session cannot perform. Until it exists there
is **no way to see the UI run** — headless C++ can be tested via `sfdk tools exec` as shown above,
but QML cannot.

Two host-specific risks noted before installing:

- Kernel **7.0.0-31-generic** is very new; VirtualBox 7.2.6's DKMS modules must build against it.
- **`kvm_intel` is loaded.** VirtualBox and KVM contend for the CPU virtualisation extensions;
  expect to unload `kvm_intel` before an emulator VM will start.

Secure Boot is disabled, so unsigned DKMS modules will at least load.

---

## 10. Revised open items

1. Does the WebView run on the actual Jolla Phone (needs a device).
2. Does a package built against 5.1.0.11 behave identically on 5.2.0.17 (needs a device).
3. Emulator not yet available (needs VirtualBox, needs root).
4. Untouched by Task 0: prior-art review, catalogue hosting URL, background-refresh strategy.

---

## 11. Stage B, part 2 — emulator, and the i18n design decision verified in a running app

### VirtualBox and the emulator

VirtualBox 7.2.6 installed from the Ubuntu archive; DKMS built and signed modules for kernel
**7.0.0-31-generic** without trouble. The emulator was then installed headlessly — no GUI
maintenance tool needed:

```
sfdk emulator list --available
sfdk emulator install SailfishOS-5.1.0.11
sfdk emulator start
```

It runs **Sailfish OS 5.1.0.11 (Pispala)**, i686, device model *Jolla C2*, 872 MB on disk.

**The predicted KVM conflict did not materialise.** `kvm_intel` stays loaded (usage count 1, no
qemu/libvirt VM running) and the emulator starts anyway — on kernels since 6.x KVM only enables
VMX when a VM is actually created, so `modprobe -r kvm_intel` was unnecessary. Worth remembering
if a KVM guest is ever started at the same time.

### Deploying: Docker build engine cannot reach the VirtualBox emulator

`sfdk deploy --sdk` fails:

```
ssh: connect to host 10.220.220.1 port 22: Connection timed out
```

The build engine is a Docker container on a bridge network; the emulator is a VirtualBox VM on a
host-only network. They have no route to each other. This is a consequence of choosing the Docker
build engine (which is otherwise the faster choice) and is **not** a misconfiguration to chase.

Working route — deploy from the host over the emulator's forwarded SSH port:

```
K=~/SailfishOS/vmshare/ssh/private_keys/sdk
scp -i $K -P 2223 -o IdentitiesOnly=yes RPMS/<pkg>.i486.rpm root@127.0.0.1:/tmp/
ssh -i $K -p 2223 -o IdentitiesOnly=yes root@127.0.0.1 "rpm -Uvh --force /tmp/<pkg>.i486.rpm"
```

`-o IdentitiesOnly=yes` is required: without it the local SSH agent offers too many keys and the
emulator drops the connection with *Too many authentication failures*.

Launching an app over SSH needs the compositor's session environment:

```
export XDG_RUNTIME_DIR=/run/user/100000
export DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/100000/dbus/user_bus_socket
export WAYLAND_DISPLAY=../../display/wayland-0
invoker --type=silica-qt5 -s /usr/bin/<app>
```

Screenshots can be taken from the host without touching the guest:
`VBoxManage controlvm "SailfishOS-5.1.0.11" screenshotpng out.png`.

**Sailjail prompts on first launch** for every permission declared in the `.desktop` file
("*<app> needs following access permission(s): Internet — Cancel / Accept*"). Expected for the real
app, which needs `Internet`; just be aware that an automated first run will block on it, and
VBoxManage offers keyboard input only, no mouse.

**WebView is not installed in the emulator image** (`sailfish-components-webview-qt5` absent),
consistent with the plan's note that the WebView does not work there.

### The i18n runtime switch: built, deployed, and observed working

§6 concluded that Qt 5.6's missing `QQmlEngine::retranslate()` forces a choice, and recommended
reloading the root QML component. That recommendation has now been implemented and run in the
emulator rather than argued on paper.

A `LanguageManager` C++ class:

- discovers shipped languages by scanning the install directory for `harbour-<app>-*.qm`, so adding
  a language stays a file-only change;
- exposes `""` as *System* — `QTranslator::load(QLocale(), ...)`, falling back to the untranslated
  English source when the device locale has no `.qm`;
- on `setLanguage()`, removes the old `QTranslator`, installs the new one, then reloads the view:

```cpp
const QUrl source = m_view->source();
m_view->setSource(QUrl());
m_view->engine()->clearComponentCache();
m_view->setSource(source);
```

This requires `SailfishApp::createView()` in `main()` rather than the one-line
`SailfishApp::main()`, so the view pointer is available.

**Observed result.** The app was launched with `LANG=en_US.UTF-8` and a timer that calls
`setLanguage("de")` after eight seconds. Before: *Latest news / Fire in the Hamburg warehouse
district / Sources / 4 more source(s) / Language*. After, in the same running process: *Aktuelle
Meldungen / Großbrand in der Hamburger Speicherstadt / Quellen / 4 weitere Quellen / Sprache* — and
the selected radio moved from *System* to *Deutsch*. Page header, body labels, section header,
plural form and the language list itself all retranslated.

Confirmed by this: the reload approach works, `qsTr()` plural forms work, the *System* option
resolves against the device locale, and no per-string `emptyString` discipline is needed anywhere.

**Decision recorded: option 1 from §6 is the one to implement.** The cost is the page stack — the
user returns to the main page after switching language — which is acceptable for a rare, deliberate
action.

### Additional gotcha

`rpmlint` rejects a `%changelog` date whose weekday does not match the date. 2026-09-07 is a
Monday; `Sun Sep 07 2026` produced *bogus date in %changelog*.

---

## 12. Answered on hardware (2026-10-16)

On a Jolla Phone with Sailfish OS **5.2.0.20 (Finlayson)**:

- A package built against the 5.1.0.11 target installs and runs. Open item §10.2 is
  answered: it behaves as on 5.1.
- `Sailfish.WebView` runs. Articles render and scroll in the built-in view, which answers
  §8.1 and §10.1. The emulator's scroll defect does not exist on the device (BUILDING.md).
- The device ships `sailfish-components-webview-qt5` 1.7.4, newer than the 1.6.9 in the
  build target. Nothing in the app depended on the difference.
- Video in the web view is silent unless the app asks for the `Audio` permission:
  `Base.permission` disables PulseAudio for every app, and `WebView` does not lift it.
