%ifarch x86_64
%define usrlib /usr/lib64
%define package %{_appname}64
%else
%define usrlib /usr/lib
%define package %{_appname}
%endif
%define _XINIT /etc/X11/xinit/xinitrc.d/vglclient
%define _XINITSSL /etc/X11/xinit/xinitrc.d/vglclient_ssl
%define _DAEMON /usr/bin/vglclient_daemon
%define _DAEMONSSL /usr/bin/vglclient_ssldaemon
%define _POSTSESSION /etc/X11/gdm/PostSession/Default

Summary: A non-intrusive remote rendering package
Name: %{package}
Version: %{_version}
Vendor: The VirtualGL Project
URL: http://virtualgl.sourceforge.net
Group: Applications/Graphics
Release: %{_build}
License: wxWindows Library License, v3
BuildRoot: %{_blddir}/%{name}-buildroot
Prereq: /sbin/ldconfig, /usr/bin/perl
Provides: %{name} = %{version}-%{release}

%description
%{_appname} is a non-intrusive remote rendering package that
allows any 3D OpenGL application to be remotely displayed over a network
in true thin client fashion without modifying the application.  %{_appname}
relies upon X11 to send the GUI and receive input events, and it sends the
OpenGL pixels separately using a motion-JPEG style protocol (and a high-speed
MMX/SSE-aware JPEG codec.)  The OpenGL pixels are composited into the
X window on the client.  %{_appname} intercepts GLX calls on the server
to force the application to do its rendering in a Pbuffer, and it then
determines when to read back this Pbuffer, compress it, and send it to the
client display.

%{_appname} is based upon ideas presented in various academic papers on
this topic, including "A Generic Solution for Hardware-Accelerated Remote
Visualization" (Stegmaier, Magallon, Ertl 2002) and "A Framework for
Interactive Hardware Accelerated Remote 3D-Visualization" (Engel, Sommer,
Ertl 2000.)

%install
rm -rf $RPM_BUILD_ROOT
mkdir -p $RPM_BUILD_ROOT/usr/bin
mkdir -p $RPM_BUILD_ROOT%{usrlib}
mkdir -p $RPM_BUILD_ROOT/opt/%{package}/bin
mkdir -p $RPM_BUILD_ROOT/opt/%{package}/include
mkdir -p $RPM_BUILD_ROOT/opt/%{package}/samples

%ifarch x86_64
install -m 755 %{_bindir}/vglrun64 $RPM_BUILD_ROOT/usr/bin/vglrun64
install -m 755 %{_bindir}/vglrun64 $RPM_BUILD_ROOT/usr/bin/rrlaunch64
%else
mkdir -p $RPM_BUILD_ROOT/etc
mkdir -p $RPM_BUILD_ROOT/etc/rc.d/init.d

install -m 755 %{_bindir}/vglclient $RPM_BUILD_ROOT/usr/bin/vglclient
install -m 755 %{_bindir}/vglrun $RPM_BUILD_ROOT/usr/bin/vglrun
install -m 755 %{_bindir}/vglrun $RPM_BUILD_ROOT/usr/bin/rrlaunch
install -m 755 rr/rrxclient.sh $RPM_BUILD_ROOT%{_DAEMON}
install -m 755 rr/rrxclient_ssl.sh $RPM_BUILD_ROOT%{_DAEMONSSL}
install -m 755 rr/rrxclient_config $RPM_BUILD_ROOT/usr/bin/vglclient_config

install -m 755 %{_bindir}/tcbench $RPM_BUILD_ROOT/opt/%{package}/bin/tcbench

%endif

install -m 755 %{_bindir}/nettest $RPM_BUILD_ROOT/opt/%{package}/bin/nettest
install -m 755 %{_bindir}/cpustat $RPM_BUILD_ROOT/opt/%{package}/bin/cpustat

install -m 755 %{_libdir}/librrfaker.so $RPM_BUILD_ROOT%{usrlib}/librrfaker.so
install -m 755 %{_libdir}/libturbojpeg.so $RPM_BUILD_ROOT%{usrlib}/libturbojpeg.so
install -m 755 %{_libdir}/librr.so $RPM_BUILD_ROOT%{usrlib}/librr.so

install -m 644 rr/rr.h  $RPM_BUILD_ROOT/opt/%{package}/include
install -m 644 samples/rrglxgears.c  $RPM_BUILD_ROOT/opt/%{package}/samples
%ifarch x86_64
install -m 644 samples/Makefile.linux64 $RPM_BUILD_ROOT/opt/%{package}/samples/Makefile
%else
install -m 644 samples/Makefile.linux $RPM_BUILD_ROOT/opt/%{package}/samples/Makefile
%endif

chmod 644 LGPL.txt LICENSE.txt LICENSE-OpenSSL.txt doc/index.html doc/*.png doc/*.gif

%clean
rm -rf $RPM_BUILD_ROOT

%post
/sbin/ldconfig

%postun
/sbin/ldconfig

%ifarch x86_64
%else
%preun
%{_DAEMON} stop
%{_DAEMONSSL} stop
if [ -x %{_XINIT} ]; then rm %{_XINIT}; fi
if [ -x %{_XINITSSL} ]; then rm %{_XINITSSL}; fi
if [ -x %{_POSTSESSION} ]; then
	/usr/bin/perl -i -p -e "s:%{_DAEMON} stop\n::g;" %{_POSTSESSION}
	/usr/bin/perl -i -p -e "s:%{_DAEMONSSL} stop\n::g;" %{_POSTSESSION}
fi
%endif

%files -n %{package}
%defattr(-,root,root)
%doc LGPL.txt LICENSE.txt LICENSE-OpenSSL.txt doc/index.html doc/*.png doc/*.gif

%dir /opt/%{package}
%dir /opt/%{package}/bin
%dir /opt/%{package}/include
%dir /opt/%{package}/samples

%ifarch x86_64
/usr/bin/vglrun64
/usr/bin/rrlaunch64

%else
/usr/bin/vglclient
%{_DAEMON}
%{_DAEMONSSL}
/usr/bin/vglclient_config
/usr/bin/vglrun
/usr/bin/rrlaunch

/opt/%{package}/bin/tcbench

%endif
/opt/%{package}/bin/nettest
/opt/%{package}/bin/cpustat

/opt/%{package}/include/rr.h
/opt/%{package}/samples/rrglxgears.c
/opt/%{package}/samples/Makefile

%{usrlib}/librrfaker.so
%{usrlib}/libturbojpeg.so
%{usrlib}/librr.so

%changelog
