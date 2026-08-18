set(VERSION 1.8)

set(MOC_HEADERS
    addfilmstripframespopup.h
    adjustlevelspopup.h
    adjustthicknesspopup.h
    antialiaspopup.h
    audiorecordingpopup.h
    autoinputcellnumberpopup.h
    batches.h
    batchserversviewer.h
    binarizepopup.h
    fillholespopup.h
    boardsettingspopup.h
    brightnessandcontrastpopup.h
    cachefxcommand.h
    camerasettingspopup.h
    canvassizepopup.h
    castviewer.h
    cleanuppaletteviewer.h
    cleanuppopup.h
    cleanuppreview.h
    cleanupsettingsmodel.h
    cleanupsettingspane.h
    cleanupsettingspopup.h
    cleanupswatch.h
    colormodelbehaviorpopup.h
    colormodelviewer.h
    comboviewerpane.h
    commandbar.h
    commandbarpopup.h
    convertpopup.h
    crashhandler.h
    duplicatepopup.h
    dvdirtreeview.h
    dvitemview.h
    dvwidgets.h
    exportlevelpopup.h
    exportalllevelspopup.h
    exportpanel.h
    exportscenepopup.h
    filebrowser.h
    filebrowsermodel.h
    filebrowserpopup.h
    fileinfopopup.h
    filmstrip.h
    flipbook.h
    formatsettingspopups.h
    frameheadgadget.h
    fxparameditorpopup.h
    histogrampopup.h
    historypane.h
    imageviewer.h
    insertfxpopup.h
    layerfooterpanel.h
    layerheaderpanel.h
    levelcreatepopup.h
    levelsettingspopup.h
    linesfadepopup.h
    lipsyncpopup.h
    autolipsyncpopup.h
    loadfolderpopup.h
    locatorpopup.h
    magpiefileimportpopup.h
    mainwindow.h
    matchline.h
    menubar.h
    menubarpopup.h
    meshifypopup.h
    messagepanel.h
    ocaio.h
    onionskinmaskgui.h
    outputsettingspopup.h
    overwritepopup.h
    pane.h
    pltgizmopopup.h
    preferencespopup.h
    previewer.h
    previewfxmanager.h
    projectpopup.h
    psdsettingspopup.h
    reframepopup.h
    renumberpopup.h
    reslist.h
    ruler.h
    savepresetpopup.h
    scanpopup.h
    scenebrowser.h
    scenesettingspopup.h
    sceneviewer.h
    sceneviewercontextmenu.h
    scriptconsolepanel.h
    brushpresetpanel.h
    toolpropertiespanel.h
    separatecolorspopup.h
    separatecolorsswatch.h
    shortcutpopup.h
    soundtrackexport.h
    startuppopup.h
    styleshortcutswitchablepanel.h
    svncleanupdialog.h
    svncommitdialog.h
    svndeletedialog.h
    svnlockdialog.h
    svnlockframerangedialog.h
    svnpurgedialog.h
    svnrevertdialog.h
    svnupdateandlockdialog.h
    svnupdatedialog.h
    tapp.h
    tasksviewer.h
    testpanel.h
    timestretchpopup.h
    toolbar.h
    tpanels.h
    trackerpopup.h
    vectorguideddrawingpane.h
    vectorizerpopup.h
    vectorizerswatch.h
    versioncontrol.h
    versioncontrolxmlwriter.h
    versioncontroltimeline.h
    versioncontrolwidget.h
    viewerpane.h
    xshcellviewer.h
    xshcolumnviewer.h
    xsheetviewer.h
    xshnoteviewer.h
    xshrowviewer.h
    xshtoolbar.h
    xdtsimportpopup.h
    expressionreferencemanager.h
    tooloptionsshortcutinvoker.h
    exportxsheetpdf.h
    custompanelmanager.h
    toolpresetcommandmanager.h
    custompaneleditorpopup.h
    convertfolderpopup.h
    exportcameratrackpopup.h
)

