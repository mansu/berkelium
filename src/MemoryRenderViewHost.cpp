/*  Berkelium Implementation
 *  MemoryRenderViewHost.cpp
 *
 *  Copyright (c) 2009, Patrick Reiter Horn
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are
 *  met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name of Sirikata nor the names of its contributors may
 *    be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER
 * OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "berkelium/Platform.hpp"
#include "WindowImpl.hpp"
#include "MemoryRenderViewHost.hpp"
#include <stdio.h>

#include "chrome/browser/renderer_host/render_widget_host_view.h"
#include "chrome/browser/renderer_host/render_process_host.h"
#include "chrome/common/notification_service.h"
#include "chrome/common/render_messages.h"

namespace Berkelium {

///////// MemoryRenderViewHost /////////

MemoryRenderViewHost::MemoryRenderViewHost(
        SiteInstance* instance,
        RenderViewHostDelegate* delegate,
        int routing_id,
        base::WaitableEvent* modal_dialog_event)
    : RenderViewHost(instance, delegate, routing_id, modal_dialog_event) {

    mWindow = static_cast<WindowImpl*>(delegate);
    mWidget = NULL;
    mResizeAckPending=true;
}

MemoryRenderViewHost::~MemoryRenderViewHost() {
}

void MemoryRenderViewHost::OnMessageReceived(const IPC::Message& msg) {
  bool msg_is_ok = true;
  IPC_BEGIN_MESSAGE_MAP_EX(MemoryRenderViewHost, msg, msg_is_ok)
    IPC_MESSAGE_HANDLER(ViewHostMsg_ScrollRect, Memory_OnMsgScrollRect)
    IPC_MESSAGE_HANDLER(ViewHostMsg_PaintRect, Memory_OnMsgPaintRect)
    IPC_MESSAGE_UNHANDLED(RenderViewHost::OnMessageReceived(msg))
  IPC_END_MESSAGE_MAP_EX()
      ;

  if (!msg_is_ok) {
    // The message had a handler, but its de-serialization failed.
    // Kill the renderer.
    process()->ReceivedBadMessage(msg.type());
  }
}



///////// MemoryRenderWidgetHost /////////

MemoryRenderWidgetHost::MemoryRenderWidgetHost(
        WindowImpl *win,
        RenderWidget *wid,
        RenderProcessHost* host,
        int routing_id)
    : RenderWidgetHost(host, routing_id) {

    mWindow = static_cast<WindowImpl*>(delegate);
    mWidget = wid;

    set_view(wid);
}

MemoryRenderWidgetHost::~MemoryRenderViewHost() {
}

void MemoryRenderWidgetHost::OnMessageReceived(const IPC::Message& msg) {
  bool msg_is_ok = true;
  IPC_BEGIN_MESSAGE_MAP_EX(MemoryRenderViewHost, msg, msg_is_ok)
    IPC_MESSAGE_HANDLER(ViewHostMsg_ScrollRect, Memory_OnMsgScrollRect)
    IPC_MESSAGE_HANDLER(ViewHostMsg_PaintRect, Memory_OnMsgPaintRect)
    IPC_MESSAGE_UNHANDLED(RenderViewHost::OnMessageReceived(msg))
  IPC_END_MESSAGE_MAP_EX()
      ;

  if (!msg_is_ok) {
    // The message had a handler, but its de-serialization failed.
    // Kill the renderer.
    process()->ReceivedBadMessage(msg.type());
  }
}





///////// MemoryRenderHostBase common functions /////////

void MemoryRenderHostBase::Memory_OnMsgScrollRect(
    const ViewHostMsg_ScrollRect_Params& params) {

  DCHECK(!params.view_size.IsEmpty());

  const size_t size = params.bitmap_rect.height() *
                      params.bitmap_rect.width() * 4;
  TransportDIB* dib = process()->GetTransportDIB(params.bitmap);
  if (dib) {
    if (dib->size() < size) {
      LOG(WARNING) << "Transport DIB too small for given rectangle";
      process()->ReceivedBadMessage(ViewHostMsg_PaintRect__ID);
    } else {
      // Scroll the backing store.
      Memory_ScrollBackingStoreRect(
          dib, params.bitmap_rect,
          params.dx, params.dy,
          params.clip_rect);
    }
  }

  // ACK early so we can prefetch the next ScrollRect if there is a next one.
  // This must be done AFTER we're done painting with the bitmap supplied by the
  // renderer. This ACK is a signal to the renderer that the backing store can
  // be re-used, so the bitmap may be invalid after this call.
  Send(new ViewMsg_ScrollRect_ACK(routing_id()));

  // Paint the view. Watch out: it might be destroyed already.
  if (view()) {
    //PRIV//view_being_painted_ = true;
    view()->MovePluginWindows(params.plugin_window_moves);
    view()->DidScrollRect(params.clip_rect, params.dx, params.dy);
    //PRIV//view_being_painted_ = false;
  }

}
void MemoryRenderHostBase::Memory_WasResized() {
    if (mResizeAckPending || !process()->HasConnection() || !view() ||
        !renderer_initialized_) {
        return;
    }
    
    gfx::Rect view_bounds = view()->GetViewBounds();
    gfx::Size new_size(view_bounds.width(), view_bounds.height());
    
    // Avoid asking the RenderWidget to resize to its current size, since it
    // won't send us a PaintRect message in that case.
    if (new_size == current_size_ || current_size_== gfx::Size())
        return;
    
    if (mInFlightSize != gfx::Size())
        return;
    
    // We don't expect to receive an ACK when the requested size is empty.
    if (!new_size.IsEmpty())
        mResizeAckPending = true;
    
    if (!Send(new ViewMsg_Resize(routing_id(), new_size,
                                 GetRootWindowResizerRect())))
        mResizeAckPending = false;
    else
        mInFlightSize = new_size;
}
void MemoryRenderHostBase::Memory_OnMsgPaintRect(
    const ViewHostMsg_PaintRect_Params&params)
{
  current_size_ = params.view_size;

    DCHECK(!params.bitmap_rect.IsEmpty());
    DCHECK(!params.view_size.IsEmpty());

    bool is_resize_ack =
        ViewHostMsg_PaintRect_Flags::is_resize_ack(params.flags);

    // resize_ack_pending_ needs to be cleared before we call DidPaintRect, since
    // that will end up reaching GetBackingStore.
    if (is_resize_ack) {
        assert(mResizeAckPending);
        mResizeAckPending=false;
        mInFlightSize.SetSize(0,0);
        //PRIV//DCHECK(resize_ack_pending_);
        //PRIV//resize_ack_pending_ = false;
        //PRIV//in_flight_size_.SetSize(0, 0);
    }

    bool is_repaint_ack =
        ViewHostMsg_PaintRect_Flags::is_repaint_ack(params.flags);


  DCHECK(!params.bitmap_rect.IsEmpty());
  DCHECK(!params.view_size.IsEmpty());

  const size_t size = params.bitmap_rect.height() *
                      params.bitmap_rect.width() * 4;
  TransportDIB* dib = process()->GetTransportDIB(params.bitmap);
  if (dib) {
    if (dib->size() < size) {
      DLOG(WARNING) << "Transport DIB too small for given rectangle";
      process()->ReceivedBadMessage(ViewHostMsg_PaintRect__ID);
    } else {
      // Paint the backing store. This will update it with the renderer-supplied
      // bits. The view will read out of the backing store later to actually
      // draw to the screen.
      Memory_PaintBackingStoreRect(dib, params.bitmap_rect);
    }
  }

  // ACK early so we can prefetch the next PaintRect if there is a next one.
  // This must be done AFTER we're done painting with the bitmap supplied by the
  // renderer. This ACK is a signal to the renderer that the backing store can
  // be re-used, so the bitmap may be invalid after this call.
  Send(new ViewMsg_PaintRect_ACK(routing_id()));

  // Now paint the view. Watch out: it might be destroyed already.
  if (view()) {
    view()->MovePluginWindows(params.plugin_window_moves);
    //PRIV//view_being_painted_ = true;
    view()->DidPaintRect(params.bitmap_rect);
    //PRIV//view_being_painted_ = false;
  }

  // If we got a resize ack, then perhaps we have another resize to send?
  if (is_resize_ack && view()) {
    gfx::Rect view_bounds = view()->GetViewBounds();
    if (current_size_.width() != view_bounds.width() ||
        current_size_.height() != view_bounds.height()) {
      Memory_WasResized();
    }
  }

}

void MemoryRenderHostBase::Memory_ScrollBackingStoreRect(
    TransportDIB* bitmap,
    const gfx::Rect& bitmap_rect,
    int dx, int dy,
    const gfx::Rect& clip_rect)
{
    Rect updateRect;
    updateRect.mTop = bitmap_rect.y();
    updateRect.mLeft = bitmap_rect.x();
    updateRect.mWidth = bitmap_rect.width();
    updateRect.mHeight = bitmap_rect.height();

    Rect clipRect;
    clipRect.mTop = clip_rect.y();
    clipRect.mLeft = clip_rect.x();
    clipRect.mWidth = clip_rect.width();
    clipRect.mHeight = clip_rect.height();

    mWindow->onPaint(
        static_cast<const unsigned char *>(bitmap->memory()),
        updateRect,
        dx,
        dy,
        clipRect);

}

void MemoryRenderHostBase::Memory_PaintBackingStoreRect(
    TransportDIB* bitmap,
    const gfx::Rect& bitmap_rect)
{
    Memory_ScrollBackingStoreRect(bitmap, bitmap_rect, 0, 0, gfx::Rect());
}




///////// MemoryRenderViewHostFactory /////////

MemoryRenderViewHostFactory::MemoryRenderViewHostFactory() {
    RenderViewHostFactory::RegisterFactory(this);
}

MemoryRenderViewHostFactory::~MemoryRenderViewHostFactory() {
    RenderViewHostFactory::UnregisterFactory();
}

RenderViewHost* MemoryRenderViewHostFactory::CreateRenderViewHost(
    SiteInstance* instance,
    RenderViewHostDelegate* delegate,
    int routing_id,
    base::WaitableEvent* modal_dialog_event)
{
    return new MemoryRenderViewHost(instance, delegate,
                                    routing_id, modal_dialog_event);
}

}

