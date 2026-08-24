execute_process(
    COMMAND tasklist /FI "IMAGENAME eq DolphinExplorer.exe"
    OUTPUT_VARIABLE dolphin_tasklist
    ERROR_QUIET
)

if(dolphin_tasklist MATCHES "DolphinExplorer\\.exe")
    message(FATAL_ERROR
        "DolphinExplorer.exe is running - close it before linking.")
endif()