if(PLATFORM EQUAL 64)
    set(MOC_HEADERS
        ${MOC_HEADERS}
        ../stopmotion/stopmotion.h
        ../stopmotion/stopmotioncontroller.h
        ../stopmotion/webcam.h
        ../stopmotion/jpgconverter.h
        ../stopmotion/canon.h
        ../stopmotion/stopmotionserial.h
        ../stopmotion/stopmotionlight.h
        cameracapturelevelcontrol.h
        penciltestpopup.h
        navtageditorpopup.h
        xshbreadcrumbs.h
    )
else()
    set(MOC_HEADERS
        ${MOC_HEADERS}
        cameracapturelevelcontrol_qt.h
        penciltestpopup_qt.h
        navtageditorpopup.h
        xshbreadcrumbs.h
    )
endif()

set(HEADERS
    ${MOC_HEADERS}
    celldata.h
    cellkeyframedata.h
    cellkeyframeselection.h
    cellselection.h
    columncommand.h
    columnselection.h
    curveio.h
    drawingdata.h
    exportlevelcommand.h
    filedata.h
    fileselection.h
    filmstripcommand.h
    filmstripselection.h
    floatingpanelcommand.h
    history.h
    iocommand.h
    keyframedata.h
    keyframemover.h
    keyframeselection.h
    kis_tablet_support_win8.h
    menubarcommandids.h
    moviegenerator.h
    scanlist.h
    sceneviewerevents.h
    selectionutils.h
    subcameramanager.h
    subscenecommand.h
    tfarmstuff.h
    versioncontrolgui.h
    versioncontrolxmlreader.h
    viewerdraw.h
    viewerpopup.h
    xshcellmover.h
    xsheetdragtool.h
    castselection.h
    ../include/cellposition.h
    ../include/orientation.h
    ../include/saveloadqsettings.h
    xdtsio.h
    sxfio.h
    levelcommand.h
# Tracker file
    ObjectTracker.h
    dummyprocessor.h
    metnum.h
    processor.h
    predict3d.h
    tvpjson_io.h
)

