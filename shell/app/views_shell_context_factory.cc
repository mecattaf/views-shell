// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "views_shell/app/views_shell_context_factory.h"

#include <string>
#include <tuple>
#include <utility>

#include "base/check.h"
#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/logging.h"
#include "base/message_loop/message_pump_type.h"
#include "base/run_loop.h"
#include "base/threading/thread.h"
#include "cc/mojo_embedder/async_layer_tree_frame_sink.h"
#include "cc/raster/single_thread_task_graph_runner.h"
#include "components/viz/common/frame_sinks/begin_frame_source.h"
#include "components/viz/common/gpu/raster_context_provider.h"
#include "components/viz/host/host_display_client.h"
#include "components/viz/host/host_frame_sink_manager.h"
#include "components/viz/host/renderer_settings_creation.h"
#include "components/viz/service/gl/gpu_service_impl.h"
#include "components/viz/service/main/viz_compositor_thread_runner_impl.h"
#include "gpu/command_buffer/client/raster_interface.h"
#include "gpu/command_buffer/client/shared_memory_limits.h"
#include "gpu/command_buffer/common/scheduling_priority.h"
#include "gpu/command_buffer/common/shared_image_capabilities.h"
#include "gpu/command_buffer/common/shm_count.h"
#include "gpu/command_buffer/service/service_utils.h"
#include "gpu/config/gpu_feature_info.h"
#include "gpu/config/gpu_info.h"
#include "gpu/ipc/client/gpu_channel_host.h"
#include "gpu/ipc/common/gpu_client_ids.h"
#include "gpu/ipc/service/gpu_init.h"
#include "mojo/public/cpp/bindings/associated_remote.h"
#include "mojo/public/cpp/bindings/binder_map.h"
#include "mojo/public/cpp/bindings/generic_pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_associated_remote.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/system/message_pipe.h"
#include "services/viz/privileged/mojom/compositing/display_private.mojom.h"
#include "services/viz/privileged/mojom/compositing/frame_sink_manager.mojom.h"
#include "services/viz/privileged/mojom/gl/gpu_host.mojom.h"
#include "services/viz/privileged/mojom/viz_main.mojom.h"
#include "services/viz/public/cpp/gpu/command_buffer_metrics.h"
#include "services/viz/public/cpp/gpu/context_provider_command_buffer.h"
#include "ui/gl/gl_switches.h"
#include "ui/ozone/public/gpu_platform_support_host.h"
#include "ui/ozone/public/ozone_platform.h"
#include "url/gurl.h"

namespace views_shell {

namespace {

// The client id of the shell's FrameSinkIds and of its GPU channel. The shell
// is the only client of its in-process viz, as the browser is of its own
// (VizProcessTransportFactory's kBrowserClientId is 0 as well).
constexpr uint32_t kShellFrameSinkClientId = 0u;
constexpr int32_t kShellGpuChannelClientId = 1;
constexpr uint64_t kShellGpuChannelTracingId = 1u;

// The Ozone GPU-host id the in-process service is announced under.
constexpr int kShellGpuHostId = 1;

// Stream ids and priorities of the raster contexts, after
// content/public/common/gpu_stream_constants.h (not linked).
constexpr int32_t kGpuStreamIdDefault = 0;
constexpr gpu::SchedulingPriority kGpuStreamPriorityUI =
    gpu::SchedulingPriority::kHigh;

scoped_refptr<viz::ContextProviderCommandBuffer> CreateContextProvider(
    scoped_refptr<gpu::GpuChannelHost> gpu_channel_host,
    bool supports_locking,
    viz::command_buffer_metrics::ContextType type) {
  constexpr bool kAutomaticFlushes = false;
  return viz::ContextProviderCommandBuffer::CreateForRaster(
      std::move(gpu_channel_host), kGpuStreamIdDefault, kGpuStreamPriorityUI,
      GURL("chrome://gpu/ViewsShellContextFactory"), kAutomaticFlushes,
      supports_locking, gpu::SharedMemoryLimits::ForDisplayCompositor(), type);
}

bool IsContextLost(viz::RasterContextProvider* context_provider) {
  return context_provider->RasterInterface()->GetGraphicsResetStatusKHR() !=
         GL_NO_ERROR;
}

bool IsWorkerContextLost(viz::RasterContextProvider* context_provider) {
  viz::RasterContextProvider::ScopedRasterContextLock lock(context_provider);
  return lock.RasterInterface()->GetGraphicsResetStatusKHR() != GL_NO_ERROR;
}

// What GpuServiceImpl::EstablishGpuChannel answers.
struct ChannelReply {
  bool success = false;
  gpu::GPUInfo gpu_info;
  gpu::GpuFeatureInfo gpu_feature_info;
  gpu::SharedImageCapabilities shared_image_capabilities;
};

}  // namespace

const char* CompositingModeName(CompositingMode mode) {
  switch (mode) {
    case CompositingMode::kSoftware:
      return "software";
    case CompositingMode::kGpu:
      return "gpu";
  }
  return "unknown";
}

// The service side of the in-process viz: the GPU main thread, the GPU IO
// thread, and what lives on them. Created and destroyed on the UI thread;
// every member below the threads is created and destroyed on the GPU main
// thread (TearDown()).
class ViewsShellContextFactory::InProcessGpu {
 public:
  InProcessGpu()
      : gpu_main_thread_("ViewsShellGpuMain"),
        gpu_io_thread_("ViewsShellGpuIO") {}
  InProcessGpu(const InProcessGpu&) = delete;
  InProcessGpu& operator=(const InProcessGpu&) = delete;
  ~InProcessGpu() { TearDown(); }

