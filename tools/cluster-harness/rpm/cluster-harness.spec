Name:       cluster-harness
Summary:    Clustering tuning harness (development tool, never shipped)
Version:    0.1.0
Release:    1
License:    BSD-3-Clause
URL:        https://github.com/TheChosenOne1984/harbour-myNews
Source0:    %{name}-%{version}.tar.bz2
BuildRequires:  pkgconfig(Qt5Core)

%description
Runs the myNews clustering algorithm over a JSON corpus so that the
Hamming threshold and time window can be tuned against real feed data.

%prep
%setup -q -n %{name}-%{version}

%build
%qmake5
%make_build

%install
%qmake5_install

%files
%{_bindir}/cluster-harness

%changelog
* Mon Sep 07 2026 Kai Wiechers <140900215+TheChosenOne1984@users.noreply.github.com> 0.1.0-1
- Initial harness.
