set(VTM_TEST_SOURCES test_money.cpp test_check.cpp test_scan_data.cpp test_price_check.cpp
    test_builder_index.cpp   # Menu Builder's quick index (core/builder_index.hh)
    test_buydown_core.cpp   # manufacturer buydowns and promotions (core/buydown.hh)
    test_system_discount_core.cpp   # system discounts (core/system_discount.hh)
    test_mass_update_core.cpp   # Mass Updates' rounding, margins, ranges (core/mass_update.hh)
    test_transfer_core.cpp   # transfers between stores: UPC matching with packs, totals, the markup (core/transfer.hh)
    test_receiving_core.cpp)   # retail receiving: invoice totals and tie-out, vendor items, invoice files (core/receiving.hh)
if(TARGET vtm_layout)
    list(APPEND VTM_TEST_SOURCES
        main.cpp   # QCoreApplication for Qt plugins
        test_layout.cpp test_navigator.cpp test_seed.cpp test_controller.cpp
        test_editor.cpp test_store.cpp test_edit_mode.cpp test_editor_ui.cpp
        test_pos_service.cpp test_pos_store.cpp test_pos_flow.cpp
        test_print.cpp test_pos_m4.cpp test_pages_ui.cpp test_net.cpp test_pos_m6.cpp test_pos_m7.cpp test_server_bank.cpp test_pairing.cpp test_updates.cpp test_phone.cpp test_checks.cpp test_menu.cpp test_reports.cpp test_coach.cpp test_staff.cpp test_safety.cpp test_customers.cpp test_waitlist.cpp test_kitchen.cpp test_inventory.cpp test_loyalty.cpp test_standby.cpp test_i18n.cpp test_kiosk.cpp test_search.cpp test_phone_orders.cpp test_cards.cpp test_refunds.cpp test_item_return.cpp test_robust.cpp test_receipts.cpp test_menu_builder.cpp test_menu_screens.cpp test_stress.cpp test_ilsa_sync.cpp test_audit_trail.cpp test_labor_law.cpp test_timeclock.cpp test_report_export.cpp test_soak.cpp test_receipt_barcode.cpp test_tax_included.cpp test_bar_items.cpp test_bar_inventory.cpp test_bar_sizes.cpp test_bar_speed.cpp test_save_speed.cpp test_bar_menu.cpp test_over_service.cpp test_quick_label.cpp
    test_images.cpp test_billboard.cpp
    test_price_check_app.cpp   # Menu Builder -> Price check: rates and cash discount, on tablets too
    test_scan_data_app.cpp   # tobacco scan data for Altria: settings, Fill in makers, the report
    test_serial_scanner.cpp   # serial barcode scanners (several ports, pseudo-terminals)
    test_wipe.cpp     # the cutover step (scripts/bin/vt-wipe-transactions)
    test_eod_archive.cpp   # the End of Day archive: write-once PDFs, the hash chain, the long-term folder
    test_exceptions_search.cpp   # Manager > Reports > Exceptions search: filters, totals match the report
    test_mass_updates.cpp   # Menu Builder > Mass Updates: paging, previews, Apply/Undo, change sets by UPC, price book
    test_transfers.cpp   # Manager > Inventory > Transfers: out, receive, differences, void, offline outbox, price flow
    test_retail_receiving.cpp   # Manager > Inventory > Receive retail: scan in, store use, tie-out, invoice files, history only, reports
    test_carton_stock.cpp   # retail on hand: a box / carton and its single as sealed + loose, breaks, Open boxes, settling once
    test_customer_sync.cpp   # TEMPORARY customers shared store to store: sent, merged by phone, to check, no loops
    test_buydowns.cpp   # Mass Updates > Buydowns / Promotions: the setup, the register's line, reports, scan data, a file for another store
    test_system_discounts.cpp   # Mass Updates > System Discounts: setup, the register's lines, tax, receipt, Copy to New, import
    test_legacy.cpp   # ILSA sales history (legacy_fixture.inc: vt-ilsa-history --self-test --write-fixture)
    )
endif()

add_executable(vtm_tests ${VTM_TEST_SOURCES})
target_link_libraries(vtm_tests PRIVATE vtm_core)
if(NOT TARGET vtm_layout)
    target_link_libraries(vtm_tests PRIVATE Catch2::Catch2WithMain)
else()
    target_link_libraries(vtm_tests PRIVATE Catch2::Catch2)
    find_package(Qt6 REQUIRED COMPONENTS Qml Test)
    target_link_libraries(vtm_tests PRIVATE vtm_ui vtm_uiplugin Qt6::Test)
    target_compile_definitions(vtm_tests PRIVATE VTM_SEED_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../seed"
                                                 VTM_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/pages"
                                                 VTM_SCRIPTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/bin")
endif()

include(${CMAKE_CURRENT_SOURCE_DIR}/../external/catch2/extras/Catch.cmake)
catch_discover_tests(vtm_tests)

# Every phrase on the screens has its Spanish, with the same %1 placeholders.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
    add_test(NAME i18n_es COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/../tools/i18n.py check es)
endif()
