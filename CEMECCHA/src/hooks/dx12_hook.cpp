#include "dx12_hook.hpp"

#include "../core/logger.hpp"
#include "game_thread.hpp"

extern "C" void ce_gt_set_hwnd(void* hwnd);

#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <atomic>
#include <mutex>
#include <vector>

#include "../../third-party/minhook/include/MinHook.h"
#include "../../third-party/imgui/imgui.h"
#include "../../third-party/imgui/backends/imgui_impl_win32.h"
#include "../../third-party/imgui/backends/imgui_impl_dx12.h"

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace
{
    using PresentFn = HRESULT (STDMETHODCALLTYPE*)(IDXGISwapChain3*, UINT, UINT);
    using ResizeBuffersFn = HRESULT (STDMETHODCALLTYPE*)(IDXGISwapChain3*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
    using ExecuteCmdListsFn= void (STDMETHODCALLTYPE*)(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);

    PresentFn g_orig_present = nullptr;
    ResizeBuffersFn g_orig_resize = nullptr;
    ExecuteCmdListsFn g_orig_execute_cmd = nullptr;
    WNDPROC g_orig_wndproc = nullptr;

    HWND g_hwnd = nullptr;
    ID3D12Device* g_device = nullptr;
    ID3D12CommandQueue* g_cmd_queue = nullptr;
    ID3D12DescriptorHeap* g_srv_heap = nullptr;
    ID3D12DescriptorHeap* g_rtv_heap = nullptr;
    ID3D12GraphicsCommandList* g_cmd_list = nullptr;

    struct FrameContext
    {
        ID3D12CommandAllocator* allocator = nullptr;
        ID3D12Resource* rtv_resource = nullptr;
        D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle{};
    };
    std::vector<FrameContext> g_frames;
    UINT g_buffer_count = 0;

    std::atomic<bool> g_imgui_ready{ false };
    std::atomic<bool> g_installed{ false };
    std::mutex g_present_mtx;

    ce::hooks::RenderFn g_render_cb;
    ce::hooks::InputFn g_input_cb;

    struct ReadbackState
    {
        ID3D12Resource* readback_buf = nullptr;
        ID3D12CommandAllocator* alloc = nullptr;
        ID3D12GraphicsCommandList* cmd_list = nullptr;
        ID3D12Fence* fence = nullptr;
        HANDLE fence_event = nullptr;
        UINT64 fence_value = 0;
        UINT rowpitch = 0;
        UINT bufsize = 0;
        UINT width = 0;
        UINT height = 0;
        DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    };
    ReadbackState g_readback{};
    std::mutex g_readback_mtx;
    IDXGISwapChain3* g_last_sc = nullptr;

    LRESULT CALLBACK hooked_wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
    {

        if (msg == ce::hooks::kGameThreadDispatchMsg)
        {
            ce::hooks::game_thread_pump();
            return 0;
        }

        if (g_imgui_ready)
        {
            if (g_input_cb && g_input_cb(hwnd, msg, wparam, lparam))
            {
                ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam);
                return true;
            }
            ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam);
        }
        return CallWindowProcW(g_orig_wndproc, hwnd, msg, wparam, lparam);
    }

    bool init_imgui(IDXGISwapChain3* sc)
    {
        DXGI_SWAP_CHAIN_DESC desc{};
        sc->GetDesc(&desc);

        if (FAILED(sc->GetDevice(__uuidof(ID3D12Device), reinterpret_cast<void**>(&g_device))))
            return false;

        g_hwnd = desc.OutputWindow;
        g_buffer_count = desc.BufferCount;
        g_frames.resize(g_buffer_count);

        D3D12_DESCRIPTOR_HEAP_DESC srv_desc{};
        srv_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srv_desc.NumDescriptors = 1;
        srv_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FAILED(g_device->CreateDescriptorHeap(&srv_desc, IID_PPV_ARGS(&g_srv_heap))))
            return false;

        D3D12_DESCRIPTOR_HEAP_DESC rtv_desc{};
        rtv_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtv_desc.NumDescriptors = g_buffer_count;
        rtv_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if (FAILED(g_device->CreateDescriptorHeap(&rtv_desc, IID_PPV_ARGS(&g_rtv_heap))))
            return false;

        SIZE_T rtv_step = g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        auto rtv_h = g_rtv_heap->GetCPUDescriptorHandleForHeapStart();

        for (UINT i = 0; i < g_buffer_count; ++i)
        {
            g_frames[i].rtv_handle = rtv_h;
            if (FAILED(sc->GetBuffer(i, IID_PPV_ARGS(&g_frames[i].rtv_resource))))
                return false;
            g_device->CreateRenderTargetView(g_frames[i].rtv_resource, nullptr, rtv_h);
            rtv_h.ptr += rtv_step;

            if (FAILED(g_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g_frames[i].allocator))))
                return false;
        }

        if (FAILED(g_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_frames[0].allocator, nullptr, IID_PPV_ARGS(&g_cmd_list))))
            return false;
        g_cmd_list->Close();

        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

        io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;

        if (!ImGui_ImplWin32_Init(g_hwnd)) return false;

        ImGui_ImplDX12_InitInfo init_info{};
        init_info.Device = g_device;
        init_info.CommandQueue = g_cmd_queue;
        init_info.NumFramesInFlight = g_buffer_count;
        init_info.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
        init_info.SrvDescriptorHeap = g_srv_heap;
        init_info.LegacySingleSrvCpuDescriptor = g_srv_heap->GetCPUDescriptorHandleForHeapStart();
        init_info.LegacySingleSrvGpuDescriptor = g_srv_heap->GetGPUDescriptorHandleForHeapStart();
        if (!ImGui_ImplDX12_Init(&init_info)) return false;

        g_orig_wndproc = reinterpret_cast<WNDPROC>( SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(hooked_wndproc)));

        ce_gt_set_hwnd(g_hwnd);

        ce::log::info("ImGui/DX12 initialized — hwnd=%p device=%p buffers=%u", g_hwnd, g_device, g_buffer_count);

        g_imgui_ready = true;
        return true;
    }

    void render_frame(IDXGISwapChain3* sc)
    {
        if (!g_cmd_queue) return;

        ImGui_ImplDX12_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        if (g_render_cb) g_render_cb();

        ImGui::Render();

        const UINT idx = sc->GetCurrentBackBufferIndex();
        auto& frame = g_frames[idx];
        frame.allocator->Reset();

        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.Transition.pResource = frame.rtv_resource;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

        g_cmd_list->Reset(frame.allocator, nullptr);
        g_cmd_list->ResourceBarrier(1, &barrier);
        g_cmd_list->OMSetRenderTargets(1, &frame.rtv_handle, FALSE, nullptr);
        g_cmd_list->SetDescriptorHeaps(1, &g_srv_heap);

        ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), g_cmd_list);

        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        g_cmd_list->ResourceBarrier(1, &barrier);
        g_cmd_list->Close();

        ID3D12CommandList* lists[]{ g_cmd_list };
        g_cmd_queue->ExecuteCommandLists(1, lists);
    }

    HRESULT STDMETHODCALLTYPE hooked_present(IDXGISwapChain3* sc, UINT sync, UINT flags)
    {
        std::lock_guard<std::mutex> lk(g_present_mtx);

        g_last_sc = sc;

        if (!g_imgui_ready)
        {
            if (g_cmd_queue && init_imgui(sc))
            {

            }
            else
            {
                return g_orig_present(sc, sync, flags);
            }
        }

        if (g_cmd_queue)
            render_frame(sc);

        return g_orig_present(sc, sync, flags);
    }

    HRESULT STDMETHODCALLTYPE hooked_resize(IDXGISwapChain3* sc, UINT count, UINT w, UINT h, DXGI_FORMAT fmt, UINT flags)
    {
        std::lock_guard<std::mutex> lk(g_present_mtx);

        if (g_imgui_ready)
        {
            ImGui_ImplDX12_InvalidateDeviceObjects();
            for (auto& f : g_frames)
            {
                if (f.rtv_resource) { f.rtv_resource->Release(); f.rtv_resource = nullptr; }
            }
        }

        HRESULT hr = g_orig_resize(sc, count, w, h, fmt, flags);

        if (g_imgui_ready)
        {
            SIZE_T step = g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
            auto rtv_h = g_rtv_heap->GetCPUDescriptorHandleForHeapStart();
            for (UINT i = 0; i < g_buffer_count; ++i)
            {
                g_frames[i].rtv_handle = rtv_h;
                sc->GetBuffer(i, IID_PPV_ARGS(&g_frames[i].rtv_resource));
                g_device->CreateRenderTargetView(g_frames[i].rtv_resource, nullptr, rtv_h);
                rtv_h.ptr += step;
            }
            ImGui_ImplDX12_CreateDeviceObjects();
        }

        return hr;
    }

    void STDMETHODCALLTYPE hooked_execute(ID3D12CommandQueue* q, UINT n, ID3D12CommandList* const* lists)
    {
        if (!g_cmd_queue)
        {
            D3D12_COMMAND_QUEUE_DESC d = q->GetDesc();
            if (d.Type == D3D12_COMMAND_LIST_TYPE_DIRECT)
                g_cmd_queue = q;
        }
        g_orig_execute_cmd(q, n, lists);
    }

    bool resolve_present_vtable(void*& out_present, void*& out_resize, void*& out_execute)
    {

        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"CEMECCHA_DUMMY_CLS";
        RegisterClassExW(&wc);

        HWND hw = CreateWindowExW(0, wc.lpszClassName, L"cemeccha_dummy", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, nullptr, nullptr, wc.hInstance, nullptr);
        if (!hw) { UnregisterClassW(wc.lpszClassName, wc.hInstance); return false; }

        ID3D12Device* dev = nullptr;
        if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&dev))))
        {
            DestroyWindow(hw);
            UnregisterClassW(wc.lpszClassName, wc.hInstance);
            return false;
        }

        D3D12_COMMAND_QUEUE_DESC qd{};
        qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        ID3D12CommandQueue* q = nullptr;
        if (FAILED(dev->CreateCommandQueue(&qd, IID_PPV_ARGS(&q))))
        {
            dev->Release();
            DestroyWindow(hw);
            UnregisterClassW(wc.lpszClassName, wc.hInstance);
            return false;
        }

        IDXGIFactory4* fac = nullptr;
        if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&fac))))
        {
            q->Release(); dev->Release();
            DestroyWindow(hw); UnregisterClassW(wc.lpszClassName, wc.hInstance);
            return false;
        }

        DXGI_SWAP_CHAIN_DESC scd{};
        scd.BufferCount = 2;
        scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        scd.OutputWindow = hw;
        scd.SampleDesc.Count = 1;
        scd.Windowed = TRUE;
        scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

        IDXGISwapChain* sc = nullptr;
        if (FAILED(fac->CreateSwapChain(q, &scd, &sc)))
        {
            fac->Release(); q->Release(); dev->Release();
            DestroyWindow(hw); UnregisterClassW(wc.lpszClassName, wc.hInstance);
            return false;
        }

        auto sc_vtbl = *reinterpret_cast<void***>(sc);
        auto q_vtbl = *reinterpret_cast<void***>(q);

        out_present = sc_vtbl[8];
        out_resize = sc_vtbl[13];
        out_execute = q_vtbl [10];

        sc->Release(); fac->Release(); q->Release(); dev->Release();
        DestroyWindow(hw);
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return true;
    }
}