  // Starts both threads, then on the GPU main thread: Ozone's GPU-side
  // interfaces, GpuInit (GL per the command line), GpuServiceImpl and the
  // FrameSinkManagerImpl. Returns once all of it exists.
  void Start(viz::mojom::FrameSinkManagerParamsPtr params) {
    base::Thread::Options gpu_options;
    gpu_options.message_pump_type = ui::OzonePlatform::GetInstance()
                                        ->GetPlatformProperties()
                                        .message_pump_type_for_gpu;
    CHECK(gpu_main_thread_.StartWithOptions(std::move(gpu_options)));
    CHECK(gpu_io_thread_.StartWithOptions(
        base::Thread::Options(base::MessagePumpType::IO, 0)));

    base::RunLoop run_loop;
    gpu_main_thread_.task_runner()->PostTaskAndReply(
        FROM_HERE,
        base::BindOnce(&InProcessGpu::StartOnGpuThread, base::Unretained(this),
                       std::move(params)),
        run_loop.QuitClosure());
    run_loop.Run();
  }

  // Asks the GPU service for a channel for the shell's client end. Runs a
  // nested RunLoop until the GPU main thread has answered.
  ChannelReply EstablishChannel(mojo::ScopedMessagePipeHandle service_end) {
    ChannelReply reply;
    base::RunLoop run_loop;
    gpu_main_thread_.task_runner()->PostTaskAndReply(
        FROM_HERE,
        base::BindOnce(&InProcessGpu::EstablishChannelOnGpuThread,
                       base::Unretained(this), std::move(service_end),
                       base::Unretained(&reply)),
        run_loop.QuitClosure());
    run_loop.Run();
    return reply;
  }

  // Ozone's GPU-host binder: the UI-side platform (on Wayland the
  // WaylandBufferManagerConnector) binds its GPU-side interfaces through it.
  void BindInterface(const std::string& interface_name,
                     mojo::ScopedMessagePipeHandle interface_pipe) {
    mojo::GenericPendingReceiver receiver(interface_name,
                                          std::move(interface_pipe));
    CHECK(binders_.TryBind(&receiver))
        << "no GPU-side binder for " << interface_name;
  }

 private:
  void StartOnGpuThread(viz::mojom::FrameSinkManagerParamsPtr params) {
    // The platform's GPU-side interfaces (WaylandBufferManagerGpu) bind on
    // this thread, as they do on the GPU main thread of a GPU process.
    ui::OzonePlatform::GetInstance()->AddInterfaces(&binders_);

    base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
    gpu_init_ = std::make_unique<gpu::GpuInit>();
    gpu_init_->InitializeInProcess(
        command_line, gpu::gles2::ParseGpuPreferences(command_line));

    viz::GpuServiceImpl::InitParams init_params;
    init_params.watchdog_thread = gpu_init_->TakeWatchdogThread();
    init_params.io_runner = gpu_io_thread_.task_runner();
    init_params.vulkan_implementation = gpu_init_->vulkan_implementation();
#if BUILDFLAG(SKIA_USE_DAWN)
    init_params.dawn_context_provider = gpu_init_->TakeDawnContextProvider();
#endif
    gpu_service_ = std::make_unique<viz::GpuServiceImpl>(
        gpu_init_->gpu_preferences(), gpu_init_->gpu_info(),
        gpu_init_->gpu_feature_info(), gpu_init_->gpu_info_for_hardware_gpu(),
        gpu_init_->gpu_feature_info_for_hardware_gpu(),
        gpu_init_->gpu_extra_info(), std::move(init_params));

    // There is no GPU process host: the service reports to nobody, and a
    // fault ends the process (docs/architecture.md §3).
    mojo::PendingRemote<viz::mojom::GpuHost> gpu_host;
    std::ignore = gpu_host.InitWithNewPipeAndPassReceiver();
    gpu_service_->InitializeWithHost(
        std::move(gpu_host), gpu::GpuProcessShmCount(),
        gpu_init_->TakeDefaultOffscreenSurface(),
        viz::mojom::GpuServiceCreationParams::New());

    runner_ = std::make_unique<viz::VizCompositorThreadRunnerImpl>();
    runner_->CreateFrameSinkManager(std::move(params), gpu_service_.get());
  }

