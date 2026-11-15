#pragma once
#include "xr_d3d12.hpp"
#include <d3dcompiler.h>
#include "temporal.hpp"
#include "temporal_shader.hpp"

namespace dtvr {
// Preserve all SDL graphics state: the game command list only copies the two
// completed views. A separate list converts them to the OpenXR swapchain format.
class EyeTransfer {
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> commands;
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12PipelineState> pipeline;
    ComPtr<ID3D12DescriptorHeap> srvHeap,rtvHeap;
    std::array<ComPtr<ID3D12Resource>,2> copies;
    ComPtr<ID3D12Fence> fence;
    HANDLE event=nullptr;
    uint64_t value=0;
    uint32_t rtvStride=0,srvStride=0;
    std::array<uint32_t,2> descriptorStart{};
    TemporalFilter temporal;
public:
    void setup(ID3D12CommandQueue* q,XrBridge& xr) {
        queue=q;check_hr(q->GetDevice(IID_PPV_ARGS(&device)),"Read game's D3D12 device");
        check_hr(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)),"Create VR allocator");
        check_hr(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&commands)),"Create VR commands");
        check_hr(commands->Close(),"Close VR command list");
        check_hr(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)),"Create VR fence");
        event=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!event)throw std::runtime_error("Create VR fence event failed");
        D3D12_DESCRIPTOR_RANGE range{};range.RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SRV;range.NumDescriptors=1;
        D3D12_ROOT_PARAMETER parameter{};parameter.ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameter.DescriptorTable.NumDescriptorRanges=1;parameter.DescriptorTable.pDescriptorRanges=&range;
        parameter.ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_STATIC_SAMPLER_DESC sampler{};sampler.Filter=D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.MaxLOD=D3D12_FLOAT32_MAX;sampler.ComparisonFunc=D3D12_COMPARISON_FUNC_ALWAYS;
        sampler.ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_ROOT_SIGNATURE_DESC signature{};signature.NumParameters=1;signature.pParameters=&parameter;
        signature.NumStaticSamplers=1;signature.pStaticSamplers=&sampler;
        ComPtr<ID3DBlob> blob,error;
        check_hr(D3D12SerializeRootSignature(&signature,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error),"Serialize eye blit signature");
        check_hr(device->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root)),"Create eye blit signature");
        const char shader[]=R"(
