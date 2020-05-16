INCLUDEPATH += $$PWD/.. $$PWD/../util $$PWD/../sokol-samples/sapp $$PWD/../sokol-samples/libs
DEFINES += SOKOL_GLES2 SOKOL_NO_ENTRY SOKOL_TRACE_HOOKS USE_DBG_UI

HEADERS += $$PWD/../sokol_app.h
SOURCES += $$PWD/../sokol-samples/libs/dbgui/dbgui.cc

IMGUI_ROOT=$(HOME)/Private/Projekty/0.shared/common-src-runtime/Gui/imgui
IMGUI_PRI=$$IMGUI_ROOT/examples/contrib_example_qt/imgui.pri

exists($$IMGUI_PRI): include($$IMGUI_PRI)
else:error("*** ERROR: Cannot find imgui.pri. Aborting.")
