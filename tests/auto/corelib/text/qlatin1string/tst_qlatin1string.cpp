/****************************************************************************
**
** Copyright (C) 2015 Klarälvdalens Datakonsult AB, a KDAB Group company, info@kdab.com, author Marc Mutz <marc.mutz@kdab.com>
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

#include <QTest>

#include <QString>

// Preserve QLatin1String-ness (QVariant(QLatin1String) creates a QVariant::String):
struct QLatin1StringContainer {
    QLatin1String l1;
};
QT_BEGIN_NAMESPACE
Q_DECLARE_TYPEINFO(QLatin1StringContainer, Q_RELOCATABLE_TYPE);
QT_END_NAMESPACE
Q_DECLARE_METATYPE(QLatin1StringContainer)

class tst_QLatin1String : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void constExpr();
    void construction();
    void userDefinedLiterals();
    void at();
    void arg() const;
    void midLeftRight();
    void nullString();
    void emptyString();
    void iterators();
    void relationalOperators_data();
    void relationalOperators();
};

void tst_QLatin1String::constExpr()
{
    // compile-time checks
    {
        constexpr QLatin1String l1s;
        static_assert(l1s.size() == 0);
        static_assert(l1s.isNull());
        static_assert(l1s.empty());
        static_assert(l1s.isEmpty());
        static_assert(l1s.latin1() == nullptr);

        constexpr QLatin1String l1s2(l1s.latin1(), l1s.latin1() + l1s.size());
        static_assert(l1s2.isNull());
        static_assert(l1s2.empty());
    }
    {
        constexpr QLatin1String l1s = nullptr;
        static_assert(l1s.size() == 0);
        static_assert(l1s.isNull());
        static_assert(l1s.empty());
        static_assert(l1s.isEmpty());
        static_assert(l1s.latin1() == nullptr);
    }
    {
        constexpr QLatin1String l1s("");
        static_assert(l1s.size() == 0);
        static_assert(!l1s.isNull());
        static_assert(l1s.empty());
        static_assert(l1s.isEmpty());
        static_assert(l1s.latin1() != nullptr);

        constexpr QLatin1String l1s2(l1s.latin1(), l1s.latin1() + l1s.size());
        static_assert(!l1s2.isNull());
        static_assert(l1s2.empty());
    }
    {
        static_assert(QLatin1String("Hello").size() == 5);
        constexpr QLatin1String l1s("Hello");
        static_assert(l1s.size() == 5);
        static_assert(!l1s.empty());
        static_assert(!l1s.isEmpty());
        static_assert(!l1s.isNull());
        static_assert(*l1s.latin1() == 'H');
        static_assert(l1s[0]      == QLatin1Char('H'));
        static_assert(l1s.at(0)   == QLatin1Char('H'));
        static_assert(l1s.front() == QLatin1Char('H'));
        static_assert(l1s.first() == QLatin1Char('H'));
        static_assert(l1s[4]      == QLatin1Char('o'));
        static_assert(l1s.at(4)   == QLatin1Char('o'));
        static_assert(l1s.back()  == QLatin1Char('o'));
        static_assert(l1s.last()  == QLatin1Char('o'));

        constexpr QLatin1String l1s2(l1s.latin1(), l1s.latin1() + l1s.size());
        static_assert(!l1s2.isNull());
        static_assert(!l1s2.empty());
        static_assert(l1s2.size() == 5);
    }
}

void tst_QLatin1String::construction()
{
    {
        const char str[6] = "hello";
        QLatin1String l1s(str);
        QCOMPARE(l1s.size(), 5);
        QCOMPARE(l1s.latin1(), reinterpret_cast<const void *>(&str[0]));
        QCOMPARE(l1s.latin1(), "hello");

        QByteArrayView helloView(str);
        helloView = helloView.first(4);
        l1s = QLatin1String(helloView);
        QCOMPARE(l1s.latin1(), helloView.data());
        QCOMPARE(l1s.latin1(), reinterpret_cast<const void *>(helloView.data()));
        QCOMPARE(l1s.size(), helloView.size());
    }

    {
        const QByteArray helloArray("hello");
        QLatin1String l1s(helloArray);
        QCOMPARE(l1s.latin1(), helloArray.data());
        QCOMPARE(l1s.size(), helloArray.size());

        QByteArrayView helloView(helloArray);
        helloView = helloView.first(4);
        l1s = QLatin1String(helloView);
        QCOMPARE(l1s.latin1(), helloView.data());
        QCOMPARE(l1s.size(), helloView.size());
    }
}

void tst_QLatin1String::userDefinedLiterals()
{
    {
        using namespace Qt::Literals::StringLiterals;

        auto str = "abcd"_L1;
        static_assert(std::is_same_v<decltype(str), QLatin1String>);
        QCOMPARE(str.size(), 4);
        QCOMPARE(str, QLatin1String("abcd"));
        QCOMPARE(str.latin1(), "abcd");
        QCOMPARE("abcd"_L1, str.latin1());
        QCOMPARE("M\xE5rten"_L1, QLatin1String("M\xE5rten"));

        auto ch = 'a'_L1;
        static_assert(std::is_same_v<decltype(ch), QLatin1Char>);
        QCOMPARE(ch, QLatin1Char('a'));
        QCOMPARE(ch.toLatin1(), 'a');
        QCOMPARE('a'_L1, ch.toLatin1());
        QCOMPARE('\xE5'_L1, QLatin1Char('\xE5'));
    }
    {
        using namespace Qt::Literals;

        auto str = "abcd"_L1;
        static_assert(std::is_same_v<decltype(str), QLatin1String>);
        QCOMPARE(str, QLatin1String("abcd"));

        auto ch = 'a'_L1;
        static_assert(std::is_same_v<decltype(ch), QLatin1Char>);
        QCOMPARE(ch, QLatin1Char('a'));
    }
    {
        using namespace Qt;

        auto str = "abcd"_L1;
        static_assert(std::is_same_v<decltype(str), QLatin1String>);
        QCOMPARE(str, QLatin1String("abcd"));

        auto ch = 'a'_L1;
        static_assert(std::is_same_v<decltype(ch), QLatin1Char>);
        QCOMPARE(ch, QLatin1Char('a'));
    }
}

void tst_QLatin1String::at()
{
    const QLatin1String l1("Hello World");
    QCOMPARE(l1.at(0), QLatin1Char('H'));
    QCOMPARE(l1.at(l1.size() - 1), QLatin1Char('d'));
    QCOMPARE(l1[0], QLatin1Char('H'));
    QCOMPARE(l1[l1.size() - 1], QLatin1Char('d'));
}

void tst_QLatin1String::arg() const
{
#define CHECK1(pattern, arg1, expected) \
    do { \
        auto p = QLatin1String(pattern); \
        QCOMPARE(p.arg(QLatin1String(arg1)), expected); \
        QCOMPARE(p.arg(u"" arg1), expected); \
        QCOMPARE(p.arg(QStringLiteral(arg1)), expected); \
        QCOMPARE(p.arg(QString(QLatin1String(arg1))), expected); \
    } while (false) \
    /*end*/
