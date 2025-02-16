Name: ssu
Version: 1.0.0
Release: 1
Summary: Seamless Software Upgrade
License: GPLv2+ and LGPLv2+ and BSD
Source0: %{name}-%{version}.tar.gz
URL: https://git.sailfishos.org/mer-core/ssu
BuildRequires: pkgconfig(Qt5Core)
BuildRequires: pkgconfig(Qt5DBus)
BuildRequires: pkgconfig(Qt5Network)
BuildRequires: pkgconfig(Qt5Xml)
BuildRequires: pkgconfig(Qt5Test)
BuildRequires: pkgconfig(libzypp)
BuildRequires: pkgconfig(libsystemd)
BuildRequires: pkgconfig(libshadowutils)
BuildRequires: pkgconfig(connman-qt5)
BuildRequires: oneshot
BuildRequires: doxygen
Requires(pre): sailfish-setup
Requires(pre): shadow-utils
Requires(pre): /usr/bin/groupadd-user
Requires(postun): shadow-utils
Requires: ssu-vendor-data
Requires: ssu-network-proxy

%description
%{summary} repository management tool. Manages software repositories, 
including hardware adaptation and other optional features, and repository 
user credentials. Alternative repository configurations may be specified for
RnD mode.

%files
%defattr(-,root,root,-)
%license COPYING COPYING.GPL COPYING.LGPL COPYING.BSD
%{_libexecdir}/zypp/plugins/urlresolver/*
%{_bindir}/ssu
%{_libdir}/libssu.so.*
%{_libdir}/ssu/libsandboxhook.so
%dir %{_sysconfdir}/zypp/credentials.d
# ssu itself does not use the package-update triggers, but provides
# them for the vendor data packages to use
%attr(0755, -, -) %{_oneshotdir}/*
%{_bindir}/ssud
%{_unitdir}/*.service
%{_datadir}/dbus-1/system-services/*.service
%dir %{_datarootdir}/%{name}
%dir %{_datarootdir}/%{name}/board-mappings.d
%dir %{_datarootdir}/%{name}/features.d
%dir %{_datarootdir}/%{name}/repos.d
%dir %{_sysconfdir}/%{name}/
%{_sysconfdir}/dbus-1/system.d/*.conf

%package network-proxy-plugin
Summary: Network Proxy support for ssu
Provides: ssu-network-proxy

%description network-proxy-plugin
%{summary}.

%files network-proxy-plugin
%defattr(-,root,root,-)
%{_libdir}/libssunetworkproxy.so

%package vendor-data-example
Summary: Sample vendor configuration data
Provides: ssu-vendor-data

%description vendor-data-example
%{summary}. A vendor (including Nemo) is supposed to put those configuration on device.

%files vendor-data-example
%defattr(-,root,root,-)
%attr(0664, root, ssu) %config %{_sysconfdir}/ssu/ssu.ini
%{_datadir}/ssu/*.ini


%package ks
Summary: Kickstart generator using %{name} data
# required for QA to pick up new macros
Provides: rpm-macros

%description ks
%{summary}. With ponies!

%files ks
%defattr(-,root,root,-)
%config %{_sysconfdir}/rpm/macros.ssuks
%{_bindir}/ssuks

%package slipstream
Summary: %{name} OS factory snapshot download provider

%description slipstream
Helper utility to authenticate downloads of factory snapshot manifests.

%files slipstream
%defattr(-,root,root,-)
%{_bindir}/ssuslipstream

%package declarative
Summary: QML plugin for libssu
BuildRequires: pkgconfig(Qt5Qml)

%description declarative
%{summary}

%files declarative
%{_libdir}/qt5/qml/Nemo/Ssu/*

%package devel
Summary: Development files for %{name}
Requires: %{name} = %{version}-%{release}

%description devel
%{summary}.

%files devel
%defattr(-,root,root,-)
%{_libdir}/libssu.so
%{_includedir}/ssu*.h
%{_libdir}/pkgconfig/ssu.pc


%package tests
Summary: Unit tests for %{name}
Requires: testrunner-lite

%description tests
%{summary}.

%files tests
%defattr(-,root,root,-)
/opt/tests/%{name}


%package tools
Summary: Tools for %{name}
Requires: rpm

%description tools
%{summary}.

%files tools
%defattr(-,root,root,-)
%attr(0755, -, -) /usr/sbin/ssu-repos.sh


%package doc
Summary: Documentation for %{name}

%description doc
%{summary}.

%files doc
%defattr(-,root,root,-)
%{_docdir}/%{name}


%prep
%setup -q -n %{name}-%{version}


%build
mkdir -p build && cd build
%qmake5 "VERSION=%{version}" DEFINES+='TARGET_ARCH=\\\"\"%{_target_cpu}\"\\\"' -recursive ..
make %{?_smp_mflags}
if [ -f ../.nodocs ]; then
    echo "Skip building documentation"
else
    cd .. && doxygen doc/Doxyfile
fi


%install
cd build && make INSTALL_ROOT=%{buildroot} install
mkdir -p %{buildroot}/%{_sysconfdir}/zypp/credentials.d
mkdir -p %{buildroot}/%{_datarootdir}/%{name}/board-mappings.d
mkdir -p %{buildroot}/%{_datarootdir}/%{name}/features.d
mkdir -p %{buildroot}/%{_datarootdir}/%{name}/repos.d
mkdir -p %{buildroot}/%{_docdir}/%{name}
mkdir -p %{buildroot}/%{_sysconfdir}/%{name}
cd .. && cp -R doc/html/* %{buildroot}/%{_docdir}/%{name}/


%pre
if [ -f /etc/ssu/ssu.ini ]; then
  chgrp ssu /etc/ssu/ssu.ini
  chmod 664 /etc/ssu/ssu.ini
fi

%postun
/sbin/ldconfig
if [ "$1" == 0 ]; then
  getent group ssu >/dev/null && groupdel ssu
fi

%post
/sbin/ldconfig
# make sure an old ssud isn't still running
killall ssud || :
