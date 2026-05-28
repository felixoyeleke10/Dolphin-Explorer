#pragma once

#include <cstdint>
#include <string>

#include "app/contracts/ContractTypes.h"

namespace dolphin::app::contracts {

struct ContractEnvelope {
    std::string     id;
    std::string     binding_key;
    ContractType    type = ContractType::ProcessedSidescanLayer;
    ContractPayload payload;
    std::string     producer_worker_id;
    std::string     schema_version = "1";
    std::string     content_hash;
    int64_t         created_utc_ms = 0;

    template <typename T>
    const T* payloadAs() const
    {
        return std::get_if<T>(&payload);
    }
};

} // namespace dolphin::app::contracts
