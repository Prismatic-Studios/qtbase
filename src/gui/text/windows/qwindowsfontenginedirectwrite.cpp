/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
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

#include "qwindowsfontenginedirectwrite_p.h"
#include "qwindowsfontdatabase_p.h"

#include <QtCore/QtEndian>
#include <QtCore/QVarLengthArray>
#include <QtCore/QFile>
#include <private/qstringiterator_p.h>
#include <QtCore/private/qsystemlibrary_p.h>
#include <QtCore/private/qwinregistry_p.h>
#include <QtGui/private/qguiapplication_p.h>
#include <qpa/qplatformintegration.h>
#include <QtGui/private/qhighdpiscaling_p.h>
#include <QtGui/qpainterpath.h>

#include <dwrite_2.h>

#include <d2d1.h>

QT_BEGIN_NAMESPACE

// Clang does not consider __declspec(nothrow) as nothrow
QT_WARNING_DISABLE_CLANG("-Wmicrosoft-exception-spec")

// Convert from design units to logical pixels
#define DESIGN_TO_LOGICAL(DESIGN_UNIT_VALUE) \
    QFixed::fromReal((qreal(DESIGN_UNIT_VALUE) / qreal(m_unitsPerEm)) * fontDef.pixelSize)

namespace {

    class GeometrySink: public IDWriteGeometrySink
    {
        Q_DISABLE_COPY_MOVE(GeometrySink)
    public:
        GeometrySink(QPainterPath *path)
            : m_refCount(0), m_path(path)
        {
            Q_ASSERT(m_path != 0);
        }
        virtual ~GeometrySink() = default;

        IFACEMETHOD_(void, AddBeziers)(const D2D1_BEZIER_SEGMENT *beziers, UINT bezierCount) override;
        IFACEMETHOD_(void, AddLines)(const D2D1_POINT_2F *points, UINT pointCount) override;
        IFACEMETHOD_(void, BeginFigure)(D2D1_POINT_2F startPoint, D2D1_FIGURE_BEGIN figureBegin) override;
        IFACEMETHOD(Close)() override;
        IFACEMETHOD_(void, EndFigure)(D2D1_FIGURE_END figureEnd) override;
        IFACEMETHOD_(void, SetFillMode)(D2D1_FILL_MODE fillMode) override;
        IFACEMETHOD_(void, SetSegmentFlags)(D2D1_PATH_SEGMENT vertexFlags) override;

        IFACEMETHOD_(unsigned long, AddRef)() override;
        IFACEMETHOD_(unsigned long, Release)() override;
        IFACEMETHOD(QueryInterface)(IID const &riid, void **ppvObject) override;

    private:
        inline static QPointF fromD2D1_POINT_2F(const D2D1_POINT_2F &inp)
        {
            return QPointF(inp.x, inp.y);
        }

        unsigned long m_refCount;
        QPointF m_startPoint;
        QPainterPath *m_path;
    };

    void GeometrySink::AddBeziers(const D2D1_BEZIER_SEGMENT *beziers,
                                  UINT bezierCount)
    {
        for (uint i=0; i<bezierCount; ++i) {
            QPointF c1 = fromD2D1_POINT_2F(beziers[i].point1);
            QPointF c2 = fromD2D1_POINT_2F(beziers[i].point2);
            QPointF p2 = fromD2D1_POINT_2F(beziers[i].point3);

            m_path->cubicTo(c1, c2, p2);
        }
    }

    void GeometrySink::AddLines(const D2D1_POINT_2F *points, UINT pointsCount)
    {
        for (uint i=0; i<pointsCount; ++i)
            m_path->lineTo(fromD2D1_POINT_2F(points[i]));
    }

    void GeometrySink::BeginFigure(D2D1_POINT_2F startPoint,
                                   D2D1_FIGURE_BEGIN /*figureBegin*/)
    {
        m_startPoint = fromD2D1_POINT_2F(startPoint);
        m_path->moveTo(m_startPoint);
    }

    IFACEMETHODIMP GeometrySink::Close()
    {
        return E_NOTIMPL;
    }

    void GeometrySink::EndFigure(D2D1_FIGURE_END figureEnd)
    {
        if (figureEnd == D2D1_FIGURE_END_CLOSED)
            m_path->closeSubpath();
    }

    void GeometrySink::SetFillMode(D2D1_FILL_MODE fillMode)
    {
        m_path->setFillRule(fillMode == D2D1_FILL_MODE_ALTERNATE
                            ? Qt::OddEvenFill
                            : Qt::WindingFill);
    }

    void GeometrySink::SetSegmentFlags(D2D1_PATH_SEGMENT /*vertexFlags*/)
    {
        /* Not implemented */
    }

    IFACEMETHODIMP_(unsigned long) GeometrySink::AddRef()
    {
        return InterlockedIncrement(&m_refCount);
    }

    IFACEMETHODIMP_(unsigned long) GeometrySink::Release()
    {
        unsigned long newCount = InterlockedDecrement(&m_refCount);
        if (newCount == 0)
        {
            delete this;
            return 0;
        }

        return newCount;
    }

