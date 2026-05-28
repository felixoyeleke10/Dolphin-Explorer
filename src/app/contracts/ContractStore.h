#pragma once

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "app/contracts/ContractEnvelope.h"

namespace dolphin::app::contracts {

class ContractStore {
public:
    void publish(const std::string& producer_worker_id,
                 const std::vector<ContractEnvelope>& outputs);

    const ContractEnvelope* latest(ContractType type,
                                   const std::string& binding_key = {}) const;

    std::vector<ContractEnvelope> latestByType(ContractType type) const;
    std::vector<ContractEnvelope> all() const;
    void clear();

    static std::string hash(const std::vector<ContractEnvelope>& envelopes);

private:
    using StoreKey = std::pair<ContractType, std::string>;
    std::map<StoreKey, ContractEnvelope> m_latest;
};

} // namespace dolphin::app::contracts
