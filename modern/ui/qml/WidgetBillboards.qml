import QtQuick

// Store Settings > Billboards (the "billboards" page; Hardware Configuration's Billboards button
// opens it too): the TVs showing the guest screen's messages, pictures and videos, here or at
// another store over the VPN. Laid out like Terminals: the billboards on the left (address, online
// or not, on or dark, muted), the one chosen on the right - its name, address, on / off now, sound,
// hours each day by the store's clock - and Deploy / Update app, Restart app, Identify and Remove.
// + Add billboard: a step at a time for a Fire TV. Kept on the store's computer: a change reaches
// the TV at once over its own connection. Managers only (WidgetAdminPanel.qml, panel "billboards";
// the records are adminRecords("billboards"), pos_billboards.cpp).
WidgetAdminPanel {
    forcedPanel: "billboards"
}
