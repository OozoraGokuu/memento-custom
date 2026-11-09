////////////////////////////////////////////////////////////////////////////////
//
// Copyright (c) 2026 Ripose
//
// This file is part of Memento.
//
// Memento is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2 of the License.
//
// Memento is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Memento.  If not, see <https://www.gnu.org/licenses/>.
//
////////////////////////////////////////////////////////////////////////////////

#include "player/mpvrenderer.h"

#include <QQuickWindow>
#include <QOpenGLFramebufferObject>

MpvRenderer::MpvRenderer(MpvPlayer *player) :
    m_player{player},
    m_frameBackend{player}
{

}

QOpenGLFramebufferObject *MpvRenderer::createFramebufferObject(
    const QSize &size)
{
    if (m_player->renderContext() == nullptr)
    {
        m_player->createRenderContext();
    }
    return QQuickFramebufferObject::Renderer::createFramebufferObject(size);
}

void MpvRenderer::synchronize(QQuickFramebufferObject *item)
{
    m_window = item->window();
}

void MpvRenderer::render()
{
    // A popup can detach its items while a final render is still queued.
    // Do not read item->window() from this render callback: it can become null
    // between beginExternalCommands() and endExternalCommands().
    if (!m_window) return;
    m_window->beginExternalCommands();

    QOpenGLFramebufferObject *fbo = framebufferObject();
    m_frameBackend.renderToFramebuffer(fbo->handle(), fbo->size());

    m_window->endExternalCommands();
}
