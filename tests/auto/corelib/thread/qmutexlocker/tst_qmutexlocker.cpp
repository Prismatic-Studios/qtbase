/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
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

#include <QCoreApplication>
#include <QMutexLocker>
#include <QSemaphore>
#include <QThread>

class tst_QMutexLockerThread : public QThread
{
public:
    QRecursiveMutex mutex;
    QSemaphore semaphore, testSemaphore;

    void waitForTest()
    {
        semaphore.release();
        testSemaphore.acquire();
    }

};

class tst_QMutexLocker : public QObject
{
    Q_OBJECT

public:
    tst_QMutexLockerThread *thread;

    void waitForThread()
    {
        thread->semaphore.acquire();
    }
    void releaseThread()
    {
        thread->testSemaphore.release();
    }

private slots:
    void scopeTest();
    void unlockAndRelockTest();
    void lockerStateTest();
    void moveSemantics();
};

void tst_QMutexLocker::scopeTest()
{
    class ScopeTestThread : public tst_QMutexLockerThread
    {
    public:
        void run() override
        {
            waitForTest();

            {
                QMutexLocker locker(&mutex);
                QVERIFY(locker.isLocked());
                waitForTest();
            }

            waitForTest();
        }
    };

    thread = new ScopeTestThread;
    thread->start();

    waitForThread();
    // mutex should be unlocked before entering the scope that creates the QMutexLocker
    QVERIFY(thread->mutex.tryLock());
    thread->mutex.unlock();
    releaseThread();

    waitForThread();
    // mutex should be locked by the QMutexLocker
    QVERIFY(!thread->mutex.tryLock());
    releaseThread();

    waitForThread();
    // mutex should be unlocked when the QMutexLocker goes out of scope
    QVERIFY(thread->mutex.tryLock());
    thread->mutex.unlock();
    releaseThread();

    QVERIFY(thread->wait());

    delete thread;
    thread = nullptr;
}


void tst_QMutexLocker::unlockAndRelockTest()
{
    class UnlockAndRelockThread : public tst_QMutexLockerThread
    {
    public:
        void run() override
        {
            QMutexLocker locker(&mutex);
            QVERIFY(locker.isLocked());

            waitForTest();

            QVERIFY(locker.isLocked());
            locker.unlock();
            QVERIFY(!locker.isLocked());

            waitForTest();

            QVERIFY(!locker.isLocked());
            locker.relock();
            QVERIFY(locker.isLocked());

            waitForTest();

            QVERIFY(locker.isLocked());
        }
    };

    thread = new UnlockAndRelockThread;
    thread->start();

    waitForThread();
    // mutex should be locked by the QMutexLocker
    QVERIFY(!thread->mutex.tryLock());
    releaseThread();

    waitForThread();
    // mutex has been explicitly unlocked via QMutexLocker
    QVERIFY(thread->mutex.tryLock());
    thread->mutex.unlock();
    releaseThread();

    waitForThread();
    // mutex has been explicitly relocked via QMutexLocker
    QVERIFY(!thread->mutex.tryLock());
    releaseThread();

    QVERIFY(thread->wait());

    delete thread;
    thread = nullptr;
}

void tst_QMutexLocker::lockerStateTest()
{
    class LockerStateThread : public tst_QMutexLockerThread
    {
    public:
        void run() override
        {
            {
                QMutexLocker locker(&mutex);
                QVERIFY(locker.isLocked());

                locker.unlock();
                QVERIFY(!locker.isLocked());

                waitForTest();
                QVERIFY(!locker.isLocked());
            }

            waitForTest();
        }
    };

    thread = new LockerStateThread;
    thread->start();

    waitForThread();
    // even though we relock() after creating the QMutexLocker, it shouldn't lock the mutex more than once
    QVERIFY(thread->mutex.tryLock());
    thread->mutex.unlock();
    releaseThread();

    waitForThread();
    // if we call QMutexLocker::unlock(), its destructor should do nothing
    QVERIFY(thread->mutex.tryLock());
    thread->mutex.unlock();
    releaseThread();

    QVERIFY(thread->wait());

    delete thread;
    thread = nullptr;
}

void tst_QMutexLocker::moveSemantics()
{
    {
        QMutexLocker<QMutex> locker(nullptr);
        QVERIFY(!locker.isLocked());
        QCOMPARE(locker.mutex(), nullptr);

        QMutexLocker locker2(std::move(locker));
        QVERIFY(!locker.isLocked());
        QVERIFY(!locker2.isLocked());
        QCOMPARE(locker.mutex(), nullptr);
        QCOMPARE(locker2.mutex(), nullptr);
    }

    QMutex mutex;

    {
        QMutexLocker locker(&mutex);
        QVERIFY(locker.isLocked());
        QCOMPARE(locker.mutex(), &mutex);
        QVERIFY(!mutex.tryLock());

        QMutexLocker locker2(std::move(locker));
        QVERIFY(!locker.isLocked());
        QVERIFY(locker2.isLocked());
        QCOMPARE(locker.mutex(), nullptr);
        QCOMPARE(locker2.mutex(), &mutex);
        QVERIFY(!mutex.tryLock());
    }

    QVERIFY(mutex.tryLock());
    mutex.unlock();

    {
        QMutex mutex;
        QMutexLocker locker(&mutex);
        QVERIFY(locker.isLocked());
        QCOMPARE(locker.mutex(), &mutex);
        QVERIFY(!mutex.tryLock());

        locker.unlock();
        QVERIFY(!locker.isLocked());
        QCOMPARE(locker.mutex(), &mutex);
        QVERIFY(mutex.tryLock());
        mutex.unlock();

        QMutexLocker locker2(std::move(locker));
        QVERIFY(!locker.isLocked());
        QVERIFY(!locker2.isLocked());
        QCOMPARE(locker.mutex(), nullptr);
        QCOMPARE(locker2.mutex(), &mutex);
        QVERIFY(mutex.tryLock());
        mutex.unlock();

        locker2.relock();
        QVERIFY(!locker.isLocked());
        QVERIFY(locker2.isLocked());
        QCOMPARE(locker.mutex(), nullptr);
        QCOMPARE(locker2.mutex(), &mutex);
        QVERIFY(!mutex.tryLock());
    }

    QVERIFY(mutex.tryLock());
    mutex.unlock();
}

QTEST_MAIN(tst_QMutexLocker)
#include "tst_qmutexlocker.moc"