    IFACEMETHODIMP GeometrySink::QueryInterface(IID const &riid, void **ppvObject)
    {
        if (__uuidof(IDWriteGeometrySink) == riid) {
            *ppvObject = this;
        } else if (__uuidof(IUnknown) == riid) {
            *ppvObject = this;
        } else {
            *ppvObject = NULL;
            return E_FAIL;
        }

        AddRef();
        return S_OK;
    }

}

static DWRITE_MEASURING_MODE renderModeToMeasureMode(DWRITE_RENDERING_MODE renderMode)
{
    switch (renderMode) {
    case DWRITE_RENDERING_MODE_GDI_CLASSIC:
        return DWRITE_MEASURING_MODE_GDI_CLASSIC;
    case DWRITE_RENDERING_MODE_GDI_NATURAL:
        return DWRITE_MEASURING_MODE_GDI_NATURAL;
    default:
        return DWRITE_MEASURING_MODE_NATURAL;
    }
}

static DWRITE_RENDERING_MODE hintingPreferenceToRenderingMode(const QFontDef &fontDef)
{
    QFont::HintingPreference hintingPreference = QFont::HintingPreference(fontDef.hintingPreference);
    if (QHighDpiScaling::isActive() && hintingPreference == QFont::PreferDefaultHinting) {
        // Microsoft documentation recommends using asymmetric rendering for small fonts
        // at pixel size 16 and less, and symmetric for larger fonts.
        hintingPreference = fontDef.pixelSize > 16.0
                ? QFont::PreferNoHinting
                : QFont::PreferVerticalHinting;
    }

    switch (hintingPreference) {
    case QFont::PreferNoHinting:
        return DWRITE_RENDERING_MODE_CLEARTYPE_NATURAL_SYMMETRIC;
    case QFont::PreferVerticalHinting:
        return DWRITE_RENDERING_MODE_CLEARTYPE_NATURAL;
    default:
        return DWRITE_RENDERING_MODE_CLEARTYPE_GDI_CLASSIC;
    }
}

/*!
    \class QWindowsFontEngineDirectWrite
    \brief Windows font engine using Direct Write.
    \internal

    Font engine for subpixel positioned text on Windows Vista
    (with platform update) and later. If selected during
    configuration, the engine will be selected only when the hinting
    preference of a font is set to None or Vertical hinting, or
    when fontengine=directwrite is selected as platform option.
*/

QWindowsFontEngineDirectWrite::QWindowsFontEngineDirectWrite(IDWriteFontFace *directWriteFontFace,
                                                             qreal pixelSize,
                                                             const QSharedPointer<QWindowsFontEngineData> &d)
    : QFontEngine(DirectWrite)
    , m_fontEngineData(d)
    , m_directWriteFontFace(directWriteFontFace)
    , m_directWriteBitmapRenderTarget(0)
    , m_lineThickness(-1)
    , m_unitsPerEm(-1)
    , m_capHeight(-1)
    , m_xHeight(-1)
{
    qCDebug(lcQpaFonts) << __FUNCTION__ << pixelSize;

    Q_ASSERT(m_directWriteFontFace);

    m_fontEngineData->directWriteFactory->AddRef();
    m_directWriteFontFace->AddRef();

    fontDef.pixelSize = pixelSize;
    collectMetrics();
    cache_cost = (m_ascent.toInt() + m_descent.toInt()) * m_xHeight.toInt() * 2000;
}

QWindowsFontEngineDirectWrite::~QWindowsFontEngineDirectWrite()
{
    qCDebug(lcQpaFonts) << __FUNCTION__;

    m_fontEngineData->directWriteFactory->Release();
    m_directWriteFontFace->Release();

    if (m_directWriteBitmapRenderTarget != 0)
        m_directWriteBitmapRenderTarget->Release();

    if (!m_uniqueFamilyName.isEmpty()) {
        QPlatformFontDatabase *pfdb = QGuiApplicationPrivate::platformIntegration()->fontDatabase();
        static_cast<QWindowsFontDatabase *>(pfdb)->derefUniqueFont(m_uniqueFamilyName);
    }
}

#ifndef Q_CC_MINGW
typedef IDWriteLocalFontFileLoader QIdWriteLocalFontFileLoader;

static UUID uuidIdWriteLocalFontFileLoader()
{
    return __uuidof(IDWriteLocalFontFileLoader);
}
#else // !Q_CC_MINGW
DECLARE_INTERFACE_(QIdWriteLocalFontFileLoader, IDWriteFontFileLoader)
{
    STDMETHOD(GetFilePathLengthFromKey)(THIS_ void const *, UINT32, UINT32*) PURE;
    STDMETHOD(GetFilePathFromKey)(THIS_ void const *, UINT32, WCHAR *, UINT32) PURE;
    STDMETHOD(GetLastWriteTimeFromKey)(THIS_ void const *, UINT32, FILETIME *) PURE;
};

