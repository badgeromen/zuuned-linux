import QtQuick
import QtQuick.Controls.Basic
import QtTest
import Zuuned

TestCase {
    id: testCase
    name: "SpokenSetup"
    width: 1000; height: 900; visible: true
    when: windowShown
    Component { id: wizardComponent; OnboardingView { width: 1000; height: 900 } }
    Component {
        id: preferencesComponent
        PodcastPreferences {
            width: 480
            autoQueue: AppSettings.podcastAutoQueue
            episodeCount: AppSettings.podcastAutoQueueCount
            autoCleanup: AppSettings.podcastAutoCleanup
            onQueuePicked: value => AppSettings.podcastAutoQueue = value
            onCountPicked: value => AppSettings.podcastAutoQueueCount = value
            onCleanupPicked: value => AppSettings.podcastAutoCleanup = value
        }
    }
    function init() {
        failOnWarning(/.*(Binding loop|TypeError|ReferenceError|Unable to assign).*/)
        AppSettings.onboardingComplete = false
        AppSettings.podcastAutoQueue = false
        AppSettings.podcastAutoCleanup = false
        AppSettings.podcastAutoQueueCount = 10
    }
    function clickControl(parent, name) {
        const control = findChild(parent, name)
        verify(control !== null, name)
        mouseClick(control, control.width / 2, control.height / 2)
    }
    function test_sharedPreferencesAndInvalidCount() {
        const prefs = createTemporaryObject(preferencesComponent, testCase)
        verify(prefs !== null)
        clickControl(prefs, "podcastAutoQueueToggle")
        compare(AppSettings.podcastAutoQueue, true)
        clickControl(prefs, "podcastCount5")
        compare(AppSettings.podcastAutoQueueCount, 5)
        clickControl(prefs, "podcastCountCustom")
        const field = findChild(prefs, "podcastAutoQueueCount")
        field.text = "27"; field.editingFinished()
        compare(AppSettings.podcastAutoQueueCount, 27)
        field.text = "0"; field.editingFinished()
        compare(AppSettings.podcastAutoQueueCount, 27)
        compare(field.text, "27")
        clickControl(prefs, "podcastAutoCleanupToggle")
        compare(AppSettings.podcastAutoCleanup, true)
    }
    function test_setupStagesUntilFinishAndNavigates() {
        const wizard = createTemporaryObject(wizardComponent, testCase)
        verify(wizard !== null)
        wizard.step = wizard.stepPodcasts
        wait(20)
        clickControl(wizard, "podcastAutoQueueToggle")
        clickControl(wizard, "podcastCount5")
        clickControl(wizard, "podcastAutoCleanupToggle")
        compare(AppSettings.podcastAutoQueue, false)
        compare(AppSettings.podcastAutoCleanup, false)
        compare(AppSettings.podcastAutoQueueCount, 10)
        clickControl(wizard, "onboardingPodcastsNext")
        compare(wizard.step, wizard.stepBooks)
        clickControl(wizard, "onboardingBooksNext")
        compare(wizard.step, wizard.stepPlayer)
        wizard.step = wizard.stepPodcasts
        compare(wizard.draftPodcastCount, 5)
        wizard.complete()
        compare(AppSettings.podcastAutoQueue, true)
        compare(AppSettings.podcastAutoCleanup, true)
        compare(AppSettings.podcastAutoQueueCount, 5)
        compare(AppSettings.onboardingComplete, true)
    }
    function test_abandoningSetupDoesNotSave() {
        const wizard = createTemporaryObject(wizardComponent, testCase)
        wizard.draftPodcastQueue = true
        wizard.draftPodcastCleanup = true
        wizard.draftPodcastCount = 7
        wizard.podcastPreferencesEdited = true
        wizard.destroy()
        wait(0)
        compare(AppSettings.podcastAutoQueue, false)
        compare(AppSettings.podcastAutoCleanup, false)
        compare(AppSettings.podcastAutoQueueCount, 10)
        compare(AppSettings.onboardingComplete, false)
    }
    function test_uncommittedCustomCountSavesAtFinish() {
        const wizard = createTemporaryObject(wizardComponent, testCase)
        wizard.step = wizard.stepPodcasts
        wait(20)
        clickControl(wizard, "podcastCountCustom")
        const field = findChild(wizard, "podcastAutoQueueCount")
        field.forceActiveFocus()
        field.text = "23"
        wizard.complete()
        compare(AppSettings.podcastAutoQueueCount, 23)
    }
    function test_bookImportOnlyAdvancesAfterSuccess() {
        const wizard = createTemporaryObject(wizardComponent, testCase)
        wizard.step = wizard.stepBooks
        const sheet = findChild(wizard, "onboardingBookImport")
        const next = findChild(wizard, "onboardingBooksNext")
        compare(next.label, "skip")
        sheet.saving = true
        SpokenMediaService.operationFinished("import book folder", "", false, "Fixture error")
        compare(wizard.importedBooks, false)
        compare(next.label, "skip")
        sheet.saving = true
        SpokenMediaService.operationFinished("import book folder", "fixture", true, "")
        compare(wizard.importedBooks, true)
        compare(next.label, "next")
    }
    function test_smallWindowKeepsStepsInBounds() {
        const wizard = createTemporaryObject(wizardComponent, testCase, {width: 800, height: 600})
        for (const pair of [[wizard.stepPodcasts, "onboardingPodcastsNext"], [wizard.stepBooks, "onboardingBooksNext"]]) {
            wizard.step = pair[0]
            wait(20)
            const next = findChild(wizard, pair[1])
            const bottom = next.mapToItem(wizard, next.width, next.height)
            grabImage(wizard).save("/tmp/zuuned-setup-" + pair[0] + ".png")
            verify(bottom.y < wizard.height)
            verify(bottom.x < wizard.width)
            verify(next.mapToItem(wizard, 0, 0).y > 0)
        }
    }
    function test_skippingKeepsSavedPreferences() {
        AppSettings.podcastAutoQueueCount = 33
        const wizard = createTemporaryObject(wizardComponent, testCase)
        verify(wizard !== null)
        wizard.complete()
        compare(AppSettings.podcastAutoQueueCount, 33)
        compare(AppSettings.podcastAutoQueue, false)
        compare(AppSettings.podcastAutoCleanup, false)
    }
}
