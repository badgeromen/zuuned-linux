import QtQuick
import QtTest
import Zuuned

TestCase {
    name: "PlaylistOccurrenceEditor"

    function init() {
        TrayState.discard()
        LibraryService.saved = null
        LibraryService.saveResult = true
        LibraryService.rows = [
            {libraryId: 1, title: "A", artist: "Artist", album: "Album", genre: "Alternative"},
            {libraryId: 2, title: "C", artist: "Artist", album: "Album", genre: "Alternative"}
        ]
        LibraryService.playlistRows = [
            {id: 1, playlistEntryId: 101, title: "A"},
            {id: 2, playlistEntryId: 102, title: "C"},
            {id: 1, playlistEntryId: 103, title: "A"}
        ]
    }

    function test_metadata_refresh_keeps_occurrences() {
        TrayState.openExisting(11, "Imported")
        compare(TrayState.tracks.map(t => t.playlistEntryId), [101, 102, 103])
        compare(TrayState.originalPlaylistEntryIds, [101, 102, 103])
        compare(TrayState.tracks[0].genre, "Alternative")
        TrayState.refreshMetadata()
        compare(TrayState.tracks.map(t => t.playlistEntryId), [101, 102, 103])
    }

    function test_reorder_and_remove_repeated_occurrence() {
        TrayState.openExisting(11, "Imported")
        TrayState.move(2, 0)
        TrayState.removeAt(1)
        verify(TrayState.save())
        compare(LibraryService.saved.tracks.map(t => t.playlistEntryId), [103, 102])
        compare(LibraryService.saved.original, [101, 102, 103])
        compare(LibraryService.saved.id, 11)
    }

    function test_late_arrivals_do_not_expand_editor_baseline() {
        TrayState.openExisting(11, "Imported")
        LibraryService.playlistRows = LibraryService.playlistRows.concat([{id: 3, playlistEntryId: 104, title: "B"}])
        TrayState.refreshMetadata()
        verify(TrayState.save())
        compare(LibraryService.saved.original, [101, 102, 103])
        compare(LibraryService.saved.tracks.length, 3)
    }

    function test_new_member_has_no_foreign_occurrence_id() {
        TrayState.openExisting(11, "Imported")
        compare(TrayState.addTracks([{id: 3, playlistEntryId: 999, title: "New"}]), 1)
        compare(TrayState.tracks[3].playlistEntryId, 0)
        verify(TrayState.save())
        compare(LibraryService.saved.tracks[3].playlistEntryId, 0)
        compare(LibraryService.saved.original, [101, 102, 103])
    }

    function test_failed_save_keeps_the_editor_open() {
        TrayState.openExisting(11, "Imported")
        TrayState.move(0, 1)
        LibraryService.saveResult = false
        verify(!TrayState.save())
        verify(TrayState.building)
        verify(TrayState.dirty)
    }

    function test_new_playlist_resets_old_baseline() {
        TrayState.openExisting(11, "Imported")
        TrayState.openNew()
        compare(TrayState.originalPlaylistEntryIds, [])
        TrayState.name = "New playlist"
        TrayState.addTracks([{id: 1, title: "A"}])
        verify(TrayState.save())
        compare(LibraryService.created, [1])
    }
}
