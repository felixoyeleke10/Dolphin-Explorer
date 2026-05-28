#include "app/orchestration/Orchestrator.h"

#include <algorithm>
#include <chrono>
#include <functional>
#include <set>

#include "app/project/Project.h"
#include "app/contracts/ContractStore.h"
#include "app/workers/Worker.h"
#include "app/workers/WorkerAdapter.h"

namespace dolphin::app::orchestration {

namespace {

int64_t nowUtcMs()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(
        system_clock::now().time_since_epoch()).count();
}

std::string hashText(const std::string& text)
{
    return std::to_string(std::hash<std::string>{}(text));
}

std::string hashGraph(const workers::Worker& worker)
{
    return hashText(worker.graph.toJson());
}

std::vector<contracts::ContractEnvelope> resolveInputs(
    const Project& project,
    const workers::Worker& worker)
{
    std::vector<contracts::ContractEnvelope> inputs;
    for (const auto& port : worker.inputs) {
        if (const auto* envelope = project.contractStore().latest(port.type, port.binding_key))
            inputs.push_back(*envelope);
    }
    return inputs;
}

} // namespace

std::vector<WorkerRunRecord> Orchestrator::run(
    Project& project,
    const std::vector<std::string>& requested_worker_ids)
{
    markDirty(project);
    auto plan = buildExecutionPlan(project, requested_worker_ids);

    std::vector<WorkerRunRecord> records;
    std::set<std::string> completed;

    for (auto* worker : plan) {
        if (!worker)
            continue;
        if (!dependenciesSatisfied(project, *worker)) {
            worker->status = workers::WorkerStatus::Blocked;
            WorkerRunRecord record;
            record.worker_id = worker->id;
            record.status = workers::WorkerStatus::Blocked;
            record.error = "Dependencies not satisfied";
            records.push_back(record);
            continue;
        }

        auto record = executeWorker(project, *worker);
        records.push_back(record);
        if (record.status == workers::WorkerStatus::Completed
            || record.status == workers::WorkerStatus::Cached) {
            completed.insert(worker->id);
        }
    }

    return records;
}

std::vector<workers::Worker*> Orchestrator::buildExecutionPlan(
    Project& project,
    const std::vector<std::string>& requested_worker_ids) const
{
    std::set<std::string> requested(requested_worker_ids.begin(), requested_worker_ids.end());
    std::vector<workers::Worker*> plan;

    for (auto& worker : project.workers()) {
        if (!requested.empty() && !requested.count(worker.id))
            continue;
        plan.push_back(&worker);
    }

    std::stable_sort(plan.begin(), plan.end(),
        [](const workers::Worker* lhs, const workers::Worker* rhs) {
            return lhs->policy.priority > rhs->policy.priority;
        });
    return plan;
}

void Orchestrator::markDirty(Project& project) const
{
    for (auto& worker : project.workers()) {
        if (worker.status == workers::WorkerStatus::Running)
            continue;
        if (worker.last_graph_hash != hashGraph(worker))
            worker.status = workers::WorkerStatus::Dirty;
    }
}

bool Orchestrator::dependenciesSatisfied(const Project& project,
                                         const workers::Worker& worker) const
{
    for (const auto& upstream_id : worker.depends_on) {
        const auto* upstream = project.findWorker(upstream_id);
        if (!upstream)
            return false;
        if (upstream->status != workers::WorkerStatus::Completed
            && upstream->status != workers::WorkerStatus::Cached) {
            return false;
        }
    }

    for (const auto& port : worker.inputs) {
        if (!port.required)
            continue;
        if (!project.contractStore().latest(port.type, port.binding_key))
            return false;
    }

    return true;
}

WorkerRunRecord Orchestrator::executeWorker(Project& project, workers::Worker& worker) const
{
    WorkerRunRecord record;
    record.worker_id = worker.id;
    record.started_utc_ms = nowUtcMs();
    worker.status = workers::WorkerStatus::Running;

    const auto inputs = resolveInputs(project, worker);
    record.input_hash = contracts::ContractStore::hash(inputs);
    record.graph_hash = hashGraph(worker);

    if (worker.policy.cache_enabled
        && !worker.last_input_hash.empty()
        && worker.last_input_hash == record.input_hash
        && worker.last_graph_hash == record.graph_hash) {
        worker.status = workers::WorkerStatus::Cached;
        record.status = workers::WorkerStatus::Cached;
        record.cached = true;
        record.ended_utc_ms = nowUtcMs();
        project.executionHistory().record(record);
        return record;
    }

    if (!worker.adapter) {
        worker.status = workers::WorkerStatus::Failed;
        record.status = workers::WorkerStatus::Failed;
        record.error = "Worker has no adapter";
        record.ended_utc_ms = nowUtcMs();
        project.executionHistory().record(record);
        return record;
    }

    workers::WorkerExecutionContext context{&project, &worker};
    pipeline::GraphJob job;
    auto source = worker.adapter->buildSource(inputs, context);
    auto output = worker.graph.execute(source, job);
    auto contracts = worker.adapter->collectOutputs(output, job, context);
    project.contractStore().publish(worker.id, contracts);

    record.graph_job = job;
    record.output_hash = contracts::ContractStore::hash(contracts);
    for (const auto& envelope : contracts)
        record.output_contract_ids.push_back(envelope.id);

    worker.last_input_hash = record.input_hash;
    worker.last_graph_hash = record.graph_hash;
    worker.last_output_hash = record.output_hash;
    worker.status = job.anyFailed()
        ? workers::WorkerStatus::Failed
        : workers::WorkerStatus::Completed;

    record.status = worker.status;
    if (job.anyFailed())
        record.error = "One or more graph nodes failed";
    record.ended_utc_ms = nowUtcMs();

    project.executionHistory().record(record);
    return record;
}

} // namespace dolphin::app::orchestration
