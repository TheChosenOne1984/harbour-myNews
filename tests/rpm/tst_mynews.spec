Name:       tst_mynews
Summary:    myNews unit tests (development only, never shipped)
Version:    0.1.0
Release:    1
License:    BSD-3-Clause
URL:        https://github.com/kaiwiechers/myNews
Source0:    %{name}-%{version}.tar.bz2
BuildRequires:  pkgconfig(Qt5Core)
BuildRequires:  pkgconfig(Qt5Test)
BuildRequires:  pkgconfig(Qt5Sql)

%description
Headless unit tests for the feed parser and the clustering algorithm.

%prep
%setup -q -n %{name}-%{version}

%build
%qmake5
%make_build

%install
%qmake5_install

%files
%{_bindir}/tst_mynews

%changelog
* Mon Sep 07 2026 Kai Wiechers <140900215+TheChosenOne1984@users.noreply.github.com> 0.1.0-1
- Feed parser and clustering tests.
