Name:       ingest-cli
Summary:    Ingestion core driver (development tool, never shipped)
Version:    0.1.0
Release:    1
License:    BSD-3-Clause
URL:        https://github.com/kaiwiechers/myNews
Source0:    %{name}-%{version}.tar.bz2
BuildRequires:  pkgconfig(Qt5Core)
BuildRequires:  pkgconfig(Qt5Network)
BuildRequires:  pkgconfig(Qt5Sql)

%description
Fetches, parses, stores and clusters real feeds without the UI, so the
ingestion core can be exercised end to end headlessly.

%prep
%setup -q -n %{name}-%{version}

%build
%qmake5
%make_build

%install
%qmake5_install

%files
%{_bindir}/ingest-cli

%changelog
* Mon Sep 07 2026 Kai Wiechers <140900215+TheChosenOne1984@users.noreply.github.com> 0.1.0-1
- Initial tool.
