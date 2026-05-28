add_library(dolphin-app STATIC
    # Project — lifecycle + per-domain CRUD + serialization
    project/Project.cpp
    project/Project.Sources.cpp
    project/Project.Layers.cpp
    project/Project.Contacts.cpp
    project/Project.Workers.cpp
    project/Project.Groups.cpp
    project/Project.Serialization.cpp
    project/Project.Serialization.Read.cpp

    # Layers
    layers/DataLayer.cpp
    layers/LayerUtils.cpp

    # Services
    services/ImportService.cpp
    services/ImportService.Tasks.cpp
    services/ImportService.Load.cpp
    services/ProcessingService.cpp

    # Contracts
    contracts/ContractStore.cpp

    # Import orchestration
    import/ImportLog.cpp
    import/ImportJobManager.cpp
    import/FormatRegistry.cpp

    # Tasks / operation management
    tasks/OperationManager.cpp

    # Project utilities
    project/PathAuthority.cpp

    # Processing orchestration
    orchestration/ExecutionHistory.cpp
    orchestration/Orchestrator.cpp

    # Workers
    workers/Worker.cpp
    workers/ProcessingWorkerAdapter.cpp
    workers/QcWorkerAdapter.cpp
    workers/ReportWorkerAdapter.cpp
)

target_include_directories(dolphin-app PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(dolphin-app PUBLIC
    dolphin-core
    dolphin-util
    dolphin-io
    dolphin-geo
    dolphin-pipeline
    Qt6::Core
    Qt6::Concurrent
)
