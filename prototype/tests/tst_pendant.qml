// R1: a pendant image that fails to load must be retried with a cache-busting
// query instead of staying invisible until the app restarts.
import QtQuick
import QtTest
import ".."

Item {
    id: root

    Component {
        id: avatarComponent
        PersonAvatar {}
    }

    TestCase {
        name: "PendantRetry"

        function test_brokenPendantRetries() {
            const avatar = avatarComponent.createObject(root, {
                width: 64,
                height: 64,
                pendantMinSize: 0,
                source: "",
                pendant: "file:///nonexistent-yunting-pendant.png",
            });
            verify(avatar !== null);
            tryVerify(() => avatar.pendantRetry >= 1, 5000);
            verify(avatar.pendantSource.indexOf("_retry=1") >= 0);
            verify(avatar.pendantRetry <= 2);
            avatar.destroy();
        }
    }
}