set(SOURCES
    floatingpanelcommand.cpp
    canvassizepopup.cpp
    commandbar.cpp
    commandbarpopup.cpp
    history.cpp
    loadfoldercommand.cpp
    loadfolderpopup.cpp
    main.cpp
    mainwindow.cpp
    matchline.cpp
    menubar.cpp
    menubarpopup.cpp
    mergecolumns.cpp
    moviegenerator.cpp
    pane.cpp
    previewer.cpp
    previewfxmanager.cpp
    ruler.cpp
    scanlist.cpp
    tapp.cpp
    toolbar.cpp
    batchserversviewer.cpp
    castviewer.cpp
    exportlevelpopup.cpp
    exportalllevelspopup.cpp
    exportscenepopup.cpp
    colormodelviewer.cpp
    exportpanel.cpp
    filebrowser.cpp
    filebrowserversioncontrol.cpp
    filmstrip.cpp
    matchlinecommand.cpp
    messagepanel.cpp
    tasksviewer.cpp
    testpanel.cpp
    tpanels.cpp
    xshcellmover.cpp
    xshcellviewer.cpp
    xshcolumnviewer.cpp
    xsheetdragtool.cpp
    xsheetviewer.cpp
    xshnoteviewer.cpp
    xshrowviewer.cpp
    xshtoolbar.cpp
    sceneviewer.cpp
    sceneviewerevents.cpp
    viewerdraw.cpp
    viewerpane.cpp
    castselection.cpp
    cellselection.cpp
    columnselection.cpp
    fileselection.cpp
    filmstripselection.cpp
    keyframeselection.cpp
    addfilmstripframespopup.cpp
    camerasettingspopup.cpp
    convertpopup.cpp
    crashhandler.cpp
    duplicatepopup.cpp
    dvdirtreeview.cpp
    filebrowserpopup.cpp
    formatsettingspopups.cpp
    fxparameditorpopup.cpp
    histogrampopup.cpp
    insertfxpopup.cpp
    layerfooterpanel.cpp
    layerheaderpanel.cpp
    levelcreatepopup.cpp
    levelsettingspopup.cpp
    lipsyncpopup.cpp
    autolipsyncpopup.cpp
    magpiefileimportpopup.cpp
    outputsettingspopup.cpp
    overwritepopup.cpp
    pltgizmopopup.cpp
    preferencespopup.cpp
    projectpopup.cpp
    psdsettingspopup.cpp
    renumberpopup.cpp
    runscriptcommand.cpp
    savepresetpopup.cpp
    scanpopup.cpp
    scenebrowser.cpp
    scenebrowserversioncontrol.cpp
    hidelinestrokegui.cpp
    sceneviewercontextmenu.cpp
    scenesettingspopup.cpp
    scriptconsolepanel.cpp
    brushpresetpanel.cpp
    toolpropertiespanel.cpp
    shortcutpopup.cpp
    soundtrackexport.cpp
    startuppopup.cpp
    subcameramanager.cpp
    kis_tablet_support_win8.cpp
    timestretchpopup.cpp
    trackerpopup.cpp
    vectorguideddrawingpane.cpp
    vectorizerpopup.cpp
    rasterizecommand.cpp
    vectorizerswatch.cpp
    versioncontrol.cpp
    versioncontroltimeline.cpp
    versioncontrolxmlreader.cpp
    versioncontrolxmlwriter.cpp
    versioncontrolwidget.cpp
    viewerpopup.cpp
    cellselectioncommand.cpp
    celltransposecommand.cpp
    columncommand.cpp
    filmstripcommand.cpp
    iocommand.cpp
    levelcommand.cpp
    viewercommand.cpp
    print.cpp
    subscenecommand.cpp
    svncleanupdialog.cpp
    svncommitdialog.cpp
    svndeletedialog.cpp
    svnlockdialog.cpp
    svnlockframerangedialog.cpp
    svnrevertdialog.cpp
    svnupdateandlockdialog.cpp
    svnupdatedialog.cpp
    svnpurgedialog.cpp
    vcrcommand.cpp
    xsheetcmd.cpp
    celldata.cpp
    drawingdata.cpp
    filedata.cpp
    imageviewer.cpp
    keyframedata.cpp
    dvitemview.cpp
    dvwidgets.cpp
    flipbook.cpp
    frameheadgadget.cpp
    onionskinmaskgui.cpp
    batches.cpp
    tfarmstuff.cpp
    cleanuppaletteviewer.cpp
    cleanuppopup.cpp
    cleanuppreview.cpp
    cleanupsettingsmodel.cpp
    cleanupsettingspopup.cpp
    cleanupswatch.cpp
    cellkeyframedata.cpp
    cellkeyframeselection.cpp
    keyframemover.cpp
    cachefxcommand.cpp
    exportlevelcommand.cpp
    adjustlevelspopup.cpp
    adjustthicknesspopup.cpp
    binarizepopup.cpp
    fillholespopup.cpp
    brightnessandcontrastpopup.cpp
    linesfadepopup.cpp
    rendercommand.cpp
    curveio.cpp
    meshifypopup.cpp
    selectionutils.cpp
    antialiaspopup.cpp
    mergecmapped.cpp
    comboviewerpane.cpp
    historypane.cpp
    cleanupsettingspane.cpp
    audiorecordingpopup.cpp
    locatorpopup.cpp
    styleshortcutswitchablepanel.cpp
    reframepopup.cpp
    autoinputcellnumberpopup.cpp
    colormodelbehaviorpopup.cpp
    boardsettingspopup.cpp
    separatecolorsswatch.cpp
    separatecolorspopup.cpp
    xdtsio.cpp
    sxfio.cpp
    ocaio.cpp
    xdtsimportpopup.cpp
    expressionreferencemanager.cpp
    tooloptionsshortcutinvoker.cpp
    tvpjson_io.cpp
    exportxsheetpdf.cpp
    custompanelmanager.cpp
    toolpresetcommandmanager.cpp
    custompaneleditorpopup.cpp
    convertfolderpopup.cpp
    exportcameratrackpopup.cpp
# Tracker file
    dummyprocessor.cpp
    metnum.cpp
    ObjectTracker.cpp
    predict3d.cpp
)

