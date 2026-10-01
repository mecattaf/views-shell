// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// The production ui::ContextFactory of views-shell (docs/architecture.md §3,
// debt D1 paid): an in-process viz host, with no test support.
//
// Service side, on a GPU main thread of its own: gpu::GpuInit, a
// viz::GpuServiceImpl and a viz::VizCompositorThreadRunnerImpl that owns the
// viz::FrameSinkManagerImpl on the viz compositor thread. This is the shape of
// components/viz/demo/service/demo_service.cc and of the GPU thread of
// components/viz/service/main/viz_main_impl.cc, run inside the shell process.
//
// Host side, on the UI thread: a viz::HostFrameSinkManager bound to that
// FrameSinkManagerImpl over mojo, and one gpu::GpuChannelHost to the in-process
// GpuServiceImpl, which gives the client its SharedImageInterface (software
// compositing allocates its shared-memory tiles through it) and, in GPU mode,
// its raster context providers.
//
// Per ui::Compositor: a root CompositorFrameSink created through
// HostFrameSinkManager::CreateRootCompositorFrameSink (the compositor's
// AcceleratedWidget, a DisplayPrivate, a viz::HostDisplayClient) and a
// cc::mojo_embedder::AsyncLayerTreeFrameSink on the client end, as
// content/browser/compositor/viz_process_transport_factory.cc does. The root
// sink forwards the compositor's parent LocalSurfaceId to viz::Display, so the
// Wayland window's configure sequence latches and ack_configure is sent
// (SPEC.md finding F2): no test platform-window config is needed.
//
// A lost viz connection ends the process: the shell's user unit restarts it
// and the layer surfaces come back (docs/architecture.md §3).

#ifndef VIEWS_SHELL_APP_VIEWS_SHELL_CONTEXT_FACTORY_H_
#define VIEWS_SHELL_APP_VIEWS_SHELL_CONTEXT_FACTORY_H_

#include <map>
#include <memory>

#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/task/single_thread_task_runner.h"
#include "components/viz/common/surfaces/frame_sink_id_allocator.h"
#include "components/viz/common/surfaces/subtree_capture_id_allocator.h"
#include "ui/compositor/compositor.h"

namespace cc {
class SingleThreadTaskGraphRunner;
}

namespace gpu {
class GpuChannelHost;
}

namespace viz {
class ContextProviderCommandBuffer;
class HostDisplayClient;
class HostFrameSinkManager;
}  // namespace viz

namespace views_shell {

// How the display compositor draws. kSoftware: viz's software renderer into
// the Ozone canvas surface (wl_shm buffers on Wayland), no GL at all. kGpu:
// SkiaRenderer over GL (ANGLE; SwiftShader on the headless bench).
enum class CompositingMode {
  kSoftware,
  kGpu,
};

const char* CompositingModeName(CompositingMode mode);

class ViewsShellContextFactory : public ui::ContextFactory {
 public:
  explicit ViewsShellContextFactory(CompositingMode mode);
  ViewsShellContextFactory(const ViewsShellContextFactory&) = delete;
  ViewsShellContextFactory& operator=(const ViewsShellContextFactory&) =
      delete;
  ~ViewsShellContextFactory() override;

  // Starts the GPU main and GPU IO threads, initialises the GPU service and
  // the frame sink manager on them, connects the HostFrameSinkManager and
  // establishes the GPU channel. Runs a nested RunLoop on the calling (UI)
  // thread until the service side answers. `io_task_runner` is the process's
  // mojo IO thread, which the GPU channel's client end listens on. Returns
  // false if the GPU channel could not be established.
  bool Initialize(scoped_refptr<base::SingleThreadTaskRunner> io_task_runner);

  CompositingMode mode() const { return mode_; }

  // ui::ContextFactory:
  void CreateLayerTreeFrameSink(
      base::WeakPtr<ui::Compositor> compositor) override;
  scoped_refptr<viz::RasterContextProvider>
  SharedMainThreadRasterContextProvider() override;
  void RemoveCompositor(ui::Compositor* compositor) override;
  cc::TaskGraphRunner* GetTaskGraphRunner() override;
  viz::FrameSinkId AllocateFrameSinkId() override;
  viz::SubtreeCaptureId AllocateSubtreeCaptureId() override;
  viz::HostFrameSinkManager* GetHostFrameSinkManager() override;

 private:
  // The service side: everything that lives on the GPU main thread.
  class InProcessGpu;

  // GPU mode: binds the main-thread and worker raster contexts on the GPU
  // channel. Returns false (and logs) when either fails to bind.
  bool EnsureGpuContexts();

  const CompositingMode mode_;
  std::unique_ptr<InProcessGpu> gpu_;
  std::unique_ptr<viz::HostFrameSinkManager> host_frame_sink_manager_;
  scoped_refptr<gpu::GpuChannelHost> gpu_channel_host_;
  scoped_refptr<viz::ContextProviderCommandBuffer> main_context_provider_;
  scoped_refptr<viz::ContextProviderCommandBuffer> worker_context_provider_;
  std::unique_ptr<cc::SingleThreadTaskGraphRunner> task_graph_runner_;
  viz::FrameSinkIdAllocator frame_sink_id_allocator_;
  viz::SubtreeCaptureIdAllocator subtree_capture_id_allocator_;
  // One display client per compositor that holds a root frame sink.
  std::map<raw_ptr<ui::Compositor>, std::unique_ptr<viz::HostDisplayClient>>
      display_clients_;
  bool gpu_platform_connected_ = false;
};

}  // namespace views_shell

#endif  // VIEWS_SHELL_APP_VIEWS_SHELL_CONTEXT_FACTORY_H_
