#include <SDL3/SDL.h>
#include <cstddef>
#include <emscripten.h>
#include <numbers>
#include <print>
#include <span>
#include <webgpu/webgpu_cpp.h>
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_wgpu.h"

static SDL_Window*         window;
static wgpu::Instance      instance;
static wgpu::Adapter       adapter;
static wgpu::Device        device;
static wgpu::Surface       surface;
static wgpu::TextureFormat sf_format;

EM_ASYNC_JS(void, wait_next_frame, (), { await new Promise(requestAnimationFrame); });

static void frame() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) ImGui_ImplSDL3_ProcessEvent(&e);

    ImGui_ImplWGPU_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    ImGui::ShowDemoWindow();
    ImGui::Render();

    wgpu::CommandEncoderDescriptor ed{.label = "my encoder"};
    wgpu::CommandEncoder           encoder = device.CreateCommandEncoder(&ed);

    // Draw an triangle
    {
        // 1. Create shader module
        static constexpr char code[] = {
#embed "shaders/render.wgsl"


        };
        static wgpu::ShaderSourceWGSL wgsl;
        wgsl.code = std::string_view{code, sizeof(code)};

        wgpu::ShaderModuleDescriptor shader_desc{
            .nextInChain = &wgsl,
            .label       = "our hardcoded red triangle shaders",
        };
        wgpu::ShaderModule shader = device.CreateShaderModule(&shader_desc);

        // 2. Create pipeline that bind shader module with target
        wgpu::ColorTargetState         target{.format = sf_format};
        wgpu::FragmentState            fs{.module = shader, .entryPoint = "fs", .targetCount = 1, .targets = &target};
        wgpu::RenderPipelineDescriptor pd{
            .label    = "triangle pipeline",
            .layout   = nullptr,
            .vertex   = wgpu::VertexState{.module = shader, .entryPoint = "vs"},
            .fragment = &fs

        };
        auto pipeline = device.CreateRenderPipeline(&pd);

        // 3. Create render pass
        wgpu::SurfaceTexture st;
        surface.GetCurrentTexture(&st);
        wgpu::RenderPassColorAttachment color{
            .view       = st.texture.CreateView(),
            .loadOp     = wgpu::LoadOp::Clear,
            .storeOp    = wgpu::StoreOp::Store,
            .clearValue = {0.45, 0.55, 0.60, 1.0},
        };
        wgpu::RenderPassDescriptor desc{.label = "render pass", .colorAttachmentCount = 1, .colorAttachments = &color};
        wgpu::RenderPassEncoder    pass = encoder.BeginRenderPass(&desc);

        // 4. Encode commands
        pass.SetPipeline(pipeline);
        pass.Draw(3);
        // ImGui_ImplWGPU_RenderDrawData(ImGui::GetDrawData(), pass.Get());
        pass.End();
    }

    static std::vector<float>    input{1, 3, 5};
    auto                         input_byte_size = static_cast<uint64_t>(input.size() * sizeof(float));
    const wgpu::BufferDescriptor wbd{
        .label = "work buffer",
        .usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopySrc | wgpu::BufferUsage::CopyDst,
        .size  = input_byte_size
    };
    const auto work_buffer = device.CreateBuffer(&wbd);
    device.GetQueue().WriteBuffer(work_buffer, 0, input.data(), input_byte_size);

    const wgpu::BufferDescriptor rbd{
        .label = "result buffer",
        .usage = wgpu::BufferUsage::MapRead | wgpu::BufferUsage::CopyDst,
        .size  = input_byte_size
    };
    const auto result_buffer = device.CreateBuffer(&rbd);

    {
        static wgpu::ShaderSourceWGSL wgsl;
        static constexpr char code[] = {
#embed "shaders/compute.wgsl"

        };
        wgsl.code = std::string_view{code, sizeof(code)};

        const wgpu::ShaderModuleDescriptor shader_desc{
            .nextInChain = &wgsl,
            .label       = "compute shader",
        };
        const wgpu::ShaderModule              shader = device.CreateShaderModule(&shader_desc);
        const wgpu::ComputePipelineDescriptor pd{
            .label   = "compute pipeline",
            .layout  = nullptr,
            .compute = wgpu::ComputeState{.module = shader}
        };
        auto pipeline = device.CreateComputePipeline(&pd);

        const wgpu::BindGroupEntry      bge{.binding = 0, .buffer = work_buffer};
        const wgpu::BindGroupDescriptor bg_desc{
            .label      = "bindgroup work buffer",
            .layout     = pipeline.GetBindGroupLayout(0),
            .entryCount = 1,
            .entries    = &bge
        };
        const auto bind_group = device.CreateBindGroup(&bg_desc);

        const wgpu::ComputePassDescriptor cpd{.label = "compute pass"};
        const auto                        pass = encoder.BeginComputePass(&cpd);
        pass.SetPipeline(pipeline);
        pass.SetBindGroup(0, bind_group);
        pass.DispatchWorkgroups(input.size());
        pass.End();

        encoder.CopyBufferToBuffer(work_buffer, 0, result_buffer, 0, result_buffer.GetSize());
    }

    wgpu::CommandBuffer cmd = encoder.Finish();
    device.GetQueue().Submit(1, &cmd);

    auto s = instance.WaitAny(
        result_buffer.MapAsync(
            wgpu::MapMode::Read,
            0,
            wgpu::kWholeMapSize,
            wgpu::CallbackMode::WaitAnyOnly,
            [](wgpu::MapAsyncStatus, wgpu::StringView) {}
        ),
        UINT64_MAX
    );

    assert(s == wgpu::WaitStatus::Success);
    std::span result(static_cast<const float*>(result_buffer.GetConstMappedRange()), input.size());
    std::println("input {} result {}", input, result);
    result_buffer.Unmap();
    std::println("input {} result {}", input, result);
}