static UUID uuidIdWriteLocalFontFileLoader()
{
    static const UUID result = { 0xb2d9f3ec, 0xc9fe, 0x4a11, {0xa2, 0xec, 0xd8, 0x62, 0x8, 0xf7, 0xc0, 0xa2}};
    return result;
}
#endif // Q_CC_MINGW

QString QWindowsFontEngineDirectWrite::filenameFromFontFile(IDWriteFontFile *fontFile)
{
    IDWriteFontFileLoader *loader = nullptr;

    HRESULT hr = fontFile->GetLoader(&loader);
    if (FAILED(hr)) {
        qErrnoWarning("%s: GetLoader failed", __FUNCTION__);
        return QString();
    }

    QIdWriteLocalFontFileLoader *localLoader = nullptr;
    hr = loader->QueryInterface(uuidIdWriteLocalFontFileLoader(),
                                reinterpret_cast<void **>(&localLoader));

    const void *fontFileReferenceKey = nullptr;
    UINT32 fontFileReferenceKeySize = 0;
    if (SUCCEEDED(hr)) {
        hr = fontFile->GetReferenceKey(&fontFileReferenceKey,
                                       &fontFileReferenceKeySize);
        if (FAILED(hr))
            qErrnoWarning(hr, "%s: GetReferenceKey failed", __FUNCTION__);
    }

    UINT32 filePathLength = 0;
    if (SUCCEEDED(hr)) {
        hr = localLoader->GetFilePathLengthFromKey(fontFileReferenceKey,
                                                   fontFileReferenceKeySize,
                                                   &filePathLength);
        if (FAILED(hr))
            qErrnoWarning(hr, "GetFilePathLength failed", __FUNCTION__);
    }

    QString ret;
    if (SUCCEEDED(hr) && filePathLength > 0) {
        QVarLengthArray<wchar_t> filePath(filePathLength + 1);

        hr = localLoader->GetFilePathFromKey(fontFileReferenceKey,
                                             fontFileReferenceKeySize,
                                             filePath.data(),
                                             filePathLength + 1);
        if (FAILED(hr))
            qErrnoWarning(hr, "%s: GetFilePathFromKey failed", __FUNCTION__);
        else
            ret = QString::fromWCharArray(filePath.data());
    }

    if (localLoader != nullptr)
        localLoader->Release();

    if (loader != nullptr)
        loader->Release();
    return ret;
}

HFONT QWindowsFontEngineDirectWrite::createHFONT() const
{
    if (m_fontEngineData == nullptr || m_directWriteFontFace == nullptr)
        return NULL;

    LOGFONT lf;
    HRESULT hr = m_fontEngineData->directWriteGdiInterop->ConvertFontFaceToLOGFONT(m_directWriteFontFace,
                                                                                   &lf);
    if (SUCCEEDED(hr)) {
        lf.lfHeight = -qRound(fontDef.pixelSize);
        return CreateFontIndirect(&lf);
    } else {
        return NULL;
    }
}

void QWindowsFontEngineDirectWrite::initializeHeightMetrics() const
{
    DWRITE_FONT_METRICS metrics;
    m_directWriteFontFace->GetMetrics(&metrics);

    m_ascent = DESIGN_TO_LOGICAL(metrics.ascent);
    m_descent = DESIGN_TO_LOGICAL(metrics.descent);
    m_leading = DESIGN_TO_LOGICAL(metrics.lineGap);

    QFontEngine::initializeHeightMetrics();
}

void QWindowsFontEngineDirectWrite::collectMetrics()
{
    DWRITE_FONT_METRICS metrics;

    m_directWriteFontFace->GetMetrics(&metrics);
    m_unitsPerEm = metrics.designUnitsPerEm;

    m_lineThickness = DESIGN_TO_LOGICAL(metrics.underlineThickness);
    m_capHeight = DESIGN_TO_LOGICAL(metrics.capHeight);
    m_xHeight = DESIGN_TO_LOGICAL(metrics.xHeight);
    m_underlinePosition = DESIGN_TO_LOGICAL(metrics.underlinePosition);

    IDWriteFontFile *fontFile = nullptr;
    UINT32 numberOfFiles = 1;
    if (SUCCEEDED(m_directWriteFontFace->GetFiles(&numberOfFiles, &fontFile))) {
        m_faceId.filename = QFile::encodeName(filenameFromFontFile(fontFile));
        fontFile->Release();
    }

    QByteArray table = getSfntTable(MAKE_TAG('h', 'h', 'e', 'a'));
    const int advanceWidthMaxLocation = 10;
    if (table.size() >= advanceWidthMaxLocation + int(sizeof(quint16))) {
        quint16 advanceWidthMax = qFromBigEndian<quint16>(table.constData() + advanceWidthMaxLocation);
        m_maxAdvanceWidth = DESIGN_TO_LOGICAL(advanceWidthMax);
    }
}