namespace
{
    void release_readback_state()
    {
        if (g_readback.readback_buf) { g_readback.readback_buf->Release(); g_readback.readback_buf = nullptr; }
        if (g_readback.cmd_list) { g_readback.cmd_list->Release(); g_readback.cmd_list = nullptr; }
        if (g_readback.alloc) { g_readback.alloc->Release(); g_readback.alloc = nullptr; }
        if (g_readback.fence) { g_readback.fence->Release(); g_readback.fence = nullptr; }
        if (g_readback.fence_event) { CloseHandle(g_readback.fence_event); g_readback.fence_event = nullptr; }
        g_readback = {};
    }

    bool ensure_readback_resources(UINT w, UINT h, DXGI_FORMAT fmt)
    {
        if (g_readback.width == w && g_readback.height == h && g_readback.format == fmt && g_readback.readback_buf)
            return true;

        release_readback_state();

        if (!g_device) return false;

        const UINT bytes_per_pixel = 4;
        const UINT row_bytes = w * bytes_per_pixel;
        const UINT rowpitch = (row_bytes + 255) & ~255u;
        const UINT bufsize = rowpitch * h;

        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_READBACK;

        D3D12_RESOURCE_DESC rd{};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        rd.Width = bufsize;
        rd.Height = 1;
        rd.DepthOrArraySize = 1;
        rd.MipLevels = 1;
        rd.SampleDesc.Count = 1;
        rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        rd.Format = DXGI_FORMAT_UNKNOWN;

        if (FAILED(g_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&g_readback.readback_buf)))) return false;

        if (FAILED(g_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g_readback.alloc)))) return false;

        if (FAILED(g_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_readback.alloc, nullptr, IID_PPV_ARGS(&g_readback.cmd_list)))) return false;
        g_readback.cmd_list->Close();

        if (FAILED(g_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_readback.fence)))) return false;
        g_readback.fence_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!g_readback.fence_event) return false;

        g_readback.rowpitch = rowpitch;
        g_readback.bufsize = bufsize;
        g_readback.width = w;
        g_readback.height = h;
        g_readback.format = fmt;
        return true;
    }
}

