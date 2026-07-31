Name:           xscreensaver-mystify
Version:        0.2.0
Release:        1%{?dist}
Summary:        Mystify display hack for XScreenSaver

License:        MIT
URL:            https://github.com/johngrimmreaper/xscreensaver-mystify
Source0:        %{url}/archive/refs/tags/v%{version}.tar.gz#/%{name}-%{version}.tar.gz

BuildRequires:  gcc
BuildRequires:  make
BuildRequires:  pkgconfig(x11)
BuildRequires:  desktop-file-utils
BuildRequires:  sed

Requires:       xscreensaver-base >= 1:6.05

%description
Mystify is a lightweight Xlib recreation of the Windows 95 and Windows 98
"Mystify Your Mind" screen saver. Colorful deforming wire polygons move around
the screen while retaining and incrementally erasing previous outlines.

This package installs Mystify as a standalone non-OpenGL display hack for
XScreenSaver. It follows the modular hack configuration layout used by Fedora
and EPEL XScreenSaver packages.

%prep
%autosetup

%build
%set_build_flags
%make_build

%install
%make_install \
    PREFIX=%{_prefix} \
    LIBEXECDIR=%{_libexecdir}/xscreensaver \
    SYSCONFDIR=%{_datadir}/xscreensaver/config

install -d -m 0755 \
    %{buildroot}%{_datadir}/applications/screensavers
desktop-file-install \
    --vendor=xscreensaver \
    --dir=%{buildroot}%{_datadir}/applications/screensavers \
    --set-key=OnlyShowIn \
    --set-value='GNOME;' \
    debian/mystify.desktop

install -Dpm 0644 mystify.6x \
    %{buildroot}%{_mandir}/man6x/mystify.6x
sed -i '1 s/^\.TH XScreenSaver 1 /.TH XScreenSaver 6x /' \
    %{buildroot}%{_mandir}/man6x/mystify.6x

install -d -m 0755 \
    %{buildroot}%{_datadir}/xscreensaver/hacks.conf.d
cat > \
    %{buildroot}%{_datadir}/xscreensaver/hacks.conf.d/xscreensaver-mystify.conf \
    <<'EOF'
                                mystify --root                              \n\
EOF

%check
%make_build check
desktop-file-validate \
    %{buildroot}%{_datadir}/applications/screensavers/xscreensaver-mystify.desktop

%post
if [ -x %{_sbindir}/update-xscreensaver-hacks ]; then
    %{_sbindir}/update-xscreensaver-hacks || :
elif [ -x %{_bindir}/update-xscreensaver-hacks ]; then
    %{_bindir}/update-xscreensaver-hacks || :
fi

%postun
if [ -x %{_sbindir}/update-xscreensaver-hacks ]; then
    %{_sbindir}/update-xscreensaver-hacks || :
elif [ -x %{_bindir}/update-xscreensaver-hacks ]; then
    %{_bindir}/update-xscreensaver-hacks || :
fi

%files
%license LICENSE
%doc README.md
%{_libexecdir}/xscreensaver/mystify
%{_datadir}/xscreensaver/config/mystify.xml
%{_datadir}/xscreensaver/hacks.conf.d/xscreensaver-mystify.conf
%{_datadir}/applications/screensavers/xscreensaver-mystify.desktop
%{_mandir}/man6x/mystify.6x*

%changelog
* Fri Jul 31 2026 Reaper <JohnGrimmReaper@disroot.org> - 0.2.0-1
- Add initial Fedora and Enterprise Linux RPM packaging.
- Follow the EPEL modular XScreenSaver hack configuration layout.