QFixed QWindowsFontEngineDirectWrite::underlinePosition() const
{
    if (m_underlinePosition > 0)
        return m_underlinePosition;
    else
        return QFontEngine::underlinePosition();
}

QFixed QWindowsFontEngineDirectWrite::lineThickness() const
{
    if (m_lineThickness > 0)
        return m_lineThickness;
    else
        return QFontEngine::lineThickness();
}

bool QWindowsFontEngineDirectWrite::getSfntTableData(uint tag, uchar *buffer, uint *length) const
{
    bool ret = false;

    const void *tableData = 0;
    UINT32 tableSize;
    void *tableContext = 0;
    BOOL exists;
    HRESULT hr = m_directWriteFontFace->TryGetFontTable(qbswap<quint32>(tag),
                                                        &tableData, &tableSize,
                                                        &tableContext, &exists);
    if (SUCCEEDED(hr)) {
        if (exists) {
            ret = true;
            if (buffer && *length >= tableSize)
                memcpy(buffer, tableData, tableSize);
            *length = tableSize;
            Q_ASSERT(int(*length) > 0);
        }
        m_directWriteFontFace->ReleaseFontTable(tableContext);
    } else {
        qErrnoWarning("%s: TryGetFontTable failed", __FUNCTION__);
    }

    return ret;
}

QFixed QWindowsFontEngineDirectWrite::emSquareSize() const
{
    if (m_unitsPerEm > 0)
        return m_unitsPerEm;
    else
        return QFontEngine::emSquareSize();
}

glyph_t QWindowsFontEngineDirectWrite::glyphIndex(uint ucs4) const
{
    UINT16 glyphIndex;

    HRESULT hr = m_directWriteFontFace->GetGlyphIndicesW(&ucs4, 1, &glyphIndex);
    if (FAILED(hr)) {
        qErrnoWarning("%s: glyphIndex failed", __FUNCTION__);
        glyphIndex = 0;
    }

    return glyphIndex;
}

bool QWindowsFontEngineDirectWrite::stringToCMap(const QChar *str, int len, QGlyphLayout *glyphs,
                                                 int *nglyphs, QFontEngine::ShaperFlags flags) const
{
    Q_ASSERT(glyphs->numGlyphs >= *nglyphs);
    if (*nglyphs < len) {
        *nglyphs = len;
        return false;
    }

    QVarLengthArray<UINT32> codePoints(len);
    int actualLength = 0;
    QStringIterator it(str, str + len);
    while (it.hasNext())
        codePoints[actualLength++] = it.next();

    QVarLengthArray<UINT16> glyphIndices(actualLength);
    HRESULT hr = m_directWriteFontFace->GetGlyphIndicesW(codePoints.data(), actualLength,
                                                         glyphIndices.data());
    if (FAILED(hr)) {
        qErrnoWarning("%s: GetGlyphIndicesW failed", __FUNCTION__);
        return false;
    }

    for (int i = 0; i < actualLength; ++i)
        glyphs->glyphs[i] = glyphIndices.at(i);

    *nglyphs = actualLength;
    glyphs->numGlyphs = actualLength;

    if (!(flags & GlyphIndicesOnly))
        recalcAdvances(glyphs, {});

    return true;
}

QFontEngine::FaceId QWindowsFontEngineDirectWrite::faceId() const
{
    return m_faceId;
}

void QWindowsFontEngineDirectWrite::recalcAdvances(QGlyphLayout *glyphs, QFontEngine::ShaperFlags) const
{
    QVarLengthArray<UINT16> glyphIndices(glyphs->numGlyphs);

    // ### Caching?
    for(int i=0; i<glyphs->numGlyphs; i++)
        glyphIndices[i] = UINT16(glyphs->glyphs[i]);

    QVarLengthArray<DWRITE_GLYPH_METRICS> glyphMetrics(glyphIndices.size());

    HRESULT hr;
    DWRITE_RENDERING_MODE renderMode = hintingPreferenceToRenderingMode(fontDef);
    if (renderMode == DWRITE_RENDERING_MODE_GDI_CLASSIC || renderMode == DWRITE_RENDERING_MODE_GDI_NATURAL) {
        hr = m_directWriteFontFace->GetGdiCompatibleGlyphMetrics(float(fontDef.pixelSize),
                                                                 1.0f,
                                                                 NULL,
                                                                 TRUE,
                                                                 glyphIndices.data(),
                                                                 glyphIndices.size(),
                                                                 glyphMetrics.data());
    } else {
        hr = m_directWriteFontFace->GetDesignGlyphMetrics(glyphIndices.data(),
                                                          glyphIndices.size(),
                                                          glyphMetrics.data());
    }
    if (SUCCEEDED(hr)) {
        qreal stretch = fontDef.stretch != QFont::AnyStretch ? fontDef.stretch / 100.0 : 1.0;
        for (int i = 0; i < glyphs->numGlyphs; ++i)
            glyphs->advances[i] = DESIGN_TO_LOGICAL(glyphMetrics[i].advanceWidth * stretch);
    } else {
        qErrnoWarning("%s: GetDesignGlyphMetrics failed", __FUNCTION__);
    }
}

