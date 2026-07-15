#pragma once

#include "app/project/Project.h"

#include <functional>
#include <string>
#include <vector>

class QMenu;
class QWidget;

namespace dolphin::ui::detail {

QMenu* buildTagMenu(
    QWidget* parent,
    const std::vector<std::string>& current_tags,
    std::function<void(std::vector<std::string>)> apply);

QMenu* buildGroupMenu(
    QWidget* parent,
    const std::vector<app::ItemGroup>& groups,
    const std::string& current_group_id,
    std::function<void(std::string)> apply_group,
    std::function<app::ItemGroup*(const std::string&)> create_group);

QMenu* buildBulkLayerGroupMenu(
    QWidget* parent,
    const std::vector<app::ItemGroup>& groups,
    int layer_count,
    std::function<void(std::string)> apply_group,
    std::function<app::ItemGroup*(const std::string&)> create_group);

} // namespace dolphin::ui::detail