static void init() {
    wgpu::SurfaceCapabilities caps;
    surface.GetCapabilities(adapter, &caps);
    int w, h;
    SDL_GetWindowSizeInPixels(window, &w, &h);
    sf_format = caps.formats[0];
    wgpu::SurfaceConfiguration config{
        .device = device,
        // The same with navigator.gpu.getPreferredCanvasFormat()
        .format = sf_format,
        .width  = uint32_t(w),
        .height = uint32_t(h),
    };
    surface.Configure(&config);

    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui_ImplSDL3_InitForOther(window);
    ImGui_ImplWGPU_InitInfo info;
    info.Device             = device.Get();
    info.RenderTargetFormat = WGPUTextureFormat(config.format);
    ImGui_ImplWGPU_Init(&info);
}

int main() {
    SDL_Init(SDL_INIT_VIDEO);
    window = SDL_CreateWindow("imgui", 1280, 720, 0);

    wgpu::InstanceFeatureName timed_wait_any = wgpu::InstanceFeatureName::TimedWaitAny;
    wgpu::InstanceDescriptor  instance_desc{.requiredFeatureCount = 1, .requiredFeatures = &timed_wait_any};
    instance = wgpu::CreateInstance(&instance_desc);
    wgpu::EmscriptenSurfaceSourceCanvasHTMLSelector canvas;
    canvas.selector = "#canvas";
    wgpu::SurfaceDescriptor surface_desc{.nextInChain = &canvas};
    surface = instance.CreateSurface(&surface_desc);

    auto s = instance.WaitAny(
        instance.RequestAdapter(
            nullptr,
            wgpu::CallbackMode::WaitAnyOnly,
            [](wgpu::RequestAdapterStatus, wgpu::Adapter a, wgpu::StringView) { adapter = std::move(a); }
        ),
        UINT64_MAX
    );
    assert(s == wgpu::WaitStatus::Success);
    s= instance.WaitAny(
        adapter.RequestDevice(
            nullptr,
            wgpu::CallbackMode::WaitAnyOnly,
            [](wgpu::RequestDeviceStatus, wgpu::Device d, wgpu::StringView) { device = std::move(d); }
        ),
        UINT64_MAX
    );
    assert(s == wgpu::WaitStatus::Success);
    init();

    while (true) {
        frame();
        wait_next_frame();
    }
}