void QWindowsFontEngineDirectWrite::addGlyphsToPath(glyph_t *glyphs, QFixedPoint *positions, int nglyphs,
                                             QPainterPath *path, QTextItem::RenderFlags flags)
{
    Q_UNUSED(flags);
    QVarLengthArray<UINT16> glyphIndices(nglyphs);
    QVarLengthArray<DWRITE_GLYPH_OFFSET> glyphOffsets(nglyphs);
    QVarLengthArray<FLOAT> glyphAdvances(nglyphs);

    for (int i=0; i<nglyphs; ++i) {
        glyphIndices[i] = glyphs[i];
        glyphOffsets[i].advanceOffset  = positions[i].x.toReal();
        glyphOffsets[i].ascenderOffset = -positions[i].y.toReal();
        glyphAdvances[i] = 0.0;
    }

    GeometrySink geometrySink(path);
    HRESULT hr = m_directWriteFontFace->GetGlyphRunOutline(
                fontDef.pixelSize,
                glyphIndices.data(),
                glyphAdvances.data(),
                glyphOffsets.data(),
                nglyphs,
                false,
                false,
                &geometrySink
                );

    if (FAILED(hr))
        qErrnoWarning("%s: GetGlyphRunOutline failed", __FUNCTION__);
}

glyph_metrics_t QWindowsFontEngineDirectWrite::boundingBox(const QGlyphLayout &glyphs)
{
    if (glyphs.numGlyphs == 0)
        return glyph_metrics_t();
    QFixed w = 0;
    for (int i = 0; i < glyphs.numGlyphs; ++i)
        w += glyphs.effectiveAdvance(i);

    return glyph_metrics_t(0, -ascent(), w - lastRightBearing(glyphs), ascent() + descent(), w, 0);
}

glyph_metrics_t QWindowsFontEngineDirectWrite::boundingBox(glyph_t g)
{
    UINT16 glyphIndex = g;

    DWRITE_GLYPH_METRICS glyphMetrics;
    HRESULT hr = m_directWriteFontFace->GetDesignGlyphMetrics(&glyphIndex, 1, &glyphMetrics);
    if (SUCCEEDED(hr)) {
        QFixed advanceWidth = DESIGN_TO_LOGICAL(glyphMetrics.advanceWidth);
        QFixed leftSideBearing = DESIGN_TO_LOGICAL(glyphMetrics.leftSideBearing);
        QFixed rightSideBearing = DESIGN_TO_LOGICAL(glyphMetrics.rightSideBearing);
        QFixed advanceHeight = DESIGN_TO_LOGICAL(glyphMetrics.advanceHeight);
        QFixed verticalOriginY = DESIGN_TO_LOGICAL(glyphMetrics.verticalOriginY);
        QFixed topSideBearing = DESIGN_TO_LOGICAL(glyphMetrics.topSideBearing);
        QFixed bottomSideBearing = DESIGN_TO_LOGICAL(glyphMetrics.bottomSideBearing);
        QFixed width = advanceWidth - leftSideBearing - rightSideBearing;
        QFixed height = advanceHeight - topSideBearing - bottomSideBearing;
        return glyph_metrics_t(leftSideBearing,
                               -verticalOriginY + topSideBearing,
                               width,
                               height,
                               advanceWidth,
                               0);
    } else {
        qErrnoWarning("%s: GetDesignGlyphMetrics failed", __FUNCTION__);
    }

    return glyph_metrics_t();
}

QFixed QWindowsFontEngineDirectWrite::capHeight() const
{
    if (m_capHeight <= 0)
        return calculatedCapHeight();

    return m_capHeight;
}

QFixed QWindowsFontEngineDirectWrite::xHeight() const
{
    return m_xHeight;
}

qreal QWindowsFontEngineDirectWrite::maxCharWidth() const
{
    return m_maxAdvanceWidth.toReal();
}

QImage QWindowsFontEngineDirectWrite::alphaMapForGlyph(glyph_t glyph,
                                                       const QFixedPoint &subPixelPosition,
                                                       const QTransform &t)
{
    QImage im = imageForGlyph(glyph, subPixelPosition, glyphMargin(Format_A8), t);

    QImage alphaMap(im.width(), im.height(), QImage::Format_Alpha8);

    for (int y=0; y<im.height(); ++y) {
        const uint *src = reinterpret_cast<const uint *>(im.constScanLine(y));
        uchar *dst = alphaMap.scanLine(y);
        for (int x=0; x<im.width(); ++x) {
            *dst = 255 - (m_fontEngineData->pow_gamma[qGray(0xffffffff - *src)] * 255. / 2047.);
            ++dst;
            ++src;
        }
    }

    return alphaMap;
}

