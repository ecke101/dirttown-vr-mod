#include "xr_d3d12.hpp"
#include <d3dcompiler.h>
#include <chrono>
#include <thread>
#include <io.h>

using namespace dtvr;
static const char shader[]=R"(
cbuffer Camera : register(b0) { column_major float4x4 viewProjection; };
struct Vertex { float4 position:SV_Position; float3 colour:COLOR; };
Vertex vs(uint id:SV_VertexID) {
    const float3 points[8]={float3(-1,-1,-1),float3(1,-1,-1),float3(1,1,-1),float3(-1,1,-1),
                           float3(-1,-1,1),float3(1,-1,1),float3(1,1,1),float3(-1,1,1)};
    const uint indices[36]={0,2,1,0,3,2,4,5,6,4,6,7,0,1,5,0,5,4,3,7,6,3,6,2,0,4,7,0,7,3,1,2,6,1,6,5};
    const float3 colours[6]={float3(.40,.18,.06),float3(.78,.42,.12),float3(.26,.13,.05),
                            float3(.90,.55,.20),float3(.56,.27,.08),float3(.68,.34,.10)};
    Vertex v;v.position=mul(viewProjection,float4(points[indices[id]]*.3,1));v.colour=colours[id/6];return v;
}
float4 ps(Vertex v):SV_Target {return float4(v.colour,1);}
)";

struct ProbeRenderer {
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> commands;
    ComPtr<ID3D12Fence> fence;
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12PipelineState> pipeline;
    ComPtr<ID3D12DescriptorHeap> rtvHeap,dsvHeap;
    std::array<ComPtr<ID3D12Resource>,2> depths;
    HANDLE event=nullptr;
    uint64_t fenceValue=0;
    uint32_t rtvStride=0,dsvStride=0;
    std::array<uint32_t,2> eyeDescriptorStart{};
    Matrix model=identity(); bool anchored=false;

