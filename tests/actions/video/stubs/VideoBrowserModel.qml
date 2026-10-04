import QtQuick
QtObject {
    property var library
    property var deviceVideos
    property bool deviceConnected: false
    property var rowOverrides: ({})
    property var movieGroups: LibraryService.rows.filter(v => !v.isTV)
    property var seriesList: []
    property var animeList: []
    property var musicVideosList: []
    property var othersList: []
    property var unmatchedSeriesGroups: LibraryService.unmatchedGroups
    property var unmatchedLoose: LibraryService.unmatched
    property int movieCount: movieGroups.length
    property int totalEpisodeCount: 2
    property int pendingCount: 0
    property int unmatchedCount: 0
    signal groupsChanged()
    function movieDetail(id) {
        const row = LibraryService.rows.find(v => Number(v.id) === Number(id))
        return row ? Object.assign({}, row, {versions: [row]}) : ({})
    }
    function seriesDetail(key) {
        return {key: key, name: "Show", seasons: [1], defaultSeason: 1,
            poster: "", seasonCount: 1, episodeCount: 2, year: "", tmdbTitle: ""}
    }
    function episodesForSeason(key, season) { return LibraryService.rows.filter(v => v.isTV && v.season === season) }
    function seriesEpisodeIds(key) { return LibraryService.rows.filter(v => v.isTV).map(v => v.id) }
}
