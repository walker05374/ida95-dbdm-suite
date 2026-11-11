# Path: ida95_dbdm_suite_plugin.py
# Runtime: IDA Pro 9.x Plugin Entrypoint (Drop into %APPDATA%/Hex-Rays/IDA Pro/plugins or ~/.idapro/plugins)

import os
from ui_timing_controller import (
    ActorContext,
    ComponentLifecycleRegistry,
    ProductionTimingController,
    ResultType,
    TimingEventDriver,
    TimingEventInstance,
    UICheckboxBinding,
    run_stage7_acceptance,
)

try:
    import ida_idaapi
    import ida_kernwin
    import idc
    from ida95_dbdm_suite.dbdm_config import UE4Offsets
    from ida95_dbdm_suite.ida95_types import IDA95TypeBuilder
    from ida95_dbdm_suite.ue4_analyzer import UE4DBDMAnalyzer
    from ida95_dbdm_suite.sdk_exporter import DBDMSDKExporter
    _HAS_IDA_RUNTIME = True
except ImportError:
    _HAS_IDA_RUNTIME = False


if _HAS_IDA_RUNTIME:
    class DBDMSuiteActionHandler(ida_kernwin.action_handler_t):
        def __init__(self, plugin_ref: "IDA95DBDMSuitePlugin") -> None:
            super().__init__()
            self.plugin_ref = plugin_ref

        def activate(self, ctx) -> int:
            self.plugin_ref.run(0)
            return 1

        def update(self, ctx) -> int:
            return ida_kernwin.AST_ENABLE_ALWAYS


    class IDA95DBDMSuitePlugin(ida_idaapi.plugin_t):
        flags = ida_idaapi.PLUGIN_KEEP | ida_idaapi.PLUGIN_MOD
        comment = "IDA 9.5 Dead by Daylight Mobile UE4 ARM64 Full Reverse Suite"
        help = "Scans UE4 globals, builds IDA 9.x tinfo_t structs, reconstructs UFunctions, and exports C++20 SDK"
        wanted_name = "DBDM Suite (IDA 9.5)"
        wanted_hotkey = "Ctrl-Shift-D"

        ACTION_NAME = "dbdm_suite:run_full_analysis"

        def init(self) -> int:
            action_desc = ida_kernwin.action_desc_t(
                self.ACTION_NAME,
                "Run DBDM UE4 Suite Analysis",
                DBDMSuiteActionHandler(self),
                self.wanted_hotkey,
                self.help,
                -1,
            )
            ida_kernwin.register_action(action_desc)
            ida_kernwin.attach_action_to_menu(
                "Edit/Plugins/", self.ACTION_NAME, ida_kernwin.SETMENU_APP
            )
            ida_kernwin.msg("[DBDM-Suite] Loaded for IDA 9.x (Ctrl-Shift-D)\n")
            return ida_idaapi.PLUGIN_KEEP

        def run(self, arg: int) -> None:
            ida_kernwin.msg("[DBDM-Suite] Starting UE4 ARM64 analysis...\n")
            ida_kernwin.show_wait_box("HIDECANCEL\nDBDM Suite: Registering UE4.27 IDA 9.x Types...")

            try:
                offsets = UE4Offsets()
                IDA95TypeBuilder.register_core_ue4_types(offsets)

                ida_kernwin.replace_wait_box("DBDM Suite: Scanning ARM64 ADRP/ADD signatures...")
                analyzer = UE4DBDMAnalyzer(offsets)
                globals_found = analyzer.scan_all_signatures()

                for name, ea in globals_found.items():
                    ida_kernwin.msg(f"[DBDM-Suite] Resolved {name} -> 0x{ea:X}\n")

                if "GUObjectArray" in globals_found:
                    IDA95TypeBuilder.apply_struct_at(
                        globals_found["GUObjectArray"], "FUObjectArray", "g_GUObjectArray"
                    )

                structs_cnt = 0
                funcs_cnt = 0
                if "GNames" in globals_found and "GUObjectArray" in globals_found:
                    ida_kernwin.replace_wait_box(
                        "DBDM Suite: Reconstructing UStructs & UFunction Exec pointers..."
                    )
                    structs_cnt, funcs_cnt = analyzer.reconstruct_ustruct_and_functions(
                        globals_found["GNames"], globals_found["GUObjectArray"]
                    )

                idb_path = idc.get_idb_path()
                out_dir = os.path.join(
                    os.path.dirname(idb_path) if idb_path else os.getcwd(), "dbdm_sdk_out"
                )
                hpp_out = DBDMSDKExporter.export_all(
                    out_dir, globals_found, offsets, structs_cnt, funcs_cnt
                )
                ida_kernwin.msg(
                    f"[DBDM-Suite] Complete! Reconstructed {structs_cnt} structs, {funcs_cnt} functions.\n"
                    f"[DBDM-Suite] SDK exported to: {hpp_out}\n"
                )
            finally:
                ida_kernwin.hide_wait_box()

        def term(self) -> None:
            ida_kernwin.detach_action_from_menu("Edit/Plugins/", self.ACTION_NAME)
            ida_kernwin.unregister_action(self.ACTION_NAME)


    def PLUGIN_ENTRY() -> IDA95DBDMSuitePlugin:
        return IDA95DBDMSuitePlugin()


if __name__ == "__main__":
    assert run_stage7_acceptance() is True
    print("IDA95 DBDM SUITE STAGE 7 INTEGRATION: PASSED")