#define CHECK2(pattern, arg1, arg2, expected) \
    do { \
        auto p = QLatin1String(pattern); \
        QCOMPARE(p.arg(QLatin1String(arg1), QLatin1String(arg2)), expected); \
        QCOMPARE(p.arg(u"" arg1, QLatin1String(arg2)), expected); \
        QCOMPARE(p.arg(QLatin1String(arg1), u"" arg2), expected); \
        QCOMPARE(p.arg(u"" arg1, u"" arg2), expected); \
    } while (false) \
    /*end*/

    CHECK1("", "World", "");
    CHECK1("%1", "World", "World");
    CHECK1("!%1?", "World", "!World?");
    CHECK1("%1%1", "World", "WorldWorld");
    CHECK1("%1%2", "World", "World%2");
    CHECK1("%2%1", "World", "%2World");

    CHECK2("", "Hello", "World", "");
    CHECK2("%1", "Hello", "World", "Hello");
    CHECK2("!%1, %2?", "Hello", "World", "!Hello, World?");
    CHECK2("%1%1", "Hello", "World", "HelloHello");
    CHECK2("%1%2", "Hello", "World", "HelloWorld");
    CHECK2("%2%1", "Hello", "World", "WorldHello");

#undef CHECK2
#undef CHECK1

    QCOMPARE(QLatin1String(" %2 %2 %1 %3 ").arg(QLatin1Char('c'), QChar::CarriageReturn, u'C'), " \r \r c C ");
}

void tst_QLatin1String::midLeftRight()
{
    const QLatin1String l1("Hello World");
    QCOMPARE(l1.mid(0),            l1);
    QCOMPARE(l1.mid(0, l1.size()), l1);
    QCOMPARE(l1.left(l1.size()),   l1);
    QCOMPARE(l1.right(l1.size()),  l1);

    QCOMPARE(l1.mid(6), QLatin1String("World"));
    QCOMPARE(l1.mid(6, 5), QLatin1String("World"));
    QCOMPARE(l1.right(5), QLatin1String("World"));

    QCOMPARE(l1.mid(6, 1), QLatin1String("W"));
    QCOMPARE(l1.right(5).left(1), QLatin1String("W"));

    QCOMPARE(l1.left(5), QLatin1String("Hello"));
}