namespace ce::hooks
{
    bool readback_last_frame(std::vector<uint32_t>* out_pixels, int* out_w, int* out_h, uint32_t* out_fmt)
    {
        if (!out_pixels || !out_w || !out_h || !out_fmt) return false;

        std::lock_guard<std::mutex> lk_readback(g_readback_mtx);

        if (!g_last_sc || !g_cmd_queue || !g_device) return false;

        const UINT idx = g_last_sc->GetCurrentBackBufferIndex();
        ID3D12Resource* back = nullptr;
        if (FAILED(g_last_sc->GetBuffer(idx, IID_PPV_ARGS(&back)))) return false;

        D3D12_RESOURCE_DESC desc = back->GetDesc();
        const UINT w = static_cast<UINT>(desc.Width);
        const UINT h = desc.Height;
        const DXGI_FORMAT fmt = desc.Format;

        if (!ensure_readback_resources(w, h, fmt))
        {
            back->Release();
            return false;
        }

        g_readback.alloc->Reset();
        g_readback.cmd_list->Reset(g_readback.alloc, nullptr);

        D3D12_RESOURCE_BARRIER b{};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = back;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        g_readback.cmd_list->ResourceBarrier(1, &b);

        D3D12_TEXTURE_COPY_LOCATION src{};
        src.pResource = back;
        src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        src.SubresourceIndex = 0;

        D3D12_TEXTURE_COPY_LOCATION dst{};
        dst.pResource = g_readback.readback_buf;
        dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint.Offset = 0;
        dst.PlacedFootprint.Footprint.Format = fmt;
        dst.PlacedFootprint.Footprint.Width = w;
        dst.PlacedFootprint.Footprint.Height = h;
        dst.PlacedFootprint.Footprint.Depth = 1;
        dst.PlacedFootprint.Footprint.RowPitch = g_readback.rowpitch;

        D3D12_BOX box{};
        box.right = w;
        box.bottom = h;
        box.back = 1;
        g_readback.cmd_list->CopyTextureRegion(&dst, 0, 0, 0, &src, &box);

        std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
        g_readback.cmd_list->ResourceBarrier(1, &b);
        g_readback.cmd_list->Close();

        ID3D12CommandList* lists[]{ g_readback.cmd_list };
        g_cmd_queue->ExecuteCommandLists(1, lists);

        const UINT64 wait = ++g_readback.fence_value;
        if (FAILED(g_cmd_queue->Signal(g_readback.fence, wait))) { back->Release(); return false; }
        if (g_readback.fence->GetCompletedValue() < wait)
        {
            g_readback.fence->SetEventOnCompletion(wait, g_readback.fence_event);
            WaitForSingleObject(g_readback.fence_event, 2000);
        }

        back->Release();

        void* mapped = nullptr;
        D3D12_RANGE readrange{ 0, g_readback.bufsize };
        if (FAILED(g_readback.readback_buf->Map(0, &readrange, &mapped))) return false;

        out_pixels->resize(static_cast<size_t>(w) * h);
        const uint8_t* src_bytes = static_cast<const uint8_t*>(mapped);
        for (UINT y = 0; y < h; ++y)
        {
            std::memcpy(out_pixels->data() + static_cast<size_t>(y) * w, src_bytes + static_cast<size_t>(y) * g_readback.rowpitch, static_cast<size_t>(w) * sizeof(uint32_t));
        }

        D3D12_RANGE norange{ 0, 0 };
        g_readback.readback_buf->Unmap(0, &norange);

        *out_w = static_cast<int>(w);
        *out_h = static_cast<int>(h);
        *out_fmt = static_cast<uint32_t>(fmt);
        return true;
    }

