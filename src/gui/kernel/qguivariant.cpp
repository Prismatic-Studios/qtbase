/****************************************************************************
**
** Copyright (C) 2020 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the QtGui module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 3 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL3 included in the
** packaging of this file. Please review the following information to
** ensure the GNU Lesser General Public License version 3 requirements
** will be met: https://www.gnu.org/licenses/lgpl-3.0.html.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 2.0 or (at your option) the GNU General
** Public license version 3 or any later version approved by the KDE Free
** Qt Foundation. The licenses are as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL2 and LICENSE.GPL3
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-2.0.html and
** https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

// Gui types
#include "qbitmap.h"
#include "qbrush.h"
#include "qcolor.h"
#include "qcolorspace.h"
#include "qcursor.h"
#include "qfont.h"
#include "qimage.h"
#if QT_CONFIG(shortcut)
#  include "qkeysequence.h"
#endif
#include "qtransform.h"
#include "qpalette.h"
#include "qpen.h"
#include "qpixmap.h"
#include "qpolygon.h"
#include "qregion.h"
#include "qtextformat.h"
#include "qmatrix4x4.h"
#include "qvector2d.h"
#include "qvector3d.h"
#include "qvector4d.h"
#include "qquaternion.h"
#include "qicon.h"

// Core types
#include "qvariant.h"
#include "qbitarray.h"
#include "qbytearray.h"
#include "qdatastream.h"
#include "qdebug.h"
#include "qmap.h"
#include "qdatetime.h"
#include "qlist.h"
#include "qstring.h"
#include "qstringlist.h"
#include "qurl.h"
#include "qlocale.h"
#include "quuid.h"

#ifndef QT_NO_GEOM_VARIANT
#include "qsize.h"
#include "qpoint.h"
#include "qrect.h"
#include "qline.h"
#endif

#include <float.h>

#include "private/qvariant_p.h"
#include <private/qmetatype_p.h>

QT_BEGIN_NAMESPACE

Q_CORE_EXPORT const QVariant::Handler *qcoreVariantHandler();

namespace {
struct GuiTypesFilter {
    template<typename T>
    struct Acceptor {
        static const bool IsAccepted = QModulesPrivate::QTypeModuleInfo<T>::IsGui && QtMetaTypePrivate::TypeDefinition<T>::IsAvailable;
    };
};

static bool convert(const QVariant::Private *d, int t,
                 void *result, bool *ok)
{
    switch (t) {
    case QMetaType::QByteArray:
        if (d->type().id() == QMetaType::QColor) {
            const QColor *c = v_cast<QColor>(d);
            *static_cast<QByteArray *>(result) = c->name(c->alpha() != 255 ? QColor::HexArgb : QColor::HexRgb).toLatin1();
            return true;
        }
        break;
    case QMetaType::QString: {
        QString *str = static_cast<QString *>(result);
        switch (d->type().id()) {
#if QT_CONFIG(shortcut)
        case QMetaType::QKeySequence:
            *str = (*v_cast<QKeySequence>(d)).toString(QKeySequence::NativeText);
            return true;
#endif
        case QMetaType::QFont:
            *str = v_cast<QFont>(d)->toString();
            return true;
        case QMetaType::QColor: {
            const QColor *c = v_cast<QColor>(d);
            *str = c->name(c->alpha() != 255 ? QColor::HexArgb : QColor::HexRgb);
            return true;
        }
        default:
            break;
        }
        break;
    }
    case QMetaType::QPixmap:
        if (d->type().id() == QMetaType::QImage) {
            *static_cast<QPixmap *>(result) = QPixmap::fromImage(*v_cast<QImage>(d));
            return true;
        } else if (d->type().id() == QMetaType::QBitmap) {
            *static_cast<QPixmap *>(result) = *v_cast<QBitmap>(d);
            return true;
        } else if (d->type().id() == QMetaType::QBrush) {
            if (v_cast<QBrush>(d)->style() == Qt::TexturePattern) {
                *static_cast<QPixmap *>(result) = v_cast<QBrush>(d)->texture();
                return true;
            }
        }
        break;
    case QMetaType::QImage:
        if (d->type().id() == QMetaType::QPixmap) {
            *static_cast<QImage *>(result) = v_cast<QPixmap>(d)->toImage();
            return true;
        } else if (d->type().id() == QMetaType::QBitmap) {
            *static_cast<QImage *>(result) = v_cast<QBitmap>(d)->toImage();
            return true;
        }
        break;
    case QMetaType::QBitmap:
        if (d->type().id() == QMetaType::QPixmap) {
            *static_cast<QBitmap *>(result) = *v_cast<QPixmap>(d);
            return true;
        } else if (d->type().id() == QMetaType::QImage) {
            *static_cast<QBitmap *>(result) = QBitmap::fromImage(*v_cast<QImage>(d));
            return true;
        }
        break;
#if QT_CONFIG(shortcut)
    case QMetaType::Int:
        if (d->type().id() == QMetaType::QKeySequence) {
            const QKeySequence &seq = *v_cast<QKeySequence>(d);
            *static_cast<int *>(result) = seq.isEmpty() ? 0 : seq[0];
            return true;
        }
        break;
#endif
    case QMetaType::QFont:
        if (d->type().id() == QMetaType::QString) {
            QFont *f = static_cast<QFont *>(result);
            f->fromString(*v_cast<QString>(d));
            return true;
        }
        break;
    case QMetaType::QColor:
        if (d->type().id() == QMetaType::QString) {
            static_cast<QColor *>(result)->setNamedColor(*v_cast<QString>(d));
            return static_cast<QColor *>(result)->isValid();
        } else if (d->type().id() == QMetaType::QByteArray) {
            static_cast<QColor *>(result)->setNamedColor(QLatin1String(*v_cast<QByteArray>(d)));
            return true;
        } else if (d->type().id() == QMetaType::QBrush) {
            if (v_cast<QBrush>(d)->style() == Qt::SolidPattern) {
                *static_cast<QColor *>(result) = v_cast<QBrush>(d)->color();
                return true;
            }
        }
        break;
    case QMetaType::QBrush:
        if (d->type().id() == QMetaType::QColor) {
            *static_cast<QBrush *>(result) = QBrush(*v_cast<QColor>(d));
            return true;
        } else if (d->type().id() == QMetaType::QPixmap) {
            *static_cast<QBrush *>(result) = QBrush(*v_cast<QPixmap>(d));
            return true;
        }
        break;
#if QT_CONFIG(shortcut)
    case QMetaType::QKeySequence: {
        QKeySequence *seq = static_cast<QKeySequence *>(result);
        switch (d->type().id()) {
        case QMetaType::QString:
            *seq = QKeySequence(*v_cast<QString>(d));
            return true;
        case QMetaType::Int:
            *seq = QKeySequence(d->data.i);
            return true;
        default:
            break;
        }
        break;
    }
#endif
#ifndef QT_NO_ICON
    case QMetaType::QIcon: {
        if (ok)
            *ok = false;
        return false;
    }
#endif
    default:
        break;
    }
    return qcoreVariantHandler()->convert(d, t, result, ok);
}

#if !defined(QT_NO_DEBUG_STREAM)
static void streamDebug(QDebug dbg, const QVariant &v)
{
    QVariant::Private *d = const_cast<QVariant::Private *>(&v.data_ptr());
    QVariantDebugStream<GuiTypesFilter> stream(dbg, d);
    QMetaTypeSwitcher::switcher<void>(stream, d->type().id(), nullptr);
}
#endif

const QVariant::Handler qt_gui_variant_handler = {
    convert,
#if !defined(QT_NO_DEBUG_STREAM)
    streamDebug
#else
    nullptr
#endif
};

#define QT_IMPL_METATYPEINTERFACE_GUI_TYPES(MetaTypeName, MetaTypeId, RealName) \
    QT_METATYPE_INTERFACE_INIT(RealName),

static const struct : QMetaTypeModuleHelper
{
    QtPrivate::QMetaTypeInterface *interfaceForType(int type) const override {
        switch (type) {
            QT_FOR_EACH_STATIC_GUI_CLASS(QT_METATYPE_CONVERT_ID_TO_TYPE)
            default: return nullptr;
        }
    }
#ifndef QT_NO_DATASTREAM
    bool save(QDataStream &stream, int type, const void *data) const override {
        switch (type) {
            QT_FOR_EACH_STATIC_GUI_CLASS(QT_METATYPE_DATASTREAM_SAVE)
            default: return false;
        }
    }
    bool load(QDataStream &stream, int type, void *data) const override {
        switch (type) {
            QT_FOR_EACH_STATIC_GUI_CLASS(QT_METATYPE_DATASTREAM_LOAD)
            default: return false;
        }
    }
#endif

} qVariantGuiHelper;


#undef QT_IMPL_METATYPEINTERFACE_GUI_TYPES
} // namespace used to hide QVariant handler

extern Q_CORE_EXPORT const QMetaTypeModuleHelper *qMetaTypeGuiHelper;

void qRegisterGuiVariant()
{
    QVariantPrivate::registerHandler(QModulesPrivate::Gui, &qt_gui_variant_handler);
    qMetaTypeGuiHelper = &qVariantGuiHelper;
}
Q_CONSTRUCTOR_FUNCTION(qRegisterGuiVariant)

QT_END_NAMESPACE