void tst_QLatin1String::nullString()
{
    // default ctor
    {
        QLatin1String l1;
        QCOMPARE(static_cast<const void*>(l1.data()), static_cast<const void*>(nullptr));
        QCOMPARE(l1.size(), 0);

        QString s = l1;
        QVERIFY(s.isNull());
    }

    // from nullptr
    {
        const char *null = nullptr;
        QLatin1String l1(null);
        QCOMPARE(static_cast<const void*>(l1.data()), static_cast<const void*>(nullptr));
        QCOMPARE(l1.size(), 0);

        QString s = l1;
        QVERIFY(s.isNull());
    }

    // from null QByteArray
    {
        const QByteArray null;
        QVERIFY(null.isNull());

        QLatin1String l1(null);
        QEXPECT_FAIL("", "null QByteArrays become non-null QLatin1Strings...", Continue);
        QCOMPARE(static_cast<const void*>(l1.data()), static_cast<const void*>(nullptr));
        QCOMPARE(l1.size(), 0);

        QString s = l1;
        QEXPECT_FAIL("", "null QByteArrays become non-null QLatin1Strings become non-null QStrings...", Continue);
        QVERIFY(s.isNull());
    }
}

void tst_QLatin1String::emptyString()
{
    {
        const char *empty = "";
        QLatin1String l1(empty);
        QCOMPARE(static_cast<const void*>(l1.data()), static_cast<const void*>(empty));
        QCOMPARE(l1.size(), 0);

        QString s = l1;
        QVERIFY(s.isEmpty());
        QVERIFY(!s.isNull());
    }

    {
        const char *notEmpty = "foo";
        QLatin1String l1(notEmpty, qsizetype(0));
        QCOMPARE(static_cast<const void*>(l1.data()), static_cast<const void*>(notEmpty));
        QCOMPARE(l1.size(), 0);

        QString s = l1;
        QVERIFY(s.isEmpty());
        QVERIFY(!s.isNull());
    }

    {
        const QByteArray empty = "";
        QLatin1String l1(empty);
        QCOMPARE(static_cast<const void*>(l1.data()), static_cast<const void*>(empty.constData()));
        QCOMPARE(l1.size(), 0);

        QString s = l1;
        QVERIFY(s.isEmpty());
        QVERIFY(!s.isNull());
    }
}

void tst_QLatin1String::iterators()
{
    QLatin1String hello("hello");
    QLatin1String olleh("olleh");

    QVERIFY(std::equal(hello.begin(), hello.end(),
                       olleh.rbegin()));
    QVERIFY(std::equal(hello.rbegin(), hello.rend(),
                       QT_MAKE_CHECKED_ARRAY_ITERATOR(olleh.begin(), olleh.size())));

    QVERIFY(std::equal(hello.cbegin(), hello.cend(),
                       olleh.rbegin()));
    QVERIFY(std::equal(hello.crbegin(), hello.crend(),
                       QT_MAKE_CHECKED_ARRAY_ITERATOR(olleh.begin(), olleh.size())));
}

void tst_QLatin1String::relationalOperators_data()
{
    QTest::addColumn<QLatin1StringContainer>("lhs");
    QTest::addColumn<int>("lhsOrderNumber");
    QTest::addColumn<QLatin1StringContainer>("rhs");
    QTest::addColumn<int>("rhsOrderNumber");

    struct Data {
        QLatin1String l1;
        int order;
    } data[] = {
        { QLatin1String(),     0 },
        { QLatin1String(""),   0 },
        { QLatin1String("a"),  1 },
        { QLatin1String("aa"), 2 },
        { QLatin1String("b"),  3 },
    };

    for (Data *lhs = data; lhs != data + sizeof data / sizeof *data; ++lhs) {
        for (Data *rhs = data; rhs != data + sizeof data / sizeof *data; ++rhs) {
            QLatin1StringContainer l = { lhs->l1 }, r = { rhs->l1 };
            QTest::addRow("\"%s\" <> \"%s\"",
                          lhs->l1.data() ? lhs->l1.data() : "nullptr",
                          rhs->l1.data() ? rhs->l1.data() : "nullptr")
                << l << lhs->order << r << rhs->order;
        }
    }
}

void tst_QLatin1String::relationalOperators()
{
    QFETCH(QLatin1StringContainer, lhs);
    QFETCH(int, lhsOrderNumber);
    QFETCH(QLatin1StringContainer, rhs);
    QFETCH(int, rhsOrderNumber);

#define CHECK(op) \
    QCOMPARE(lhs.l1 op rhs.l1, lhsOrderNumber op rhsOrderNumber) \
    /*end*/
    CHECK(==);
    CHECK(!=);
    CHECK(< );
    CHECK(> );
    CHECK(<=);
    CHECK(>=);
#undef CHECK
}

QTEST_APPLESS_MAIN(tst_QLatin1String)

#include "tst_qlatin1string.moc"