  void EstablishChannelOnGpuThread(mojo::ScopedMessagePipeHandle service_end,
                                   ChannelReply* reply) {
    // On the GPU main thread EstablishGpuChannel runs synchronously and
    // answers before it returns.
    gpu_service_->EstablishGpuChannel(
        kShellGpuChannelClientId, kShellGpuChannelTracingId,
        /*is_gpu_host=*/true, /*enable_extra_handles_validation=*/false,
        std::move(service_end),
        base::BindOnce(
            [](ChannelReply* reply, bool success, const gpu::GPUInfo& gpu_info,
               const gpu::GpuFeatureInfo& gpu_feature_info,
               const gpu::SharedImageCapabilities& shared_image_capabilities) {
              reply->success = success;
              reply->gpu_info = gpu_info;
              reply->gpu_feature_info = gpu_feature_info;
              reply->shared_image_capabilities = shared_image_capabilities;
            },
            base::Unretained(reply)));
  }

  void TearDownOnGpuThread() {
    // The runner stops the viz compositor thread (and with it the
    // FrameSinkManagerImpl) before the GPU service it uses goes away.
    runner_.reset();
    gpu_service_.reset();
    gpu_init_.reset();
  }

  void TearDown() {
    if (!gpu_main_thread_.IsRunning()) {
      return;
    }
    gpu_main_thread_.task_runner()->PostTask(
        FROM_HERE, base::BindOnce(&InProcessGpu::TearDownOnGpuThread,
                                  base::Unretained(this)));
    // GpuServiceImpl's destructor waits on the GPU IO thread, so that thread
    // stops last.
    gpu_main_thread_.Stop();
    gpu_io_thread_.Stop();
  }

