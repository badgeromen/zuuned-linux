import QtQuick
import Zuuned

QtObject {
    // entry: an episode row or its ID. Rows from episodePage carry feedUrl
    // and deviceSource, so a list row needs no library lookup.
    function deviceFor(entry) {
        void SpokenMediaService.revision
        void DeviceService.podcastEpisodesList
        if (!DeviceService.connected) return ({known: false, onDevice: false, playedKnown: false, played: false})
        const row = typeof entry === "object" && entry ? entry : null
        const identity = row && row.feedUrl !== undefined && row.deviceSource !== undefined
            ? {feedUrl: row.feedUrl, sourceUrl: row.deviceSource}
            : SpokenMediaService.podcastDeviceIdentity(row ? row.id : entry)
        if (!identity.feedUrl || !identity.sourceUrl) return ({known: false, onDevice: false, playedKnown: false, played: false})
        return DeviceService.podcastDeviceStatus(identity.feedUrl, identity.sourceUrl)
    }
    function played(entry) {
        const state = deviceFor(entry)
        return !!entry.played || (!!state.playedKnown && !!state.played)
    }
    function playedLabel(entry) {
        if (entry.played) return "played"
        const state = deviceFor(entry)
        return state.playedKnown && state.played ? "played on Zune" : ""
    }
}
