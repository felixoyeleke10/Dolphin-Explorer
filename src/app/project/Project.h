#pragma once
#include <string>
#include <vector>
#include <memory>
#include <QObject>
#include "app/layers/DataLayer.h"
#include "app/contracts/ContractStore.h"
#include "app/orchestration/ExecutionHistory.h"
#include "app/workers/Worker.h"
#include "app/project/ItemGroup.h"
#include "core/Contact.h"
#include "core/SpatialRef.h"

namespace dolphin::app {

struct ProjectSource {
    std::string id;
    std::string format;   // "xtf", "kmall"
    std::string path;
    core::SpatialRef source_spatial_ref;
    std::string sha256;
    uint64_t    size_bytes = 0;
    int64_t     modified_utc_ms = 0;
};

class Project : public QObject {
    Q_OBJECT
public:
    explicit Project(QObject* parent = nullptr);

    // Project-level default processing graph. Layers may inherit this graph
    // instead of maintaining their own local override.
    pipeline::NodeGraph processing_graph;

    // Create a blank project
    static std::shared_ptr<Project> create(const std::string& name,
                                            const std::string& manifest_path);

    // Open an existing .dlp project file (also accepts legacy .pelagic)
    static std::shared_ptr<Project> open(const std::string& manifest_path);

    bool save();
    bool saveAs(const std::string& new_path);

    // Sources
    ProjectSource* addSource(const std::string& path, const std::string& format);
    ProjectSource* findSource(const std::string& id);
    ProjectSource* findSourceByPath(const std::string& path);

    // Layers
    DataLayer*     addLayer(const std::string& source_id, const std::string& label);
    void           commitLayer(const std::string& id);
    void           markLayerIndexing(const std::string& id);
    void           markLayerFailed(const std::string& id);
    DataLayer*     findLayer(const std::string& id);
    std::vector<DataLayer*> findLayersBySource(const std::string& source_id) const;
    void           removeLayer(const std::string& id);
    void           reorderLayers(const std::vector<std::string>& ordered_layer_ids);

    // Workers
    workers::Worker* addWorker(workers::Worker worker);
    workers::Worker* findWorker(const std::string& id);
    const workers::Worker* findWorker(const std::string& id) const;
    void             removeWorker(const std::string& id);
    std::vector<workers::Worker>&       workers()       { return m_workers; }
    const std::vector<workers::Worker>& workers() const { return m_workers; }

    // Shared worker exchange + run history
    contracts::ContractStore& contractStore() { return m_contract_store; }
    const contracts::ContractStore& contractStore() const { return m_contract_store; }
    orchestration::ExecutionHistory& executionHistory() { return m_execution_history; }
    const orchestration::ExecutionHistory& executionHistory() const { return m_execution_history; }

    // Contacts
    void           addContact(const core::Contact& c);
    void           updateContact(const core::Contact& c);
    void           removeContact(uint64_t id);
    const std::vector<core::Contact>& contacts() const { return m_contacts; }

    // Layer groups
    ItemGroup*     addLayerGroup(const std::string& name);
    void           removeLayerGroup(const std::string& id);
    void           renameLayerGroup(const std::string& id, const std::string& name);
    ItemGroup*     findLayerGroup(const std::string& id);
    const std::vector<ItemGroup>& layerGroups() const { return m_layer_groups; }
    void           setLayerGroup(const std::string& layer_id, const std::string& group_id);
    void           setLayerTags(const std::string& layer_id, std::vector<std::string> tags);

    // Contact groups
    ItemGroup*     addContactGroup(const std::string& name);
    void           removeContactGroup(const std::string& id);
    void           renameContactGroup(const std::string& id, const std::string& name);
    ItemGroup*     findContactGroup(const std::string& id);
    const std::vector<ItemGroup>& contactGroups() const { return m_contact_groups; }
    void           setContactGroup(uint64_t contact_id, const std::string& group_id);
    void           setContactTags(uint64_t contact_id, std::vector<std::string> tags);

    // Metadata
    const std::string& name()         const { return m_name; }
    const std::string& manifestPath() const { return m_manifest_path; }
    std::string        dataPath()     const;   // project/data/ directory
    const std::string& crs()          const { return m_display_spatial_ref.id; }
    const core::SpatialRef& displaySpatialRef() const { return m_display_spatial_ref; }
    void               setCrs(const std::string& epsg);

    // Temporary (unsaved) projects exist only for the current session.
    // The cache folder is cleaned up when the user explicitly deletes the session,
    // or after a configurable idle period.  Saving promotes the project to a
    // named persistent project and clears this flag.
    bool isTempProject()  const { return m_is_temp; }
    void setTempProject(bool temp) { m_is_temp = temp; }

    const std::vector<std::unique_ptr<DataLayer>>& layers()  const { return m_layers; }
    const std::vector<ProjectSource>&              sources() const { return m_sources; }

signals:
    void layerPending(DataLayer* layer);  // Placeholder created; import starting
    void layerReady(DataLayer* layer);    // State → Ready; data available for all consumers
    void layerRemoved(const std::string& id);
    void layersReordered();
    void contactAdded(const core::Contact& c);
    void contactRemoved(uint64_t id);
    void layerGroupsChanged();
    void contactGroupsChanged();
    void modified();

private:
    std::string                           m_name;
    std::string                           m_manifest_path;
    core::SpatialRef                      m_display_spatial_ref = core::makeWgs84SpatialRef();
    bool                                  m_is_temp = false;
    std::vector<ProjectSource>            m_sources;
    std::vector<std::unique_ptr<DataLayer>> m_layers;
    std::vector<workers::Worker>          m_workers;
    contracts::ContractStore              m_contract_store;
    orchestration::ExecutionHistory       m_execution_history;
    std::vector<core::Contact>            m_contacts;
    uint64_t                              m_next_contact_id = 1;
    std::vector<ItemGroup>                m_layer_groups;
    std::vector<ItemGroup>                m_contact_groups;

    std::string toJson() const;
    bool        fromJson(const std::string& json);
    std::string generateId(const std::string& prefix) const;
    void        purgeOrphanedCaches();
};

} // namespace dolphin::app
