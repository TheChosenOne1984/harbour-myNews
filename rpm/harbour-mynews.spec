Name:       harbour-mynews
Summary:    News reader that groups the same story across sources
Version:    0.1.0
Release:    2
License:    BSD-3-Clause
URL:        https://github.com/kaiwiechers/myNews
Source0:    %{name}-%{version}.tar.bz2

Requires:   sailfishsilica-qt5 >= 0.10.9
# The article view. A runtime dependency rather than a link-time one: see the .pro.
Requires:   sailfish-components-webview-qt5

BuildRequires:  pkgconfig(sailfishapp) >= 1.0.2
BuildRequires:  pkgconfig(Qt5Core)
BuildRequires:  pkgconfig(Qt5Qml)
BuildRequires:  pkgconfig(Qt5Quick)
BuildRequires:  pkgconfig(Qt5Sql)
BuildRequires:  pkgconfig(Qt5Network)
BuildRequires:  desktop-file-utils

%description
Aggregates RSS/Atom feeds entirely on the device and groups
articles that report the same story across different sources
into a single entry. No backend, no account, no telemetry.

%prep
%setup -q -n %{name}-%{version}

%build
%qmake5
%make_build

%install
%qmake5_install
desktop-file-install --delete-original \
    --dir %{buildroot}%{_datadir}/applications \
    %{buildroot}%{_datadir}/applications/*.desktop

%files
%{_bindir}/%{name}
%{_datadir}/%{name}
%{_datadir}/applications/%{name}.desktop
%{_datadir}/icons/hicolor/*/apps/%{name}.png

%changelog
* Fri Oct 16 2026 Kai Wiechers <140900215+TheChosenOne1984@users.noreply.github.com> 0.1.0-2
- Request the Audio permission, so video in the article view has sound.
- Keep stories listed when a feed is removed; re-cluster retitled articles.
- Refresh on start; apply the chosen UI language on start, to dates and the catalogue.
* Mon Sep 07 2026 Kai Wiechers <140900215+TheChosenOne1984@users.noreply.github.com> 0.1.0-1
- Project skeleton: build, packaging, Sailjail, and runtime UI language switching.
