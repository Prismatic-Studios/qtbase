/****************************************************************************
**
** Copyright (C) 2015 The Qt Company Ltd.
** Contact: http://www.qt.io/licensing/
**
** This file is part of the plugins of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL3$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see http://www.qt.io/terms-conditions. For further
** information use the contact form at http://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 3 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPLv3 included in the
** packaging of this file. Please review the following information to
** ensure the GNU Lesser General Public License version 3 requirements
** will be met: https://www.gnu.org/licenses/lgpl.html.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 2.0 or later as published by the Free
** Software Foundation and appearing in the file LICENSE.GPL included in
** the packaging of this file. Please review the following information to
** ensure the GNU General Public License version 2.0 requirements will be
** met: http://www.gnu.org/licenses/gpl-2.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "qwinrtwindow.h"
#include "qwinrtscreen.h"
#include <private/qeventdispatcher_winrt_p.h>

#include <EGL/egl.h>
#define EGL_EGLEXT_PROTOTYPES
#include <EGL/eglext.h>

#include <qfunctions_winrt.h>
#include <qpa/qplatformscreen.h>
#include <qpa/qwindowsysteminterface.h>
#include <QtGui/QGuiApplication>
#include <QtGui/QOpenGLContext>
#include <QtGui/QWindow>
#include <QtPlatformSupport/private/qeglconvenience_p.h>

#include <functional>
#include <wrl.h>
#include <windows.foundation.h>
#include <windows.foundation.collections.h>
#include <windows.ui.xaml.h>
#include <windows.ui.xaml.controls.h>
#include <windows.ui.viewmanagement.h>

using namespace ABI::Windows::UI::ViewManagement;
using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;
using namespace ABI::Windows::Foundation;
using namespace ABI::Windows::Foundation::Collections;
using namespace ABI::Windows::UI;

QT_BEGIN_NAMESPACE

class QWinRTWindowPrivate
{
public:
    QWinRTScreen *screen;

    QSurfaceFormat surfaceFormat;
    QString windowTitle;

    ComPtr<Xaml::Controls::ISwapChainPanel> swapChainPanel;
};

QWinRTWindow::QWinRTWindow(QWindow *window)
    : QPlatformWindow(window)
    , d_ptr(new QWinRTWindowPrivate)
{
    Q_D(QWinRTWindow);

    d->screen = static_cast<QWinRTScreen *>(screen());
    setWindowFlags(window->flags());
    setWindowState(window->windowState());
    setWindowTitle(window->title());
    handleContentOrientationChange(window->contentOrientation());

    d->surfaceFormat.setAlphaBufferSize(0);
    d->surfaceFormat.setRedBufferSize(8);
    d->surfaceFormat.setGreenBufferSize(8);
    d->surfaceFormat.setBlueBufferSize(8);
    d->surfaceFormat.setDepthBufferSize(24);
    d->surfaceFormat.setStencilBufferSize(8);
    d->surfaceFormat.setRenderableType(QSurfaceFormat::OpenGLES);
    d->surfaceFormat.setSamples(1);
    d->surfaceFormat.setSwapBehavior(QSurfaceFormat::DoubleBuffer);

    HRESULT hr;
    hr = QEventDispatcherWinRT::runOnXamlThread([this, d]() {
        // Create a new swapchain and place it inside the canvas
        HRESULT hr;
        hr = RoActivateInstance(HString::MakeReference(RuntimeClass_Windows_UI_Xaml_Controls_SwapChainPanel).Get(),
                                &d->swapChainPanel);
        Q_ASSERT_SUCCEEDED(hr);
        ComPtr<Xaml::IUIElement> uiElement;
        hr = d->swapChainPanel.As(&uiElement);
        Q_ASSERT_SUCCEEDED(hr);

        ComPtr<Xaml::IDependencyObject> canvas = d->screen->canvas();
        ComPtr<Xaml::Controls::IPanel> panel;
        hr = canvas.As(&panel);
        Q_ASSERT_SUCCEEDED(hr);
        ComPtr<IVector<Xaml::UIElement *>> children;
        hr = panel->get_Children(&children);
        Q_ASSERT_SUCCEEDED(hr);
        hr = children->Append(uiElement.Get());
        Q_ASSERT_SUCCEEDED(hr);
        return S_OK;
    });
    Q_ASSERT_SUCCEEDED(hr);

    setGeometry(window->geometry());
}

QWinRTWindow::~QWinRTWindow()
{
    Q_D(QWinRTWindow);

    HRESULT hr;
    hr = QEventDispatcherWinRT::runOnXamlThread([d]() {
        HRESULT hr;
        ComPtr<Xaml::IDependencyObject> canvas = d->screen->canvas();
        ComPtr<Xaml::Controls::IPanel> panel;
        hr = canvas.As(&panel);
        Q_ASSERT_SUCCEEDED(hr);
        ComPtr<IVector<Xaml::UIElement *>> children;
        hr = panel->get_Children(&children);
        Q_ASSERT_SUCCEEDED(hr);

        ComPtr<Xaml::IUIElement> uiElement;
        hr = d->swapChainPanel.As(&uiElement);
        Q_ASSERT_SUCCEEDED(hr);
        quint32 index;
        boolean found;
        hr = children->IndexOf(uiElement.Get(), &index, &found);
        Q_ASSERT_SUCCEEDED(hr);
        if (found) {
            hr = children->RemoveAt(index);
            Q_ASSERT_SUCCEEDED(hr);
        }
        return S_OK;
    });
    RETURN_VOID_IF_FAILED("Failed to completely destroy window resources, likely because the application is shutting down");
}

QSurfaceFormat QWinRTWindow::format() const
{
    Q_D(const QWinRTWindow);
    return d->surfaceFormat;
}

bool QWinRTWindow::isActive() const
{
    Q_D(const QWinRTWindow);
    return d->screen->topWindow() == window();
}

bool QWinRTWindow::isExposed() const
{
    const bool exposed = isActive();
    return exposed;
}

void QWinRTWindow::setGeometry(const QRect &rect)
{
    Q_D(QWinRTWindow);

    if (window()->isTopLevel()) {
        QPlatformWindow::setGeometry(d->screen->geometry());
        QWindowSystemInterface::handleGeometryChange(window(), geometry());
    } else {
        QPlatformWindow::setGeometry(rect);
        QWindowSystemInterface::handleGeometryChange(window(), rect);
    }

    HRESULT hr;
    hr = QEventDispatcherWinRT::runOnXamlThread([this, d]() {
        HRESULT hr;
        ComPtr<Xaml::IFrameworkElement> frameworkElement;
        hr = d->swapChainPanel.As(&frameworkElement);
        Q_ASSERT_SUCCEEDED(hr);
        const QSizeF size = QSizeF(geometry().size()) / d->screen->scaleFactor();
        hr = frameworkElement->put_Width(size.width());
        Q_ASSERT_SUCCEEDED(hr);
        hr = frameworkElement->put_Height(size.height());
        Q_ASSERT_SUCCEEDED(hr);
        return S_OK;
    });
    Q_ASSERT_SUCCEEDED(hr);
}

void QWinRTWindow::setVisible(bool visible)
{
    Q_D(QWinRTWindow);
    if (!window()->isTopLevel())
        return;
    if (visible)
        d->screen->addWindow(window());
    else
        d->screen->removeWindow(window());
}

void QWinRTWindow::setWindowTitle(const QString &title)
{
    Q_D(QWinRTWindow);
    d->windowTitle = title;
    d->screen->updateWindowTitle();
}

void QWinRTWindow::raise()
{
    Q_D(QWinRTWindow);
    if (!window()->isTopLevel())
        return;
    d->screen->raise(window());
}

void QWinRTWindow::lower()
{
    Q_D(QWinRTWindow);
    if (!window()->isTopLevel())
        return;
    d->screen->lower(window());
}

WId QWinRTWindow::winId() const
{
    Q_D(const QWinRTWindow);
    return WId(d->swapChainPanel.Get());
}

qreal QWinRTWindow::devicePixelRatio() const
{
    return screen()->devicePixelRatio();
}

QT_END_NAMESPACE
