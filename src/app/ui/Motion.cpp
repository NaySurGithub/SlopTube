#include "app/ui/Motion.hpp"

namespace sloptube::app::motion {

Layer::Layer(ImDrawList* list)
    : m_list(list) {
    // A fresh command keeps the layer's clip rectangles separate from earlier content.
    m_list->AddDrawCmd();
    m_firstCommand = m_list->CmdBuffer.Size - 1;
    m_firstVertex = m_list->VtxBuffer.Size;
}

void Layer::end(ImVec2 offset, float alpha, float scale, ImVec2 pivot) {
    if (offset.x == 0 && offset.y == 0 && alpha >= 1 && scale == 1) {
        return;
    }
    const auto transform = [&](ImVec2 p) {
        return ImVec2{pivot.x + (p.x - pivot.x) * scale + offset.x,
                      pivot.y + (p.y - pivot.y) * scale + offset.y};
    };
    for (int i = m_firstVertex; i < m_list->VtxBuffer.Size; ++i) {
        ImDrawVert& vertex = m_list->VtxBuffer[i];
        vertex.pos = transform(vertex.pos);
        if (alpha < 1) {
            vertex.col = withAlpha(vertex.col, alpha);
        }
    }
    for (int i = m_firstCommand; i < m_list->CmdBuffer.Size; ++i) {
        ImVec4& clip = m_list->CmdBuffer[i].ClipRect;
        const ImVec2 min = transform({clip.x, clip.y});
        const ImVec2 max = transform({clip.z, clip.w});
        clip = {min.x, min.y, max.x, max.y};
    }
    // Commands added after the layer must not inherit its transformed clip rectangle.
    m_list->AddDrawCmd();
    m_list->CmdBuffer.back().ClipRect = m_list->_ClipRectStack.back();
}

} // namespace sloptube::app::motion
