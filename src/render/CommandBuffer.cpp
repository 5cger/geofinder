// 参考蓝图 §5.4 — CommandBuffer 实现（Sprint 1）
//
// Sprint 1 仅存储命令列表，不做合批优化。
// TODO(Sprint2): 实现 execute() 合批合并策略

#include "render/CommandBuffer.hpp"

namespace geofinder {

void CommandBuffer::clear()
{
    m_commands.clear();
}

void CommandBuffer::add(const RenderCommand& cmd)
{
    m_commands.push_back(cmd);
}

const std::vector<RenderCommand>& CommandBuffer::getCommands() const
{
    return m_commands;
}

} // namespace geofinder