    void createDevice(const XrGraphicsRequirementsD3D12KHR& requirements) {
        ComPtr<IDXGIFactory4> factory;
        check_hr(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"Create DXGI factory");
        ComPtr<IDXGIAdapter1> adapter;
        check_hr(factory->EnumAdapterByLuid(requirements.adapterLuid,IID_PPV_ARGS(&adapter)),"Find headset GPU");
        DXGI_ADAPTER_DESC1 desc{};adapter->GetDesc1(&desc);std::printf("D3D12 adapter: %ls\n",desc.Description);
        check_hr(D3D12CreateDevice(adapter.Get(),requirements.minFeatureLevel,IID_PPV_ARGS(&device)),"Create D3D12 device");
        D3D12_COMMAND_QUEUE_DESC q{};q.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
        check_hr(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)),"Create D3D12 queue");
    }
    void setup(XrBridge& xr) {
        check_hr(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)),"Create command allocator");
        check_hr(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&commands)),"Create command list");
        check_hr(commands->Close(),"Close initial list");
        check_hr(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)),"Create fence");
        event=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!event) throw std::runtime_error("Create fence event failed");
        D3D12_ROOT_PARAMETER parameter{};parameter.ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameter.Constants.Num32BitValues=16;parameter.ShaderVisibility=D3D12_SHADER_VISIBILITY_VERTEX;
        D3D12_ROOT_SIGNATURE_DESC rs{};rs.NumParameters=1;rs.pParameters=&parameter;
        ComPtr<ID3DBlob> blob,error;
        check_hr(D3D12SerializeRootSignature(&rs,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error),"Serialize root signature");
        check_hr(device->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root)),"Create root signature");
        ComPtr<ID3DBlob> vertex,pixel;
        HRESULT result=D3DCompile(shader,sizeof(shader),nullptr,nullptr,nullptr,"vs","vs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&vertex,&error);
        if(FAILED(result)&&error)std::printf("%s\n",(char*)error->GetBufferPointer());check_hr(result,"Compile vertex shader");
        result=D3DCompile(shader,sizeof(shader),nullptr,nullptr,nullptr,"ps","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&pixel,&error);
        if(FAILED(result)&&error)std::printf("%s\n",(char*)error->GetBufferPointer());check_hr(result,"Compile pixel shader");
        D3D12_GRAPHICS_PIPELINE_STATE_DESC ps{};ps.pRootSignature=root.Get();
        ps.VS={vertex->GetBufferPointer(),vertex->GetBufferSize()};ps.PS={pixel->GetBufferPointer(),pixel->GetBufferSize()};
        ps.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;ps.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;
        ps.RasterizerState.DepthClipEnable=TRUE;
        ps.BlendState.RenderTarget[0].RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;
        ps.DepthStencilState.DepthEnable=TRUE;ps.DepthStencilState.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ALL;
        ps.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_LESS;
        ps.SampleMask=UINT_MAX;ps.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        ps.NumRenderTargets=1;ps.RTVFormats[0]=xr.format;ps.DSVFormat=DXGI_FORMAT_D32_FLOAT;ps.SampleDesc.Count=1;
        check_hr(device->CreateGraphicsPipelineState(&ps,IID_PPV_ARGS(&pipeline)),"Create cube pipeline");
        uint32_t imageCount=0;for(const auto& eye:xr.eyes)imageCount+=(uint32_t)eye.images.size();
        D3D12_DESCRIPTOR_HEAP_DESC heap{};heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;heap.NumDescriptors=imageCount;
        check_hr(device->CreateDescriptorHeap(&heap,IID_PPV_ARGS(&rtvHeap)),"Create RTV heap");
        heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_DSV;heap.NumDescriptors=2;
        check_hr(device->CreateDescriptorHeap(&heap,IID_PPV_ARGS(&dsvHeap)),"Create DSV heap");
        rtvStride=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        dsvStride=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
        uint32_t index=0;
        for(unsigned i=0;i<2;++i) {
            eyeDescriptorStart[i]=index;
            for(const auto& image:xr.eyes[i].images) {
                auto handle=rtvHeap->GetCPUDescriptorHandleForHeapStart();handle.ptr+=rtvStride*index++;
                device->CreateRenderTargetView(image.texture,nullptr,handle);
            }
            D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;
            D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=xr.eyes[i].width;
            d.Height=xr.eyes[i].height;d.DepthOrArraySize=1;d.MipLevels=1;d.Format=DXGI_FORMAT_D32_FLOAT;
            d.SampleDesc.Count=1;d.Flags=D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
            D3D12_CLEAR_VALUE clear{};clear.Format=DXGI_FORMAT_D32_FLOAT;clear.DepthStencil.Depth=1;
            check_hr(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_DEPTH_WRITE,&clear,
                IID_PPV_ARGS(&depths[i])),"Create eye depth buffer");
            auto handle=dsvHeap->GetCPUDescriptorHandleForHeapStart();handle.ptr+=dsvStride*i;
            device->CreateDepthStencilView(depths[i].Get(),nullptr,handle);
        }
    }
    void synchronize() {
        check_hr(queue->Signal(fence.Get(),++fenceValue),"Signal eye rendering");
        if(fence->GetCompletedValue()<fenceValue) {
            check_hr(fence->SetEventOnCompletion(fenceValue,event),"Wait for eye rendering");
            if(WaitForSingleObject(event,10000)!=WAIT_OBJECT_0)throw std::runtime_error("Eye rendering GPU timeout");
        }
    }
    void render(XrBridge& xr) {
        if(!anchored) {
            // Anchor a cube two metres in front of the initial head direction.
            const auto& h=xr.head.pose;auto view=viewMatrix(h);
            model[12]=h.position.x-2*view[2];model[13]=h.position.y-2*view[6];model[14]=h.position.z-2*view[10];
            anchored=true;
        }
        check_hr(allocator->Reset(),"Reset allocator");check_hr(commands->Reset(allocator.Get(),pipeline.Get()),"Reset commands");
        commands->SetGraphicsRootSignature(root.Get());commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        for(unsigned i=0;i<2;++i) {
            auto& eye=xr.eyes[i];auto* texture=eye.images[eye.acquired].texture;
            auto barrier=transition(texture,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_RENDER_TARGET);
            commands->ResourceBarrier(1,&barrier);
            auto rtv=rtvHeap->GetCPUDescriptorHandleForHeapStart();rtv.ptr+=rtvStride*(eyeDescriptorStart[i]+eye.acquired);
            auto dsv=dsvHeap->GetCPUDescriptorHandleForHeapStart();dsv.ptr+=dsvStride*i;
            const float clear[4]={.08f,.10f,.12f,1.f};
            commands->ClearRenderTargetView(rtv,clear,0,nullptr);commands->ClearDepthStencilView(dsv,D3D12_CLEAR_FLAG_DEPTH,1,0,0,nullptr);
            commands->OMSetRenderTargets(1,&rtv,FALSE,&dsv);
            D3D12_VIEWPORT viewport{0,0,(float)eye.width,(float)eye.height,0,1};
            D3D12_RECT scissor{0,0,(LONG)eye.width,(LONG)eye.height};
            commands->RSSetViewports(1,&viewport);commands->RSSetScissorRects(1,&scissor);
            auto m=multiply(projection(xr.views[i].fov),multiply(viewMatrix(xr.views[i].pose),model));
            commands->SetGraphicsRoot32BitConstants(0,16,m.data(),0);commands->DrawInstanced(36,1,0,0);
            barrier=transition(texture,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COMMON);
            commands->ResourceBarrier(1,&barrier);
        }
        check_hr(commands->Close(),"Close eye commands");ID3D12CommandList* lists[]={commands.Get()};
        queue->ExecuteCommandLists(1,lists);synchronize();
    }
    ~ProbeRenderer(){if(event)CloseHandle(event);}
};