if(PLATFORM EQUAL 64)
    set(SOURCES
        ${SOURCES}
        ../stopmotion/stopmotion.cpp
        ../stopmotion/stopmotioncontroller.cpp
        ../stopmotion/webcam.cpp
        ../stopmotion/jpgconverter.cpp
        ../stopmotion/canon.cpp
        ../stopmotion/stopmotionserial.cpp
        ../stopmotion/stopmotionlight.cpp
        cameracapturelevelcontrol.cpp
        penciltestpopup.cpp
        navtageditorpopup.cpp
        xshbreadcrumbs.cpp
    )
else()
    set(SOURCES
        ${SOURCES}
        cameracapturelevelcontrol_qt.cpp
        penciltestpopup_qt.cpp
        navtageditorpopup.cpp
        xshbreadcrumbs.cpp
    )
endif()


set(OBJCSOURCES filebrowsermodel.cpp)

# Resource file(s)
set(RESOURCES toonz.qrc)

if(BUILD_ENV_MSVC)
    set(RC_FILE toonz.rc)
endif()

qt5_add_resources(SOURCES ${RESOURCES})

if(BUILD_ENV_APPLE)
    #set_source_files_properties(${OBJCSOURCES} PROPERTIES LANGUAGE OBJCXX)
    set_source_files_properties(${OBJCSOURCES} PROPERTIES COMPILE_FLAGS "-x objective-c++")
endif()

qt5_wrap_cpp(SOURCES ${MOC_HEADERS})

if(BUILD_ENV_MSVC)
    add_executable(OpenToonz WIN32 ${HEADERS} ${SOURCES} ${OBJCSOURCES} ${RESOURCES} ${RC_FILE})
else()
    set(MACOSX_BUNDLE_ICON_FILE "OpenToonz.icns")
    set_source_files_properties(OpenToonz.icns PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
    add_executable(OpenToonz MACOSX_BUNDLE OpenToonz.icns ${HEADERS} ${SOURCES} ${OBJCSOURCES} ${RESOURCES})
    add_dependencies(OpenToonz tnzcore tnzbase toonzlib colorfx tnzext image sound toonzqt tnztools tnzstdfx tfarm)
    #add_definitions(
    #    -DTNZBASE_EXPORTS
    #    -DTPARAM_EXPORTS
    #    -DTXSHEET_EXPORTS
    #    -DTFX_EXPORTS
    #    -DTAPPTOOLS_EXPORTS
    #    -DTTEST_EXPORTS
    #)
endif()

if(WITH_TRANSLATION)
    add_translation(toonz ${HEADERS} ${SOURCES})
    add_compile_translations_command(OpenToonz toonz)
endif()

if(BUILD_TARGET_WIN)
    add_definitions(
        -DUNICODE
        -D_UNICODE
    )
endif()

message("subdir: toonz")
message("Bin: " ${CMAKE_CURRENT_BINARY_DIR})
#message("Lib: " ${LIBRARY_OUTPUT_PATH})
#message("Exe: " ${EXECUTABLE_OUTPUT_PATH})
#message("Sources:" ${SOURCES})

include_directories(
    ../../sources/toonzfarm/include
    ../../sources/toonzqt
    ../../sources/stopmotion
)

if (WITH_CANON)
    add_definitions(-DWITH_CANON)
endif()

if (WITH_WINTAB AND BUILD_TARGET_WIN AND (PLATFORM EQUAL 64))
    add_definitions(-DWITH_WINTAB)
endif()

if(BUILD_ENV_APPLE)
    include_directories(../../sources/mousedragfilter)
    if(PLATFORM EQUAL 64)
        include_directories(${TURBOJPEG_INCLUDE_DIRS})
    endif()
endif()

if(BUILD_TARGET_WIN)
    if(PLATFORM EQUAL 32)
        include_directories(
            SYSTEM
            ${SDKROOT}/quicktime/QT73SDK/CIncludes
        )
    endif()
endif()