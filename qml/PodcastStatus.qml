import QtQuick
import Zuuned

QtObject {
    function deviceFor(id) {
        void SpokenMediaService.revision
        void DeviceService.podcastEpisodesList
        if (!DeviceService.connected) return ({known: false, onDevice: false, playedKnown: false, played: false})
        const identity = SpokenMediaService.podcastDeviceIdentity(id)
        if (!identity.feedUrl || !identity.sourceUrl) return ({known: false, onDevice: false, playedKnown: false, played: false})
        return DeviceService.podcastDeviceStatus(identity.feedUrl, identity.sourceUrl)
    }
    function played(entry) {
        const state = deviceFor(entry.id)
        return !!entry.played || (!!state.playedKnown && !!state.played)
    }
    function playedLabel(entry) {
        if (entry.played) return "played"
        const state = deviceFor(entry.id)
        return state.playedKnown && state.played ? "played on Zune" : ""
    }
}