int main(int argc,char** argv) {
    // Wine's inherited console handles can be absent when launched from the
    // desktop agent. Keep an application-owned log independent of those handles.
    if (!std::freopen("probe-result.log","w",stdout)) return 3;
    _dup2(_fileno(stdout),_fileno(stderr));
    setvbuf(stdout,nullptr,_IONBF,0);setvbuf(stderr,nullptr,_IONBF,0);
    const double duration=argc>1?std::clamp(std::atof(argv[1]),1.,120.):15.;
    try {
        wchar_t executable[MAX_PATH];GetModuleFileNameW(nullptr,executable,MAX_PATH);
        std::wstring loader(executable);loader=loader.substr(0,loader.find_last_of(L"\\/"))+L"\\openxr_loader.dll";
        ProbeRenderer renderer;
        XrBridge xr;auto requirements=xr.prepare(loader.c_str());
        renderer.createDevice(requirements);xr.bind(renderer.device.Get(),renderer.queue.Get());renderer.setup(xr);
        const auto started=std::chrono::steady_clock::now();
        auto firstVisible=started;bool hasVisible=false,exitRequested=false;
        unsigned rendered=0,tracked=0;float maximumHeadTranslation=0;
        XrVector3f initialPosition{};
        while(!xr.quit) {
            bool draw=xr.beginFrame();
            if(draw) {
                xr.acquire();renderer.render(xr);++rendered;
                if(xr.head.locationFlags & XR_SPACE_LOCATION_POSITION_TRACKED_BIT)++tracked;
                if(rendered==1)initialPosition=xr.head.pose.position;
                auto p=xr.head.pose.position;
                float distance=std::sqrt(std::pow(p.x-initialPosition.x,2)+std::pow(p.y-initialPosition.y,2)+std::pow(p.z-initialPosition.z,2));
                maximumHeadTranslation=std::max(maximumHeadTranslation,distance);
                if(rendered%120==1)std::printf("Frame %u: head (%.3f %.3f %.3f), tracked flags 0x%llx, period %.2f ms\n",
                    rendered,p.x,p.y,p.z,(unsigned long long)xr.head.locationFlags,xr.frame.predictedDisplayPeriod/1000000.);
            }
            xr.endFrame(draw);
            auto now=std::chrono::steady_clock::now();
            if(xr.visible&&!hasVisible){firstVisible=now;hasVisible=true;}
            if(!exitRequested&&xr.running&&hasVisible&&std::chrono::duration<double>(now-firstVisible).count()>duration) {
                check_xr(xr.xrRequestExitSession(xr.session),"Request test exit");exitRequested=true;
            }
            if(std::chrono::duration<double>(now-started).count()>90)break;
            if(!xr.running)std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        std::printf("RESULT: rendered_frames=%u position_tracked_frames=%u max_head_translation_m=%.3f\n",
            rendered,tracked,maximumHeadTranslation);
        return rendered>0?0:2;
    } catch(const std::exception& e) {std::fprintf(stderr,"ERROR: %s\n",e.what());return 1;}
}