    bool is_installed() { return g_installed.load(); }

    bool install_dx12(RenderFn render, InputFn input)
    {
        if (g_installed) return true;

        g_render_cb = std::move(render);
        g_input_cb = std::move(input);

        if (MH_Initialize() != MH_OK)
        {
            ce::log::err("MinHook init failed");
            return false;
        }

        void* target_present = nullptr;
        void* target_resize = nullptr;
        void* target_execute = nullptr;
        if (!resolve_present_vtable(target_present, target_resize, target_execute))
        {
            ce::log::err("failed to resolve DX12/DXGI vtable");
            return false;
        }

        ce::log::info("DXGI Present=%p ResizeBuffers=%p ExecuteCmdLists=%p", target_present, target_resize, target_execute);

        if (MH_CreateHook(target_present, hooked_present, reinterpret_cast<void**>(&g_orig_present)) != MH_OK) return false;
        if (MH_CreateHook(target_resize, hooked_resize, reinterpret_cast<void**>(&g_orig_resize)) != MH_OK) return false;
        if (MH_CreateHook(target_execute, hooked_execute, reinterpret_cast<void**>(&g_orig_execute_cmd)) != MH_OK) return false;

        if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK)
        {
            ce::log::err("MinHook enable failed");
            return false;
        }

        g_installed = true;
        ce::log::info("DX12 hooks installed");
        return true;
    }

    void uninstall_dx12()
    {
        if (!g_installed) return;

        if (g_hwnd && g_orig_wndproc)
            SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_orig_wndproc));

        MH_DisableHook(MH_ALL_HOOKS);
        MH_Uninitialize();

        if (g_imgui_ready)
        {
            ImGui_ImplDX12_Shutdown();
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
        }

        for (auto& f : g_frames)
        {
            if (f.rtv_resource) f.rtv_resource->Release();
            if (f.allocator) f.allocator->Release();
        }
        g_frames.clear();

        if (g_cmd_list) g_cmd_list->Release();
        if (g_srv_heap) g_srv_heap->Release();
        if (g_rtv_heap) g_rtv_heap->Release();
        if (g_device) g_device->Release();

        release_readback_state();

        g_installed = false;
        g_imgui_ready = false;
    }
}
