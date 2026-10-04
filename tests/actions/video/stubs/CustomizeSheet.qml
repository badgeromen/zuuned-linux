import QtQuick
Item {
    signal saved(string message)
    function openFor(context) { LibraryService.customization = context }
}
