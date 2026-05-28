# ─────────────────────────────────────────────────────────────────────────────
#  dolphin-ui  —  UI layer, split into feature-owned sub-targets.
#
#  Dependency rules
#    core / io / geo / render / pipeline / app  →  never depend on ui
#    ui sub-targets  →  may depend on app, render, geo; never on each other
#                       except where noted (processing → map)
#    dolphin-ui (INTERFACE)  →  aggregate; DolphinExplorer links this only
#
#  Each sub-target:
#    • is STATIC
#    • has PUBLIC ${CMAKE_SOURCE_DIR}/src so all headers are reachable
#    • uses PRIVATE for internal dolphin deps (symbols are embedded in .a)
#    • uses PUBLIC for Qt modules (header-visible to consumers)
# ─────────────────────────────────────────────────────────────────────────────

# ── 1. Style / design system ──────────────────────────────────────────────────
add_library(dolphin-ui-style STATIC
    shell/AppStyle.cpp
    shell/AppStyleBase.cpp
    shell/AppStyleShell.cpp
    shell/AppStylePanels.cpp
    shell/AppStyleDialogs.cpp
    shell/AppStyleAcousticViews.cpp
    shell/AppStyleNodeGraph.cpp
)
# No Q_OBJECT in any style source — skip AutoMoc to avoid moc predefs
# generation failures with certain CMake/MinGW toolchain combinations.
set_target_properties(dolphin-ui-style PROPERTIES AUTOMOC OFF)
target_include_directories(dolphin-ui-style PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(dolphin-ui-style PUBLIC
    Qt6::Widgets
    Qt6::Svg
)

# ── 2. Bottom dock (terminal / diagnostics) ───────────────────────────────────
add_library(dolphin-ui-bottom STATIC
    bottom/DiagnosticsHub.cpp
    bottom/RuntimeLogBridge.cpp
    bottom/BottomDockPanel.cpp
    bottom/BottomDockPanel.Layout.cpp
    bottom/BottomDockPanel.Problems.cpp
    bottom/BottomDockPanel.OutputJobs.cpp
    bottom/TerminalWidget.cpp
)
target_include_directories(dolphin-ui-bottom PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(dolphin-ui-bottom
    PRIVATE dolphin-app
    PUBLIC  Qt6::Widgets
)

# ── 3. Shared widgets / panels / dialogs (used by 2+ features) ───────────────
add_library(dolphin-ui-shared STATIC
    shared/AppCommands.cpp
    shared/panels/ContactInspectorPage.cpp
    shared/panels/LayerInspectorPage.cpp
    shared/panels/LineListPanel.cpp
    shared/panels/LineListPanel.Tree.cpp
    shared/panels/LineListPanel.ContextMenu.cpp
    shared/widgets/AnimatedToolButton.cpp
    shared/widgets/CollapsibleSection.cpp
    shared/widgets/CommandBar.cpp
    shared/widgets/ViewerToolbar.cpp
    shared/widgets/LayerPickerWidget.cpp
    shared/dialogs/CommandPaletteDialog.cpp
    shared/dialogs/CommandPaletteDelegate.cpp
    shared/dialogs/CrsPickerDialog.cpp
    shared/dialogs/SettingsDialog.cpp
)
target_include_directories(dolphin-ui-shared PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(dolphin-ui-shared
    PRIVATE dolphin-app
            dolphin-render
            dolphin-ui-style
    PUBLIC  Qt6::Widgets
            Qt6::Svg
)

# ── 4. Geodesy feature ────────────────────────────────────────────────────────
add_library(dolphin-ui-geodesy STATIC
    features/geodesy/GeodesyPanel.cpp
)
target_include_directories(dolphin-ui-geodesy PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(dolphin-ui-geodesy
    PRIVATE dolphin-app
            dolphin-geo
            dolphin-ui-shared
    PUBLIC  Qt6::Widgets
)

# ── 5. Contacts feature ───────────────────────────────────────────────────────
add_library(dolphin-ui-contacts STATIC
    features/contacts/ContactListPanel.cpp
)
target_include_directories(dolphin-ui-contacts PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(dolphin-ui-contacts
    PRIVATE dolphin-app
            dolphin-ui-shared
    PUBLIC  Qt6::Widgets
)

# ── 6. Data Library ───────────────────────────────────────────────────────────
add_library(dolphin-ui-datalibrary STATIC
    features/datalibrary/DataLibraryWindow.cpp
)
target_include_directories(dolphin-ui-datalibrary PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(dolphin-ui-datalibrary
    PRIVATE dolphin-app
            dolphin-ui-shared
    PUBLIC  Qt6::Widgets
)

# ── 6. Import feature ─────────────────────────────────────────────────────────
add_library(dolphin-ui-import STATIC
    features/import/ImportController.cpp
    features/import/ImportPanel.cpp
    features/import/ImportProgressDialog.cpp
    features/import/ImportReviewWizard.cpp
    features/import/ImportReviewWizard.Layout.cpp
    features/import/ImportReviewWizard.Tabs.cpp
    features/import/ImportReviewWizard.SensorTabs.cpp
    features/import/ImportSetupDialog.cpp
)
target_include_directories(dolphin-ui-import PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(dolphin-ui-import
    PRIVATE dolphin-app
            dolphin-io
            dolphin-ui-shared
    PUBLIC  Qt6::Widgets
            Qt6::Concurrent
)

# ── 7. Map feature (MapView + SidescanViewController) ────────────────────────
add_library(dolphin-ui-map STATIC
    features/map/MapView.cpp
    features/map/paint/MapViewPaint.cpp
    features/map/paint/MapViewPaint.Sonar.cpp
    features/map/paint/MapViewPaint.NavTrack.cpp
    features/map/paint/MapViewPaint.Contacts.cpp
    features/map/paint/MapViewPaint.Overlays.cpp
    features/map/MapViewInput.cpp
    features/map/MapView3D.cpp
    features/map/MapView3D.Load.cpp
    features/map/MapView3D.GL.cpp
    features/map/MapView3D.Paint.cpp
    features/map/MapView3D.Overlays.cpp
    features/map/MapView3D.Input.cpp
    features/map/MapViewportHost.cpp
    features/map/sidescan/SidescanSwathGeoreferencer.cpp
    features/map/sidescan/SssGeorefParams.cpp
    features/map/sidescan/SssCoverageBuild.cpp
    features/map/sidescan/SssNavTrackBuild.cpp
    features/map/sidescan/SssPreviewRasterBuild.cpp
    features/map/sidescan/SidescanViewController.cpp
    features/map/sidescan/SidescanMapQuality.cpp
    features/map/sidescan/SidescanMapLoadTask.cpp
    features/map/sidescan/SidescanMapDiagnostics.cpp
    features/map/sidescan/SidescanEntryFilter.cpp
    features/map/sidescan/SidescanCorrectionDialog.cpp
    features/map/sidescan/SidescanCorrectionDialog.Layout.cpp
    features/map/track/TrackMapBuild.cpp
    features/map/subbottom/SbpProfileBuild.cpp
    features/map/subbottom/SubBottomMapDiagnostics.cpp
)
target_include_directories(dolphin-ui-map PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(dolphin-ui-map
    PRIVATE dolphin-app
            dolphin-render
            dolphin-geo
            dolphin-ui-shared
    PUBLIC  Qt6::Widgets
            Qt6::Concurrent
            Qt6::OpenGL
            Qt6::OpenGLWidgets
)

# ── 8. Waterfall feature ──────────────────────────────────────────────────────
add_library(dolphin-ui-waterfall STATIC
    features/waterfall/WaterfallView.cpp
    features/waterfall/WaterfallViewData.cpp
    features/waterfall/WaterfallViewPaint.cpp
    features/waterfall/WaterfallViewSeabed.cpp
    features/waterfall/WaterfallViewProcessing.cpp
    features/waterfall/processing/WaterfallProcessingAlgorithms.cpp
    features/waterfall/WaterfallViewInput.cpp
    features/waterfall/WaterfallSettingsDialog.cpp
    features/waterfall/WaterfallWindow.cpp
    features/waterfall/WaterfallWindow.Toolbar.cpp
    features/waterfall/WaterfallWindow.Status.cpp
    features/waterfall/WaterfallWindow.ContextMenu.cpp
    features/waterfall/WaterfallWindowLoad.cpp
    features/waterfall/components/WaterfallScrollSync.cpp
    features/waterfall/processing/SeabedAutoDetector.cpp
    features/waterfall/rendering/WaterfallGLRenderer.cpp
    features/waterfall/rendering/WaterfallRenderer.cpp
    features/waterfall/rendering/WaterfallRendererRow.cpp
    features/waterfall/processing/WaterfallSeabedTracker.cpp
    features/waterfall/processing/WaterfallPingAssembler.cpp
    features/waterfall/processing/WaterfallAmpProfile.cpp
    features/waterfall/painters/WaterfallOverlayPainter.cpp
    features/waterfall/painters/WaterfallOverlayPainterHUD.cpp
    features/waterfall/painters/WaterfallOverlayPainterAmplitude.cpp
    features/waterfall/panels/WaterfallInspectorPanel.cpp
    features/waterfall/panels/WaterfallInspectorPanel.Layout.cpp
    features/waterfall/panels/WaterfallInspectorPanel.Data.cpp
    features/waterfall/panels/WaterfallAnalysisPanel.cpp
    features/waterfall/panels/WaterfallAnalysisPanelImage.cpp
    features/waterfall/panels/WaterfallAnalysisPanelSeabed.cpp
    features/waterfall/panels/WaterfallAnalysisPanelContact.cpp
    features/waterfall/panels/WaterfallAnalysisPanelProcessing.cpp
    features/waterfall/components/WfValueRow.cpp
    features/waterfall/components/WfToggleRow.cpp
)
target_include_directories(dolphin-ui-waterfall PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(dolphin-ui-waterfall
    PRIVATE dolphin-app
            dolphin-render
            dolphin-ui-shared
    PUBLIC  Qt6::Widgets
            Qt6::Concurrent
            Qt6::OpenGL
            Qt6::OpenGLWidgets
)

# ── 9. Sub-bottom profiler viewer ────────────────────────────────────────────
add_library(dolphin-ui-subbottom STATIC
    features/subbottom/SubBottomSettingsDialog.cpp
    features/subbottom/SubBottomSettingsDialog.Tabs.cpp
    features/subbottom/SubBottomView.cpp
    features/subbottom/SubBottomView.Paint.cpp
    features/subbottom/SubBottomView.Input.cpp
    features/subbottom/SubBottomWindow.cpp
    features/subbottom/SubBottomWindow.Toolbar.cpp
    features/subbottom/SubBottomWindow.Status.cpp
    features/subbottom/SubBottomWindow.Load.cpp
    features/subbottom/SubBottomWindow.Processing.cpp
    features/subbottom/panels/SubBottomDisplayPanel.cpp
    features/subbottom/panels/SubBottomInspectorPanel.Layout.cpp
    features/subbottom/panels/SubBottomInspectorPanel.Data.cpp
)
target_include_directories(dolphin-ui-subbottom PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(dolphin-ui-subbottom
    PRIVATE dolphin-app
            dolphin-ui-shared
    PUBLIC  Qt6::Widgets
            Qt6::Concurrent
)

# ── 10. Node graph feature ────────────────────────────────────────────────────
add_library(dolphin-ui-nodegraph STATIC
    features/nodegraph/NodePaletteView.cpp
    features/nodegraph/NodeGraphView.cpp
    features/nodegraph/NodeGraphViewGeometry.cpp
    features/nodegraph/NodeGraphViewHitTest.cpp
    features/nodegraph/NodeGraphViewPaint.cpp
    features/nodegraph/NodeGraphViewInput.cpp
    features/nodegraph/NodeGraphWindow.cpp
    features/nodegraph/NodeGraphWindow.Layout.cpp
    features/nodegraph/NodeGraphWindow.PaletteDrag.cpp
    features/nodegraph/NodeInspectorPanel.cpp
)
target_include_directories(dolphin-ui-nodegraph PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(dolphin-ui-nodegraph
    PRIVATE dolphin-app
            dolphin-pipeline
            dolphin-ui-shared
    PUBLIC  Qt6::Widgets
)

# ── 11. Processing feature ────────────────────────────────────────────────────
#  Allowed exception: depends on dolphin-ui-map (ProcessingWindow drives
#  SidescanViewController::prebuildTier).
add_library(dolphin-ui-processing STATIC
    features/processing/ProcessingWindow.cpp
    features/processing/ProcessingController.cpp
)
target_include_directories(dolphin-ui-processing PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(dolphin-ui-processing
    PRIVATE dolphin-app
            dolphin-ui-map
            dolphin-ui-shared
    PUBLIC  Qt6::Widgets
)

# ── 11. Metadata feature ──────────────────────────────────────────────────────
add_library(dolphin-ui-metadata STATIC
    features/metadata/MetadataExportUtils.cpp
    features/metadata/SSSMetadataWindow.cpp
    features/metadata/SSSMetadataChart.cpp
    features/metadata/SSSNavModel.cpp
    features/metadata/SSSMetadataPlotWidget.cpp
    features/metadata/SSSMetadataLoad.cpp
    features/metadata/SSSMetadataFieldPanel.cpp
    features/metadata/SSSMetadataExport.cpp
    features/metadata/SBPMetadataWindow.cpp
    features/metadata/SBPNavModel.cpp
    features/metadata/SBPMetadataWindow.Layout.cpp
    features/metadata/SBPMetadataWindow.Load.cpp
    features/metadata/SBPMetadataWindow.Export.cpp
)
target_include_directories(dolphin-ui-metadata PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(dolphin-ui-metadata
    PRIVATE dolphin-app
            dolphin-ui-shared
            Qt6::Concurrent
    PUBLIC  Qt6::Widgets
)

# ── 12. MainWindow — composition root ─────────────────────────────────────────
#  Links every feature target; owns shell layout, menus, panel placement,
#  and high-level coordinator files.
#
#  TODO (spine systems): AppState, WindowRegistry, and the commands/ folder
#  are logically spine-level (Systems 1, 8, 10) — they belong in dolphin-ui-shared
#  or a future dolphin-ui-systems target once AppState is decoupled from
#  AppSettingsDialog. Until then they live here to avoid a circular dependency.
add_library(dolphin-ui-mainwindow STATIC
    mainwindow/panels/InspectorPanel.cpp
    mainwindow/rightpanel/RightPanelHost.cpp
    mainwindow/rightpanel/PanelChatWidget.cpp
    mainwindow/rightpanel/RightPanel.Info.cpp
    mainwindow/rightpanel/RightPanel.Display.cpp
    mainwindow/rightpanel/RightPanel.SubBottomDisplay.cpp
    mainwindow/rightpanel/RightPanel.SbpGain.cpp
    mainwindow/rightpanel/RightPanel.SbpSignal.cpp
    mainwindow/MainWindow.cpp
    mainwindow/MainWindow.Diagnostics.cpp
    mainwindow/MainWindow.Chrome.cpp
    mainwindow/MainWindow.Commands.cpp
    mainwindow/MainWindow.Shell.cpp
    mainwindow/MainWindow.ContextPanels.cpp
    mainwindow/MainWindow.MainArea.cpp
    mainwindow/MainWindow.ToolBar.cpp
    mainwindow/MainWindow.Menus.cpp
    mainwindow/MainWindow.Geodesy.cpp
    mainwindow/MainWindow.Project.cpp
    mainwindow/MainWindow.Events.cpp
    mainwindow/MainWindow.ProjectBinding.cpp
    mainwindow/MainWindow.Import.cpp
    mainwindow/MainWindow.Tools.cpp
    mainwindow/MainWindow.Layout.cpp
    mainwindow/coordinators/MainWindow.LayerCoordinator.cpp
    mainwindow/coordinators/MainWindow.WaterfallCoordinator.cpp
    mainwindow/coordinators/MainWindow.SubBottomCoordinator.cpp
    mainwindow/coordinators/MainWindow.NodeGraphCoordinator.cpp
    mainwindow/coordinators/MainWindow.ProcessingCoordinator.cpp
    mainwindow/coordinators/MainWindow.DataLibraryCoordinator.cpp
    mainwindow/MainWindow.MapContextMenu.cpp
    mainwindow/MainStatusBar.cpp
    mainwindow/AppState.cpp
    mainwindow/WindowRegistry.cpp
    mainwindow/ProjectEventBus.cpp
    mainwindow/MainWindow.AppSettings.cpp
    mainwindow/AppSettingsDialog.cpp
    mainwindow/AppSettingsDialog.Pages.cpp
    mainwindow/ChatPanel.cpp
    mainwindow/panels/NavInfoPanel.cpp
    mainwindow/panels/HeadingInfoPanel.cpp
    mainwindow/panels/GainControlPanel.cpp
    mainwindow/panels/ImagingControlPanel.cpp
)
target_include_directories(dolphin-ui-mainwindow PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(dolphin-ui-mainwindow
    PRIVATE dolphin-app
            dolphin-ui-style
            dolphin-ui-shared
            dolphin-ui-bottom
            dolphin-ui-map
            dolphin-ui-waterfall
            dolphin-ui-subbottom
            dolphin-ui-nodegraph
            dolphin-ui-import
            dolphin-ui-processing
            dolphin-ui-metadata
            dolphin-ui-geodesy
            dolphin-ui-contacts
            dolphin-ui-datalibrary
            Qt6::Concurrent
            Qt6::Network
    PUBLIC  Qt6::Widgets
            Qt6::Svg
)

# ── 13. dolphin-ui — INTERFACE aggregate ─────────────────────────────────────
#  DolphinExplorer links only this target; the aggregate pulls in all
#  sub-targets and their transitive Qt / dolphin deps.
add_library(dolphin-ui INTERFACE)
target_link_libraries(dolphin-ui INTERFACE
    dolphin-ui-mainwindow
    dolphin-ui-style
    dolphin-ui-shared
    dolphin-ui-bottom
    dolphin-ui-map
    dolphin-ui-waterfall
    dolphin-ui-subbottom
    dolphin-ui-nodegraph
    dolphin-ui-import
    dolphin-ui-processing
    dolphin-ui-metadata
    dolphin-ui-geodesy
    dolphin-ui-contacts
    dolphin-ui-datalibrary
)
