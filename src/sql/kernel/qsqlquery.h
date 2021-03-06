/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the QtSql module of the Qt Toolkit.
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

#ifndef QSQLQUERY_H
#define QSQLQUERY_H

#include <QtSql/qtsqlglobal.h>
#include <QtSql/qsqldatabase.h>
#include <QtCore/qstring.h>
#include <QtCore/qvariant.h>

QT_BEGIN_NAMESPACE


class QSqlDriver;
class QSqlError;
class QSqlResult;
class QSqlRecord;
class QSqlQueryPrivate;


class Q_SQL_EXPORT QSqlQuery
{
public:
    explicit QSqlQuery(QSqlResult *r);
    explicit QSqlQuery(const QString& query = QString(), const QSqlDatabase &db = QSqlDatabase());
    explicit QSqlQuery(const QSqlDatabase &db);

#if QT_DEPRECATED_SINCE(6, 2)
    QT_DEPRECATED_VERSION_X_6_2("QSqlQuery is not meant to be copied. Use move construction instead.")
    QSqlQuery(const QSqlQuery &other);
    QT_DEPRECATED_VERSION_X_6_2("QSqlQuery is not meant to be copied. Use move assignment instead.")
    QSqlQuery& operator=(const QSqlQuery &other);
#else
    QSqlQuery(const QSqlQuery &other) = delete;
    QSqlQuery& operator=(const QSqlQuery &other) = delete;
#endif

    QSqlQuery(QSqlQuery &&other) noexcept
        : d(std::exchange(other.d, nullptr))
    {}
    QT_MOVE_ASSIGNMENT_OPERATOR_IMPL_VIA_MOVE_AND_SWAP(QSqlQuery)

    ~QSqlQuery();

    void swap(QSqlQuery &other) noexcept
    { qt_ptr_swap(d, other.d); }

    bool isValid() const;
    bool isActive() const;
    bool isNull(int field) const;
    bool isNull(const QString &name) const;
    int at() const;
    QString lastQuery() const;
    int numRowsAffected() const;
    QSqlError lastError() const;
    bool isSelect() const;
    int size() const;
    const QSqlDriver* driver() const;
    const QSqlResult* result() const;
    bool isForwardOnly() const;
    QSqlRecord record() const;

    void setForwardOnly(bool forward);
    bool exec(const QString& query);
    QVariant value(int i) const;
    QVariant value(const QString& name) const;

    void setNumericalPrecisionPolicy(QSql::NumericalPrecisionPolicy precisionPolicy);
    QSql::NumericalPrecisionPolicy numericalPrecisionPolicy() const;

    bool seek(int i, bool relative = false);
    bool next();
    bool previous();
    bool first();
    bool last();

    void clear();

    // prepared query support
    bool exec();
    enum BatchExecutionMode { ValuesAsRows, ValuesAsColumns };
    bool execBatch(BatchExecutionMode mode = ValuesAsRows);
    bool prepare(const QString& query);
    void bindValue(const QString& placeholder, const QVariant& val,
                   QSql::ParamType type = QSql::In);
    void bindValue(int pos, const QVariant& val, QSql::ParamType type = QSql::In);
    void addBindValue(const QVariant& val, QSql::ParamType type = QSql::In);
    QVariant boundValue(const QString& placeholder) const;
    QVariant boundValue(int pos) const;
    QVariantList boundValues() const;
    QString executedQuery() const;
    QVariant lastInsertId() const;
    void finish();
    bool nextResult();

private:
    QSqlQueryPrivate* d;
};

QT_END_NAMESPACE

#endif // QSQLQUERY_H