  base::Thread gpu_main_thread_;
  base::Thread gpu_io_thread_;
  mojo::BinderMap binders_;
  std::unique_ptr<gpu::GpuInit> gpu_init_;
  std::unique_ptr<viz::GpuServiceImpl> gpu_service_;
  std::unique_ptr<viz::VizCompositorThreadRunnerImpl> runner_;
};

ViewsShellContextFactory::ViewsShellContextFactory(CompositingMode mode)
    : mode_(mode),
      task_graph_runner_(std::make_unique<cc::SingleThreadTaskGraphRunner>()),
      frame_sink_id_allocator_(kShellFrameSinkClientId) {
  task_graph_runner_->Start("CompositorTileWorker1",
                            base::SimpleThread::Options());
}

ViewsShellContextFactory::~ViewsShellContextFactory() {
  // Every compositor has gone (aura::Env is torn down first).
  DCHECK(display_clients_.empty());
  display_clients_.clear();
  main_context_provider_.reset();
  worker_context_provider_.reset();
  task_graph_runner_->Shutdown();
  // The host side lets go of the service before the service stops, so the
  // connection-lost callback never fires on an orderly exit.
  host_frame_sink_manager_.reset();
  if (gpu_platform_connected_) {
    if (auto* host =
            ui::OzonePlatform::GetInstance()->GetGpuPlatformSupportHost()) {
      host->OnChannelDestroyed(kShellGpuHostId);
    }
  }
  if (gpu_channel_host_) {
    gpu_channel_host_->DestroyChannel();
    gpu_channel_host_.reset();
  }
  gpu_.reset();
}

bool ViewsShellContextFactory::Initialize(
    scoped_refptr<base::SingleThreadTaskRunner> io_task_runner) {
  DCHECK(!gpu_);
  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
  if (mode_ == CompositingMode::kSoftware) {
    // Software compositing needs no GL: the display draws with Skia into the
    // Ozone canvas surface. GL off also keeps the GPU service from loading
    // ANGLE (as components/viz/demo does without --viz-demo-use-gpu).
    command_line->AppendSwitchASCII(switches::kUseGL,
                                    gl::kGLImplementationDisabledName);
  }

  // The frame sink manager's two pipes: the host keeps the FrameSinkManager
  // remote and the FrameSinkManagerClient receiver, the service the others.
  mojo::PendingRemote<viz::mojom::FrameSinkManager> frame_sink_manager;
  mojo::PendingReceiver<viz::mojom::FrameSinkManager>
      frame_sink_manager_receiver =
          frame_sink_manager.InitWithNewPipeAndPassReceiver();
  mojo::PendingRemote<viz::mojom::FrameSinkManagerClient>
      frame_sink_manager_client;
  mojo::PendingReceiver<viz::mojom::FrameSinkManagerClient>
      frame_sink_manager_client_receiver =
          frame_sink_manager_client.InitWithNewPipeAndPassReceiver();

  auto params = viz::mojom::FrameSinkManagerParams::New();
  params->restart_id = viz::BeginFrameSource::kNotRestartableId;
  params->use_activation_deadline = false;
  params->activation_deadline_in_frames = 0u;
  params->frame_sink_manager = std::move(frame_sink_manager_receiver);
  params->frame_sink_manager_client = std::move(frame_sink_manager_client);

  gpu_ = std::make_unique<InProcessGpu>();
  gpu_->Start(std::move(params));

  // The platform's UI side connects to its GPU side through the in-process
  // service (WaylandBufferManagerHost <-> WaylandBufferManagerGpu), as
  // viz::GpuHostImpl does for a GPU process.
  if (auto* host =
          ui::OzonePlatform::GetInstance()->GetGpuPlatformSupportHost()) {
    host->OnGpuServiceLaunched(
        kShellGpuHostId,
        base::BindRepeating(&InProcessGpu::BindInterface,
                            base::Unretained(gpu_.get())),
        base::BindOnce([](const std::string& message) {
          LOG(FATAL) << "the platform asked to end the GPU service: "
                     << message;
        }));
    gpu_platform_connected_ = true;
  }

  host_frame_sink_manager_ = std::make_unique<viz::HostFrameSinkManager>();
  host_frame_sink_manager_->BindAndSetManager(
      std::move(frame_sink_manager_client_receiver), nullptr,
      std::move(frame_sink_manager));
  host_frame_sink_manager_->SetConnectionLostCallback(
      base::BindRepeating([] {
        LOG(FATAL) << "lost the in-process viz; the shell exits so its unit "
                      "restarts it";
      }));

  mojo::MessagePipe pipe;
  ChannelReply reply = gpu_->EstablishChannel(std::move(pipe.handle1));
  if (!reply.success) {
    LOG(ERROR) << "the in-process GPU service refused the shell's channel";
    return false;
  }
  gpu_channel_host_ = gpu::GpuChannelHost::Create(
      kShellGpuChannelClientId, reply.gpu_info, reply.gpu_feature_info,
      reply.shared_image_capabilities, std::move(pipe.handle0),
      std::move(io_task_runner));

  LOG(INFO) << "in-process viz up: " << CompositingModeName(mode_)
            << " compositing, GL renderer '" << reply.gpu_info.gl_renderer
            << "'";
  return true;
}

bool ViewsShellContextFactory::EnsureGpuContexts() {
  if (worker_context_provider_ &&
      IsWorkerContextLost(worker_context_provider_.get())) {
    worker_context_provider_.reset();
  }
  if (!worker_context_provider_) {
    auto worker = CreateContextProvider(
        gpu_channel_host_, /*supports_locking=*/true,
        viz::command_buffer_metrics::ContextType::BROWSER_RASTER_WORKER);
    gpu::ContextResult result = worker->BindToCurrentSequence();
    if (result != gpu::ContextResult::kSuccess) {
      LOG(ERROR) << "worker raster context failed to bind";
      return false;
    }
    worker_context_provider_ = std::move(worker);
  }

  if (main_context_provider_ && IsContextLost(main_context_provider_.get())) {
    main_context_provider_.reset();
  }
  if (!main_context_provider_) {
    auto main = CreateContextProvider(
        gpu_channel_host_, /*supports_locking=*/false,
        viz::command_buffer_metrics::ContextType::BROWSER_MAIN_THREAD);
    gpu::ContextResult result = main->BindToCurrentSequence();
    if (result != gpu::ContextResult::kSuccess) {
      LOG(ERROR) << "main-thread raster context failed to bind";
      return false;
    }
    main_context_provider_ = std::move(main);
  }
  return true;
}

void ViewsShellContextFactory::CreateLayerTreeFrameSink(
    base::WeakPtr<ui::Compositor> compositor_weak_ptr) {
  ui::Compositor* compositor = compositor_weak_ptr.get();
  if (!compositor) {
    return;
  }
  CHECK(gpu_channel_host_) << "Initialize() must succeed before compositing";

  bool gpu_compositing = mode_ == CompositingMode::kGpu &&
                         !compositor->force_software_compositor();
  if (gpu_compositing && !EnsureGpuContexts()) {
    // GPU mode was asked for and cannot bind its contexts: say so and fall
    // back for this compositor rather than draw nothing.
    LOG(ERROR) << "GPU compositing unavailable, this compositor draws in "
                  "software";
    gpu_compositing = false;
  }

  scoped_refptr<viz::RasterContextProvider> context_provider;
  scoped_refptr<viz::RasterContextProvider> worker_context_provider;
  if (gpu_compositing) {
    context_provider = main_context_provider_;
    worker_context_provider = worker_context_provider_;
  }

  auto root_params = viz::mojom::RootCompositorFrameSinkParams::New();
  mojo::PendingAssociatedRemote<viz::mojom::CompositorFrameSink> sink_remote;
  root_params->compositor_frame_sink =
      sink_remote.InitWithNewEndpointAndPassReceiver();
  mojo::PendingReceiver<viz::mojom::CompositorFrameSinkClient> client_receiver =
      root_params->compositor_frame_sink_client
          .InitWithNewPipeAndPassReceiver();
  mojo::AssociatedRemote<viz::mojom::DisplayPrivate> display_private;
  root_params->display_private =
      display_private.BindNewEndpointAndPassReceiver();
  auto display_client =
      std::make_unique<viz::HostDisplayClient>(compositor->widget());
  root_params->display_client = display_client->GetBoundRemote(nullptr);
  display_clients_[compositor] = std::move(display_client);

  root_params->frame_sink_id = compositor->frame_sink_id();
  root_params->widget = compositor->widget();
  root_params->gpu_compositing = gpu_compositing;
  root_params->renderer_settings = viz::CreateRendererSettings();
  host_frame_sink_manager_->CreateRootCompositorFrameSink(
      std::move(root_params));

  cc::mojo_embedder::AsyncLayerTreeFrameSink::InitParams params;
  params.compositor_task_runner = compositor->task_runner();
  params.pipes.compositor_frame_sink_associated_remote = std::move(sink_remote);
  params.pipes.client_receiver = std::move(client_receiver);

  // Software compositing allocates its tiles and UI resources as shared-memory
  // SharedImages, so the shared image interface is needed in both modes.
  auto frame_sink = std::make_unique<cc::mojo_embedder::AsyncLayerTreeFrameSink>(
      std::move(context_provider), std::move(worker_context_provider),
      gpu_channel_host_->CreateClientSharedImageInterface(), &params);
  compositor->SetLayerTreeFrameSink(std::move(frame_sink),
                                    std::move(display_private));
}

scoped_refptr<viz::RasterContextProvider>
ViewsShellContextFactory::SharedMainThreadRasterContextProvider() {
  if (mode_ != CompositingMode::kGpu || !gpu_channel_host_) {
    return nullptr;
  }
  if (!EnsureGpuContexts()) {
    return nullptr;
  }
  return main_context_provider_;
}

void ViewsShellContextFactory::RemoveCompositor(ui::Compositor* compositor) {
  display_clients_.erase(compositor);
}

cc::TaskGraphRunner* ViewsShellContextFactory::GetTaskGraphRunner() {
  return task_graph_runner_.get();
}

viz::FrameSinkId ViewsShellContextFactory::AllocateFrameSinkId() {
  return frame_sink_id_allocator_.NextFrameSinkId();
}

viz::SubtreeCaptureId ViewsShellContextFactory::AllocateSubtreeCaptureId() {
  return subtree_capture_id_allocator_.NextSubtreeCaptureId();
}

viz::HostFrameSinkManager* ViewsShellContextFactory::GetHostFrameSinkManager() {
  return host_frame_sink_manager_.get();
}

}  // namespace views_shell
