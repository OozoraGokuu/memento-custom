#include <QTest>

#include "manager/playermanager.h"

class PlayerManagerTest : public QObject
{
    Q_OBJECT

private slots:
    void overlappingCueExtendsPendingBoundary()
    {
        QVERIFY(PlayerManager::isOverlappingNativeContinuation(
            10.0, 0.0, 12.0));

        // Once the earlier cue drops out, the remaining overlapping cue is
        // still part of the same window and must retain the extended end.
        QVERIFY(PlayerManager::isOverlappingNativeContinuation(
            12.0, 5.0, 12.0));
    }

    void adjacentAndDisjointCuesRemainBoundaries()
    {
        QVERIFY(!PlayerManager::isOverlappingNativeContinuation(
            10.0, 10.0, 12.0));
        QVERIFY(!PlayerManager::isOverlappingNativeContinuation(
            10.0, 11.0, 12.0));
    }

    void shorterOverlapDoesNotReplacePendingBoundary()
    {
        QVERIFY(!PlayerManager::isOverlappingNativeContinuation(
            10.0, 5.0, 8.0));
    }
};

QTEST_GUILESS_MAIN(PlayerManagerTest)

#include "test_playermanager.moc"