QImage QWindowsFontEngineDirectWrite::alphaMapForGlyph(glyph_t glyph,
                                                       const QFixedPoint &subPixelPosition)
{
    return alphaMapForGlyph(glyph, subPixelPosition, QTransform());
}

bool QWindowsFontEngineDirectWrite::supportsHorizontalSubPixelPositions() const
{
    return true;
}

QImage QWindowsFontEngineDirectWrite::imageForGlyph(glyph_t t,
                                                    const QFixedPoint &subPixelPosition,
                                                    int margin,
                                                    const QTransform &originalTransform,
                                                    const QColor &color)
{
    UINT16 glyphIndex = t;
    FLOAT glyphAdvance = 0;

    DWRITE_GLYPH_OFFSET glyphOffset;
    glyphOffset.advanceOffset = 0;
    glyphOffset.ascenderOffset = 0;

    DWRITE_GLYPH_RUN glyphRun;
    glyphRun.fontFace = m_directWriteFontFace;
    glyphRun.fontEmSize = fontDef.pixelSize;
    glyphRun.glyphCount = 1;
    glyphRun.glyphIndices = &glyphIndex;
    glyphRun.glyphAdvances = &glyphAdvance;
    glyphRun.isSideways = false;
    glyphRun.bidiLevel = 0;
    glyphRun.glyphOffsets = &glyphOffset;

    QTransform xform = originalTransform;
    if (fontDef.stretch != 100 && fontDef.stretch != QFont::AnyStretch)
        xform.scale(fontDef.stretch / 100.0, 1.0);

    DWRITE_MATRIX transform;
    transform.dx = subPixelPosition.x.toReal();
    transform.dy = 0;
    transform.m11 = xform.m11();
    transform.m12 = xform.m12();
    transform.m21 = xform.m21();
    transform.m22 = xform.m22();

    DWRITE_RENDERING_MODE renderMode = hintingPreferenceToRenderingMode(fontDef);
    DWRITE_MEASURING_MODE measureMode =
            renderModeToMeasureMode(renderMode);

    IDWriteGlyphRunAnalysis *glyphAnalysis = NULL;
    HRESULT hr = m_fontEngineData->directWriteFactory->CreateGlyphRunAnalysis(
                &glyphRun,
                1.0f,
                &transform,
                renderMode,
                measureMode,
                0.0, 0.0,
                &glyphAnalysis
                );

    if (SUCCEEDED(hr)) {
        RECT rect;
        glyphAnalysis->GetAlphaTextureBounds(DWRITE_TEXTURE_CLEARTYPE_3x1, &rect);

        if (rect.top == rect.bottom || rect.left == rect.right)
            return QImage();

        QRect boundingRect = QRect(QPoint(rect.left - margin,
                                          rect.top - margin),
                                   QPoint(rect.right + margin,
                                          rect.bottom + margin));


        const int width = boundingRect.width() - 1; // -1 due to Qt's off-by-one definition of a QRect
        const int height = boundingRect.height() - 1;

        QImage image;
        HRESULT hr = DWRITE_E_NOCOLOR;
        IDWriteColorGlyphRunEnumerator *enumerator = 0;
        IDWriteFactory2 *factory2 = nullptr;
        if (glyphFormat == QFontEngine::Format_ARGB
                && SUCCEEDED(m_fontEngineData->directWriteFactory->QueryInterface(__uuidof(IDWriteFactory2),
                                                                                  reinterpret_cast<void **>(&factory2)))) {
            hr = factory2->TranslateColorGlyphRun(0.0f,
                                                  0.0f,
                                                  &glyphRun,
                                                  NULL,
                                                  measureMode,
                                                  NULL,
                                                  0,
                                                  &enumerator);
            image = QImage(width, height, QImage::Format_ARGB32_Premultiplied);
            image.fill(0);
        } else  {
            image = QImage(width, height, QImage::Format_RGB32);
            image.fill(0xffffffff);
        }

        BOOL ok = true;

        if (SUCCEEDED(hr)) {
            while (SUCCEEDED(hr) && ok) {
                const DWRITE_COLOR_GLYPH_RUN *colorGlyphRun = 0;
                hr = enumerator->GetCurrentRun(&colorGlyphRun);
                if (FAILED(hr)) { // No colored runs, only outline
                    qErrnoWarning(hr, "%s: IDWriteColorGlyphRunEnumerator::GetCurrentRun failed", __FUNCTION__);
                    break;
                }

                IDWriteGlyphRunAnalysis *colorGlyphsAnalysis = NULL;
                hr = m_fontEngineData->directWriteFactory->CreateGlyphRunAnalysis(
                            &colorGlyphRun->glyphRun,
                            1.0f,
                            &transform,
                            renderMode,
                            measureMode,
                            0.0, 0.0,
                            &colorGlyphsAnalysis
                            );
                if (FAILED(hr)) {
                    qErrnoWarning(hr, "%s: CreateGlyphRunAnalysis failed for color run", __FUNCTION__);
                    break;
                }

                float r, g, b, a;
                if (colorGlyphRun->paletteIndex == 0xFFFF) {
                    r = float(color.redF());
                    g = float(color.greenF());
                    b = float(color.blueF());
                    a = float(color.alphaF());
                } else {
                    r = qBound(0.0f, colorGlyphRun->runColor.r, 1.0f);
                    g = qBound(0.0f, colorGlyphRun->runColor.g, 1.0f);
                    b = qBound(0.0f, colorGlyphRun->runColor.b, 1.0f);
                    a = qBound(0.0f, colorGlyphRun->runColor.a, 1.0f);
                }

                if (!qFuzzyIsNull(a)) {
                    renderGlyphRun(&image,
                                   r,
                                   g,
                                   b,
                                   a,
                                   colorGlyphsAnalysis,
                                   boundingRect);
                }
                colorGlyphsAnalysis->Release();

                hr = enumerator->MoveNext(&ok);
                if (FAILED(hr)) {
                    qErrnoWarning(hr, "%s: IDWriteColorGlyphRunEnumerator::MoveNext failed", __FUNCTION__);
                    break;
                }
            }
        } else {
            float r, g, b, a;
            if (glyphFormat == QFontEngine::Format_ARGB) {
                r = float(color.redF());
                g = float(color.greenF());
                b = float(color.blueF());
                a = float(color.alphaF());
            } else {
                r = g = b = a = 0.0;
            }

            renderGlyphRun(&image,
                           r,
                           g,
                           b,
                           a,
                           glyphAnalysis,
                           boundingRect);
        }

        glyphAnalysis->Release();
        return image;
    } else {
        qErrnoWarning(hr, "%s: CreateGlyphRunAnalysis failed", __FUNCTION__);
        return QImage();
    }
}


