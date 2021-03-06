/****************************************************************************
**
** Copyright (C) 2021 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the test suite of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:GPL-EXCEPT$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3 as published by the Free Software
** Foundation with exceptions as appearing in the file LICENSE.GPL3-EXCEPT
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "logwidget.h"
#include <QApplication>
#include <QStyle>

#include <QtCore/QDebug>
#include <QtCore/QLibraryInfo>
#include <QtCore/QList>
#include <QtCore/qlogging.h>
#include <QtCore/QStringList>

#include <iostream>

LogWidget *LogWidget::m_instance = nullptr;
bool LogWidget::m_lineNumberingEnabled = true;
bool LogWidget::m_showMessageType = true;
int LogWidget::m_indent = 0;

LogWidget::LogWidget(QWidget *parent)
    : QPlainTextEdit(parent)
{
    LogWidget::m_instance = this;
    setReadOnly(true);
}

LogWidget::~LogWidget()
{
    LogWidget::m_instance = nullptr;
}

QString LogWidget::startupMessage()
{
    QString result;
    result += QLatin1String(QLibraryInfo::build());

    const QCoreApplication *coreApp = QCoreApplication::instance();
    if (qobject_cast<const QGuiApplication *>(coreApp)) {
        result += QLatin1Char(' ');
        result += QGuiApplication::platformName();
    }
    if (qobject_cast<const QApplication *>(coreApp)) {
        result += QLatin1Char(' ');
        result += QApplication::style()->objectName();
    }
    if (coreApp) {
        QStringList arguments = QCoreApplication::arguments();
        arguments.pop_front();
        if (!arguments.isEmpty()) {
            result += QLatin1Char('\n');
            result += arguments.join(QLatin1String(" "));
        }
    }
    return result;
}

static const QList<QString> &messageTypes()
{
    static QList<QString> result;
    if (result.isEmpty()) {
        result << QLatin1String("debug") << QLatin1String("warn")
            << QLatin1String("critical") << QLatin1String("fatal")
            << QLatin1String("info");
    }
    return result;
}

static void messageHandler(QtMsgType type, const QString &text)
{
    static int n = 0;
    QString message;
    if (LogWidget::lineNumberingEnabled())
        message.append(QString::fromLatin1("%1 ").arg(n, 4, 10, QLatin1Char('0')));
    if (LogWidget::showMessageType()) {
        message.append(messageTypes().at(type));
        message.append(QLatin1Char(' '));
    }
    for (int i = 0, ie = LogWidget::indent(); i < ie; ++i)
        message.append(QLatin1Char(' '));
    message.append(text);
    if (LogWidget *logWindow = LogWidget::instance())
        logWindow->appendText(message);
#ifdef Q_OS_WIN
    std::wcerr << reinterpret_cast<const wchar_t *>(message.utf16()) << L'\n';
#else
    std::cerr << qPrintable(message) << '\n';
#endif
    n++;
}

static void qtMessageHandler(QtMsgType type, const QMessageLogContext &, const QString &text)
{ messageHandler(type, text); }

void LogWidget::install()
{
    qInstallMessageHandler(qtMessageHandler);
    qInfo("%s", qPrintable(LogWidget::startupMessage()));
}

void LogWidget::uninstall() { qInstallMessageHandler(nullptr); }

void LogWidget::appendText(const QString &message)
{
    appendPlainText(message);
    ensureCursorVisible();
}
