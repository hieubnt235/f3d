#include <SDL3/SDL.h>
#include <cstddef>
#include <emscripten.h>
#include <numbers>
#include <print>
#include <span>
#include <webgpu/webgpu_cpp.h>
#include "SDL3/SDL_video.h"
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

// 2.1 Uniform Draw data
/*
    struct DrawData {
        color: vec4f,
        resolution: vec2f,
    }
*/
struct DrawData {
        alignas(16) float color[4]; // make this align 16 like wgsl vec4f instead of 4 by default
        float resolution[2];
        float trans_px[2]    = {0.0, 0.0};
        float rot_cos_sin[2] = {1.0, 0.0};
        float scale[2]       = {1.0, 1.0};
};
// size is pad so that size = roundup(align of max elem in S, just past last member ), like wgsl
static_assert(sizeof(DrawData) == 48);

static void frame() {

    // Create command encoder for this frame
    wgpu::CommandEncoderDescriptor ed{.label = "my encoder"};
    wgpu::CommandEncoder           encoder = device.CreateCommandEncoder(&ed);

    // Create render pass
    wgpu::SurfaceTexture st;
    surface.GetCurrentTexture(&st);
    wgpu::RenderPassColorAttachment color{
        .view       = st.texture.CreateView(),
        .loadOp     = wgpu::LoadOp::Load,
        .storeOp    = wgpu::StoreOp::Store,
        .clearValue = {0.45, 0.55, 0.60, 1.0},
    };
    wgpu::RenderPassDescriptor desc{.label = "render pass", .colorAttachmentCount = 1, .colorAttachments = &color};
    wgpu::RenderPassEncoder    pass = encoder.BeginRenderPass(&desc);

    // Create webgpu render data
    static DrawData data{
        .color       = {0, 1, 0.5, 0.5},
        .resolution  = {100, 100},
        .trans_px    = {0, 0},
        .rot_cos_sin = {1, 0},
        .scale       = {1.0, 1.0}
    };
    using Vertex = std::array<float, 2>;
    static_assert(sizeof(Vertex) == 8);
    // F data
    static constexpr std::array<Vertex, 12> vertex_data{{
        // left column
        {0, 0},
        {30, 0},
        {0, 150},
        {30, 150},

        // top rung
        {30, 0},
        {100, 0},
        {30, 30},
        {100, 30},

        // middle rung
        {30, 60},
        {70, 60},
        {30, 90},
        {70, 90},
    }};

    static constexpr std::array<uint32_t, 18> index_data{
        // left column
        0,
        1,
        2,
        2,
        1,
        3,

        // top rung
        4,
        5,
        6,
        6,
        5,
        7,

        // middle rung
        8,
        9,
        10,
        10,
        9,
        11,
    };

    // Webgpu render pass
    {
        // 1. Create render pipeline
        // 1.1 Shader module
        static constexpr char code[] = {
#embed "shaders/3d.wgsl"
        };
        wgpu::ShaderSourceWGSL wgsl{};
        wgsl.code = std::string_view(code, sizeof(code));
        wgpu::ShaderModuleDescriptor smd{.nextInChain = &wgsl, .label = "shader module"};
        const auto                   shader_module = device.CreateShaderModule(&smd);

        // 1.2 Vertex state
        wgpu::VertexAttribute    vtx_attr{.format = wgpu::VertexFormat::Float32x2, .offset = 0, .shaderLocation = 0};
        wgpu::VertexBufferLayout vtx_buffer_layout{
            .arrayStride    = sizeof(Vertex),
            .attributeCount = 1,
            .attributes     = &vtx_attr
        };
        wgpu::VertexState
            vertex_state{.module = shader_module, .entryPoint = "vs", .bufferCount = 1, .buffers = &vtx_buffer_layout};

        // 1.3 Fragment state
        wgpu::ColorTargetState target{.format = sf_format};
        wgpu::FragmentState
            fragment_state{.module = shader_module, .entryPoint = "fs", .targetCount = 1, .targets = &target};

        // 1.3 Create pipeline
        wgpu::RenderPipelineDescriptor render_pd{
            .label    = "3d pipeline",
            .layout   = nullptr, // auto
            .vertex   = vertex_state,
            .fragment = &fragment_state
        };
        auto pipeline = device.CreateRenderPipeline(&render_pd);

        // 2. Create buffers
        wgpu::BufferDescriptor draw_data_bd{
            .label = "uniform buffer",
            .usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst,
            .size  = sizeof(DrawData),
        };
        const auto draw_data_buffer = device.CreateBuffer(&draw_data_bd);
        static int w, h;
        SDL_GetWindowSizeInPixels(window, &w, &h);
        data.resolution[0] = static_cast<float>(w);
        data.resolution[1] = static_cast<float>(h);
        device.GetQueue().WriteBuffer(draw_data_buffer, 0, static_cast<const void*>(&data), sizeof(DrawData));

        wgpu::BindGroupEntry      bge{.binding = 0, .buffer = draw_data_buffer};
        wgpu::BindGroupDescriptor bgd{
            .label      = "bind group pass3",
            .layout     = pipeline.GetBindGroupLayout(0), // Parse the shader to get layout
            .entryCount = 1,
            .entries    = &bge
        };
        const auto bind_group = device.CreateBindGroup(&bgd);

        // 2.2 Vertex + index buffer
        wgpu::BufferDescriptor vertex_bd{
            .label = "vertex buffer",
            .usage = wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopyDst,
            .size  = sizeof(vertex_data)
        };
        auto vertex_buffer = device.CreateBuffer(&vertex_bd);
        device.GetQueue().WriteBuffer(vertex_buffer, 0, vertex_data.data(), sizeof(vertex_data));

        wgpu::BufferDescriptor index_bd{
            .label = "index buffer",
            .usage = wgpu::BufferUsage::Index | wgpu::BufferUsage::CopyDst,
            .size  = sizeof(index_data)
        };
        auto index_buffer = device.CreateBuffer(&index_bd);
        device.GetQueue().WriteBuffer(index_buffer, 0, index_data.data(), sizeof(index_data));

        // 4. Create render pass

        pass.SetPipeline(pipeline);
        pass.SetBindGroup(0, bind_group);
        pass.SetVertexBuffer(0, vertex_buffer);
        pass.SetIndexBuffer(index_buffer, wgpu::IndexFormat::Uint32);
        pass.DrawIndexed(index_data.size());
    }

    // ImGui render pass
    {
        // Poll inputs
        SDL_Event e;
        while (SDL_PollEvent(&e)) ImGui_ImplSDL3_ProcessEvent(&e);
        ImGui_ImplWGPU_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        ImGui::Begin("Controls");
        ImGui::SliderFloat("translation.x", &data.trans_px[0], 0.0f, 1000.0f);
        ImGui::SliderFloat("translation.y", &data.trans_px[1], 0.0f, 1000.0f);

        static float ang_deg = 0;
        ImGui::SliderFloat("rotate.degree", &ang_deg, -360.0f, 360.0f);
        data.rot_cos_sin[0] = std::cos(ang_deg * 2 * std::numbers::pi / 360);
        data.rot_cos_sin[1] = std::sin(ang_deg * 2 * std::numbers::pi / 360);

        ImGui::SliderFloat("scale.x", &data.scale[0], -5.0f, 5.0f);
        ImGui::SliderFloat("scale.y", &data.scale[1], -5.0f, 5.0f);

        ImGui::End();
        ImGui::Render();
        ImGui_ImplWGPU_RenderDrawData(ImGui::GetDrawData(), pass.Get());
    }

    pass.End();
    wgpu::CommandBuffer cmd = encoder.Finish();
    // This should happend few, best practice is to do more work in single command buffer.
    device.GetQueue().Submit(1, &cmd);
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
    s = instance.WaitAny(
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