void QWindowsFontEngineDirectWrite::renderGlyphRun(QImage *destination,
                                                   float r,
                                                   float g,
                                                   float b,
                                                   float a,
                                                   IDWriteGlyphRunAnalysis *glyphAnalysis,
                                                   const QRect &boundingRect)
{
    const int width = destination->width();
    const int height = destination->height();

    r *= 255.0;
    g *= 255.0;
    b *= 255.0;

    const int size = width * height * 3;
    if (size > 0) {
        RECT rect;
        rect.left = boundingRect.left();
        rect.top = boundingRect.top();
        rect.right = boundingRect.right();
        rect.bottom = boundingRect.bottom();

        QVarLengthArray<BYTE, 1024> alphaValueArray(size);
        BYTE *alphaValues = alphaValueArray.data();
        memset(alphaValues, 0, size);

        HRESULT hr = glyphAnalysis->CreateAlphaTexture(DWRITE_TEXTURE_CLEARTYPE_3x1,
                                                       &rect,
                                                       alphaValues,
                                                       size);
        if (SUCCEEDED(hr)) {
            if (destination->hasAlphaChannel()) {
                for (int y = 0; y < height; ++y) {
                    uint *dest = reinterpret_cast<uint *>(destination->scanLine(y));
                    BYTE *src = alphaValues + width * 3 * y;

                    for (int x = 0; x < width; ++x) {
                        float redAlpha = a * *src++ / 255.0;
                        float greenAlpha = a * *src++ / 255.0;
                        float blueAlpha = a * *src++ / 255.0;
                        float averageAlpha = (redAlpha + greenAlpha + blueAlpha) / 3.0;

                        QRgb currentRgb = dest[x];
                        dest[x] = qRgba(qRound(qRed(currentRgb) * (1.0 - averageAlpha) + averageAlpha * r),
                                        qRound(qGreen(currentRgb) * (1.0 - averageAlpha) + averageAlpha * g),
                                        qRound(qBlue(currentRgb) * (1.0 - averageAlpha) + averageAlpha * b),
                                        qRound(qAlpha(currentRgb) * (1.0 - averageAlpha) + averageAlpha * 255));
                    }
                }

            } else {
                for (int y = 0; y < height; ++y) {
                    uint *dest = reinterpret_cast<uint *>(destination->scanLine(y));
                    BYTE *src = alphaValues + width * 3 * y;

                    for (int x = 0; x < width; ++x) {
                        dest[x] = *(src + 0) << 16
                                | *(src + 1) << 8
                                | *(src + 2);

                        src += 3;
                    }
                }
            }
        } else {
            qErrnoWarning("%s: CreateAlphaTexture failed", __FUNCTION__);
        }
    } else {
        glyphAnalysis->Release();
        qWarning("%s: Glyph has no bounds", __FUNCTION__);
    }
}

QImage QWindowsFontEngineDirectWrite::alphaRGBMapForGlyph(glyph_t t,
                                                          const QFixedPoint &subPixelPosition,
                                                          const QTransform &xform)
{
    QImage mask = imageForGlyph(t,
                                subPixelPosition,
                                glyphMargin(QFontEngine::Format_A32),
                                xform);

    return mask.depth() == 32
           ? mask
           : mask.convertToFormat(QImage::Format_RGB32);
}

