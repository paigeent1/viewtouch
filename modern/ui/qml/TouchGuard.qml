pragma Singleton
import QtQuick

// Where the pop-up keyboard is, for the page's tap handlers. Qt offers a
// touch to the tap handlers underneath whatever took it, so a key on the
// keyboard also pressed the page button behind it; those handlers ignore a
// press inside the keyboard. (MouseAreas don't need this: the keyboard's
// own takes the touch from them.)
QtObject {
    // The keyboard's top edge on the screen; -1: no keyboard.
    property real keyboardTop: -1
    // A panel's own sheet over the whole window (counting a drawer): the
    // page under it is off, as for Main's own sheets (Main.pageCovered).
    property bool sheetOpen: false

    // An item of the window the keyboard is in (Main's): a touch on another window - the guest
    // screen - is never under this keyboard (BHDAN 2026-11-22: "Earn rewards: touch to add your
    // phone" ignored on the guest screen while the register's keyboard was up).
    property Item keyboardWindowItem: null

    function topOf(item) {
        let it = item
        while (it && it.parent)
            it = it.parent
        return it
    }

    // item: the handler's item (pass it, so a touch on another window counts as not covered).
    function covered(scenePoint, item) {
        if (keyboardTop < 0 || scenePoint.y < keyboardTop)
            return false
        if (item && keyboardWindowItem && topOf(item) !== topOf(keyboardWindowItem))
            return false
        return true
    }
}
