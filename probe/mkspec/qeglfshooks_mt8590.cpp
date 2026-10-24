//
// Copyright 2015,2016,2017 Sony Corporation
// WMPROBE additions: nw-a55 project
//
// Sony's qtbase/mkspecs/devices/linux-mt8590-g++/qeglfshooks_mt8590.cpp from
// its Qt 5.3.2 source release, which ships under the GNU LGPL 2.1 (with the
// Digia Qt LGPL Exception 1.1) or the GNU GPL 3.0. This copy, with our
// additions, is under the GNU GPL 3.0 (LICENSE at the repository root).
//

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <stdio.h>
#include "qeglfshooks.h"

#ifdef WMPROBE
#include "overlay.h"
#include "probe.h"
#endif

// Mali's fbdev window, normally from EGL/fbdev_window.h in the Mali SDK.
struct fbdev_window {
    unsigned short width;
    unsigned short height;
};

QT_BEGIN_NAMESPACE

class QEglFSmt8590Hooks : public QEglFSHooks
{
public:
#ifdef WMPROBE
    virtual void platformInit();
#endif
    virtual bool hasCapability(QPlatformIntegration::Capability cap) const;
    virtual QByteArray fbDeviceName() const;
    virtual EGLNativeDisplayType platformDisplay() const;
    virtual EGLNativeWindowType createNativeWindow(QPlatformWindow *platformWindow,
                                                   const QSize &size,
                                                   const QSurfaceFormat &format);
    virtual void destroyNativeWindow(EGLNativeWindowType window);
    virtual int screenDepth() const;
    virtual QDpi logicalDpi() const;
    virtual void waitForVSync() const;
    virtual bool filterConfig(EGLDisplay display, EGLConfig config) const;
};

#ifdef WMPROBE
void QEglFSmt8590Hooks::platformInit()
{
  QEglFSHooks::platformInit();
  overlayStart();
}
#endif

bool QEglFSmt8590Hooks::hasCapability(QPlatformIntegration::Capability cap) const
{
    switch (cap) {
        case QPlatformIntegration::ThreadedPixmaps:
        case QPlatformIntegration::OpenGL:
        case QPlatformIntegration::ThreadedOpenGL:
        case QPlatformIntegration::BufferQueueingOpenGL:
            return true;
        default:
            return false;
    }
}


QByteArray QEglFSmt8590Hooks::fbDeviceName() const
{
  QByteArray fbDev = qgetenv("QT_QPA_EGLFS_FB");
  if (fbDev.isEmpty())
      fbDev = QByteArrayLiteral("/dev/graphics/fb0");
  return fbDev;
}

EGLNativeDisplayType QEglFSmt8590Hooks::platformDisplay() const
{
  return (EGLNativeDisplayType)framebufferIndex();
}

EGLNativeWindowType QEglFSmt8590Hooks::createNativeWindow(QPlatformWindow *platformWindow,
                                               const QSize &size,
                                               const QSurfaceFormat &format)
{
#ifdef WMPROBE
  probeStart();
#endif
  fbdev_window *window = new fbdev_window();
  window->width = size.width();
  window->height = size.height();

  return (EGLNativeWindowType)window;
}


void QEglFSmt8590Hooks::destroyNativeWindow(EGLNativeWindowType window)
{
  if ( window != 0 ) {
    delete (fbdev_window *)window;
  } else {}
}

int QEglFSmt8590Hooks::screenDepth() const
{
  int screenDepth = QEglFSHooks::screenDepth();
  return screenDepth;
}


QDpi QEglFSmt8590Hooks::logicalDpi() const
{
  QDpi retDpi = QEglFSHooks::logicalDpi();
  return retDpi;
}

void QEglFSmt8590Hooks::waitForVSync() const
{
  QEglFSHooks::waitForVSync();
}

bool QEglFSmt8590Hooks::filterConfig(EGLDisplay display, EGLConfig config) const
{
  return QEglFSHooks::filterConfig(display, config);
}

QEglFSmt8590Hooks eglFSmt8590Hooks;
QEglFSHooks *platformHooks = &eglFSmt8590Hooks;

QT_END_NAMESPACE