QFontEngine *QWindowsFontEngineDirectWrite::cloneWithSize(qreal pixelSize) const
{
    QWindowsFontEngineDirectWrite *fontEngine = new QWindowsFontEngineDirectWrite(m_directWriteFontFace,
                                                                                  pixelSize,
                                                                                  m_fontEngineData);

    fontEngine->fontDef = fontDef;
    fontEngine->fontDef.pixelSize = pixelSize;
    if (!m_uniqueFamilyName.isEmpty()) {
        fontEngine->setUniqueFamilyName(m_uniqueFamilyName);
        QPlatformFontDatabase *pfdb = QGuiApplicationPrivate::platformIntegration()->fontDatabase();
        static_cast<QWindowsFontDatabase *>(pfdb)->refUniqueFont(m_uniqueFamilyName);
    }

    return fontEngine;
}

Qt::HANDLE QWindowsFontEngineDirectWrite::handle() const
{
    return m_directWriteFontFace;
}

void QWindowsFontEngineDirectWrite::initFontInfo(const QFontDef &request,
                                                 int dpi)
{
    fontDef = request;

    if (fontDef.pointSize < 0)
        fontDef.pointSize = fontDef.pixelSize * 72. / dpi;
    else if (fontDef.pixelSize == -1)
        fontDef.pixelSize = qRound(fontDef.pointSize * dpi / 72.);
}

QString QWindowsFontEngineDirectWrite::fontNameSubstitute(const QString &familyName)
{
    const QString substitute =
        QWinRegistryKey(HKEY_LOCAL_MACHINE,
                        LR"(Software\Microsoft\Windows NT\CurrentVersion\FontSubstitutes)")
        .stringValue(familyName);
    return substitute.isEmpty() ? familyName : substitute;
}

glyph_metrics_t QWindowsFontEngineDirectWrite::alphaMapBoundingBox(glyph_t glyph,
                                                                   const QFixedPoint &subPixelPosition,
                                                                   const QTransform &originalTransform,
                                                                   GlyphFormat format)
{
    Q_UNUSED(format);

    QTransform matrix = originalTransform;
    if (fontDef.stretch != 100 && fontDef.stretch != QFont::AnyStretch)
        matrix.scale(fontDef.stretch / 100.0, 1.0);

    glyph_metrics_t bbox = QFontEngine::boundingBox(glyph, matrix); // To get transformed advance

    UINT16 glyphIndex = glyph;
    FLOAT glyphAdvance = 0;

    DWRITE_GLYPH_OFFSET glyphOffset;
    glyphOffset.advanceOffset = 0;
    glyphOffset.ascenderOffset = 0;

    DWRITE_GLYPH_RUN glyphRun;
    glyphRun.fontFace = m_directWriteFontFace;
    glyphRun.fontEmSize = fontDef.pixelSize;
    glyphRun.glyphCount = 1;
    glyphRun.glyphIndices = &glyphIndex;
    glyphRun.glyphAdvances = &glyphAdvance;
    glyphRun.isSideways = false;
    glyphRun.bidiLevel = 0;
    glyphRun.glyphOffsets = &glyphOffset;

    DWRITE_MATRIX transform;
    transform.dx = subPixelPosition.x.toReal();
    transform.dy = 0;
    transform.m11 = matrix.m11();
    transform.m12 = matrix.m12();
    transform.m21 = matrix.m21();
    transform.m22 = matrix.m22();

    DWRITE_RENDERING_MODE renderMode = hintingPreferenceToRenderingMode(fontDef);
    DWRITE_MEASURING_MODE measureMode = renderModeToMeasureMode(renderMode);

    IDWriteGlyphRunAnalysis *glyphAnalysis = NULL;
    HRESULT hr = m_fontEngineData->directWriteFactory->CreateGlyphRunAnalysis(
                &glyphRun,
                1.0f,
                &transform,
                renderMode,
                measureMode,
                0.0, 0.0,
                &glyphAnalysis
                );

    if (SUCCEEDED(hr)) {
        RECT rect;
        glyphAnalysis->GetAlphaTextureBounds(DWRITE_TEXTURE_CLEARTYPE_3x1, &rect);
        glyphAnalysis->Release();

        int margin = glyphMargin(format);

        if (rect.left == rect.right || rect.top == rect.bottom)
            return glyph_metrics_t();

        return glyph_metrics_t(rect.left,
                               rect.top,
                               rect.right - rect.left + margin * 2,
                               rect.bottom - rect.top + margin * 2,
                               bbox.xoff, bbox.yoff);
    } else {
        return glyph_metrics_t();
    }
}

QImage QWindowsFontEngineDirectWrite::bitmapForGlyph(glyph_t glyph,
                                                     const QFixedPoint &subPixelPosition,
                                                     const QTransform &t,
                                                     const QColor &color)
{
    return imageForGlyph(glyph, subPixelPosition, glyphMargin(QFontEngine::Format_ARGB), t, color);
}

QT_END_NAMESPACE