Texture2D image:register(t0);SamplerState imageSampler:register(s0);
struct Vertex {float4 p:SV_Position;float2 uv:TEXCOORD;};
Vertex vs(uint id:SV_VertexID) {Vertex v;float2 p=float2((id<<1)&2,id&2);
v.p=float4(p*float2(2,-2)+float2(-1,1),0,1);v.uv=p;return v;}
float4 ps(Vertex v):SV_Target {return float4(image.SampleLevel(imageSampler,v.uv,0).rgb,1);}
)";
        ComPtr<ID3DBlob> vs,ps;
        HRESULT result=D3DCompile(shader,sizeof(shader),nullptr,nullptr,nullptr,"vs","vs_5_0",0,0,&vs,&error);
        if(FAILED(result)&&error)std::printf("Shader: %s\n",(char*)error->GetBufferPointer());
        check_hr(result,"Compile eye blit vertex shader");
        check_hr(D3DCompile(shader,sizeof(shader),nullptr,nullptr,nullptr,"ps","ps_5_0",0,0,&ps,&error),"Compile eye blit fragment shader");
        D3D12_GRAPHICS_PIPELINE_STATE_DESC state{};state.pRootSignature=root.Get();
        state.VS={vs->GetBufferPointer(),vs->GetBufferSize()};state.PS={ps->GetBufferPointer(),ps->GetBufferSize()};
        state.BlendState.RenderTarget[0].RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;
        state.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;state.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;
        state.RasterizerState.DepthClipEnable=TRUE;state.SampleMask=UINT_MAX;
        state.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        state.NumRenderTargets=1;state.RTVFormats[0]=xr.format;state.SampleDesc.Count=1;
        check_hr(device->CreateGraphicsPipelineState(&state,IID_PPV_ARGS(&pipeline)),"Create eye blit pipeline");
        D3D12_DESCRIPTOR_HEAP_DESC heap{};heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heap.NumDescriptors=2;heap.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        check_hr(device->CreateDescriptorHeap(&heap,IID_PPV_ARGS(&srvHeap)),"Create eye source descriptors");
        heap={};heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        heap.NumDescriptors=(UINT)(xr.eyes[0].images.size()+xr.eyes[1].images.size());
        check_hr(device->CreateDescriptorHeap(&heap,IID_PPV_ARGS(&rtvHeap)),"Create eye target descriptors");
        rtvStride=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        srvStride=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        unsigned index=0;
        for(unsigned i=0;i<2;++i) {descriptorStart[i]=index;
            for(auto& image:xr.eyes[i].images) {
                auto h=rtvHeap->GetCPUDescriptorHandleForHeapStart();h.ptr+=rtvStride*index++;
                device->CreateRenderTargetView(image.texture,nullptr,h);
            }
        }
        temporal.setup(device.Get(),xr.renderWidth,xr.renderHeight,temporalShader,sizeof(temporalShader)-1);
    }
    void setAntialiasing(bool enabled) {temporal.setEnabled(enabled);}
    void resetHistory() {temporal.reset();}
    void setView(unsigned eye,const TemporalView& view) {temporal.setView(eye,view);}
    void captureDepth(unsigned eye,ID3D12GraphicsCommandList* commands,ID3D12Resource* source,D3D12_RESOURCE_STATES state) {
        temporal.captureDepth(eye,commands,source,state);
    }
    void capture(unsigned eye,ID3D12GraphicsCommandList* gameCommands,ID3D12Resource* source,D3D12_RESOURCE_STATES state) {
        const auto desc=source->GetDesc();
        if(!copies[eye]) {
            D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
            auto copyDesc=desc;copyDesc.Flags=D3D12_RESOURCE_FLAG_NONE;
            if(desc.Format==DXGI_FORMAT_R8G8B8A8_UNORM || desc.Format==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)
                copyDesc.Format=DXGI_FORMAT_R8G8B8A8_TYPELESS;
            if(desc.Format==DXGI_FORMAT_B8G8R8A8_UNORM || desc.Format==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)
                copyDesc.Format=DXGI_FORMAT_B8G8R8A8_TYPELESS;
            check_hr(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&copyDesc,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                nullptr,IID_PPV_ARGS(&copies[eye])),"Create eye capture texture");
            D3D12_SHADER_RESOURCE_VIEW_DESC view{};view.Format=desc.Format;
            // The game's final colour is gamma encoded in an UNORM target.
            if(view.Format==DXGI_FORMAT_R8G8B8A8_UNORM)view.Format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
            if(view.Format==DXGI_FORMAT_B8G8R8A8_UNORM)view.Format=DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
            view.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;view.Texture2D.MipLevels=1;
            view.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            auto handle=srvHeap->GetCPUDescriptorHandleForHeapStart();handle.ptr+=eye*srvStride;
            device->CreateShaderResourceView(copies[eye].Get(),&view,handle);
            std::printf("Game eye capture: %llu x %u, DXGI format %u, source state 0x%x\n",desc.Width,desc.Height,desc.Format,state);
        }
        auto barriers=std::array<D3D12_RESOURCE_BARRIER,2>{
            transition(source,state,D3D12_RESOURCE_STATE_COPY_SOURCE),
            transition(copies[eye].Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_DEST)};
        gameCommands->ResourceBarrier(2,barriers.data());gameCommands->CopyResource(copies[eye].Get(),source);
        for(auto& b:barriers)std::swap(b.Transition.StateBefore,b.Transition.StateAfter);
        gameCommands->ResourceBarrier(2,barriers.data());
    }
    void submit(XrBridge& xr) {
        check_hr(allocator->Reset(),"Reset eye allocator");check_hr(commands->Reset(allocator.Get(),pipeline.Get()),"Reset eye commands");
        for(unsigned i=0;i<2;++i) {
            auto resolved=temporal.resolve(i,commands.Get(),copies[i].Get());
            auto destination=srvHeap->GetCPUDescriptorHandleForHeapStart();destination.ptr+=i*srvStride;
            device->CopyDescriptorsSimple(1,destination,resolved,D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            ID3D12DescriptorHeap* heaps[]={srvHeap.Get()};commands->SetDescriptorHeaps(1,heaps);
            commands->SetPipelineState(pipeline.Get());commands->SetGraphicsRootSignature(root.Get());
            commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            auto& eye=xr.eyes[i];auto* image=eye.images[eye.acquired].texture;
            auto barrier=transition(image,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_RENDER_TARGET);
            commands->ResourceBarrier(1,&barrier);
            auto target=rtvHeap->GetCPUDescriptorHandleForHeapStart();target.ptr+=(descriptorStart[i]+eye.acquired)*rtvStride;
            auto source=srvHeap->GetGPUDescriptorHandleForHeapStart();source.ptr+=i*srvStride;
            commands->OMSetRenderTargets(1,&target,FALSE,nullptr);commands->SetGraphicsRootDescriptorTable(0,source);
            D3D12_VIEWPORT viewport{0,0,(float)eye.width,(float)eye.height,0,1};
            D3D12_RECT scissor{0,0,(LONG)eye.width,(LONG)eye.height};
            commands->RSSetViewports(1,&viewport);commands->RSSetScissorRects(1,&scissor);commands->DrawInstanced(3,1,0,0);
            barrier=transition(image,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COMMON);
            commands->ResourceBarrier(1,&barrier);
        }
        check_hr(commands->Close(),"Close eye blit commands");ID3D12CommandList* lists[]={commands.Get()};
        queue->ExecuteCommandLists(1,lists);
        check_hr(queue->Signal(fence.Get(),++value),"Signal game eye rendering");
        if(fence->GetCompletedValue()<value) {
            check_hr(fence->SetEventOnCompletion(value,event),"Wait for game eye rendering");
            if(WaitForSingleObject(event,10000)!=WAIT_OBJECT_0)throw std::runtime_error("Game VR GPU timeout");
        }
    }
    ~EyeTransfer() {if(event)CloseHandle(event);}
};
} // namespace dtvr
