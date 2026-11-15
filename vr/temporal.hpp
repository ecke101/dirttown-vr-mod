#pragma once
#include "xr_d3d12.hpp"
#include <d3dcompiler.h>

namespace dtvr {
struct TemporalView {
    Matrix inverseJitteredVP=identity(),unjitteredVP=identity();
    std::array<double,3> origin{};
    float jitterX=0,jitterY=0;
    uint64_t frame=0;
};
class TemporalFilter {
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12PipelineState> pipeline;
    ComPtr<ID3D12DescriptorHeap> srvHeap,rtvHeap;
    struct History {
        ComPtr<ID3D12Resource> colour,depth;
    };
    struct EyeHistory {
        ComPtr<ID3D12Resource> normalDepth;
        std::array<History,2> images;
        TemporalView current,previous;
        bool valid=false;
        unsigned writeIndex=0;
    };
    std::array<EyeHistory,2> eyes;
    uint32_t width=0,height=0,srvStride=0,rtvStride=0;
    bool enabled=true;
    D3D12_CPU_DESCRIPTOR_HANDLE srvCpu(unsigned index) {
        auto h=srvHeap->GetCPUDescriptorHandleForHeapStart();h.ptr+=index*srvStride;return h;
    }
    D3D12_GPU_DESCRIPTOR_HANDLE srvGpu(unsigned index) {
        auto h=srvHeap->GetGPUDescriptorHandleForHeapStart();h.ptr+=index*srvStride;return h;
    }
    D3D12_CPU_DESCRIPTOR_HANDLE rtv(unsigned index) {
        auto h=rtvHeap->GetCPUDescriptorHandleForHeapStart();h.ptr+=index*rtvStride;return h;
    }
    void resourceView(ID3D12Resource* resource,DXGI_FORMAT format,unsigned index) {
        D3D12_SHADER_RESOURCE_VIEW_DESC view{};view.Format=format;
        view.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;view.Texture2D.MipLevels=1;
        view.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        device->CreateShaderResourceView(resource,&view,srvCpu(index));
    }
    ComPtr<ID3D12Resource> makeImage(DXGI_FORMAT format,bool target) {
        D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        d.Width=width;d.Height=height;d.DepthOrArraySize=1;d.MipLevels=1;d.Format=format;d.SampleDesc.Count=1;
        d.Flags=target?D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET:D3D12_RESOURCE_FLAG_NONE;
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
        ComPtr<ID3D12Resource> result;
        check_hr(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            nullptr,IID_PPV_ARGS(&result)),"Create temporal image");return result;
    }
public:
    void setup(ID3D12Device* d,uint32_t w,uint32_t h,const char* shader,size_t length) {
        device=d;width=w;height=h;
        std::array<D3D12_DESCRIPTOR_RANGE,4> ranges{};
        std::array<D3D12_ROOT_PARAMETER,5> parameters{};
        for(unsigned i=0;i<4;++i) {
            ranges[i].RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SRV;ranges[i].NumDescriptors=1;ranges[i].BaseShaderRegister=i;
            parameters[i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            parameters[i].DescriptorTable.NumDescriptorRanges=1;parameters[i].DescriptorTable.pDescriptorRanges=&ranges[i];
            parameters[i].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
        }
        parameters[4].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameters[4].Constants.Num32BitValues=40;parameters[4].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
        std::array<D3D12_STATIC_SAMPLER_DESC,2> samplers{};
        for(unsigned i=0;i<2;++i) {
            auto& s=samplers[i];s.ShaderRegister=i;s.ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
            s.Filter=i?D3D12_FILTER_MIN_MAG_MIP_POINT:D3D12_FILTER_MIN_MAG_MIP_LINEAR;
            s.AddressU=s.AddressV=s.AddressW=D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            s.MaxLOD=D3D12_FLOAT32_MAX;s.ComparisonFunc=D3D12_COMPARISON_FUNC_ALWAYS;
        }
        D3D12_ROOT_SIGNATURE_DESC signature{};signature.NumParameters=5;signature.pParameters=parameters.data();
        signature.NumStaticSamplers=2;signature.pStaticSamplers=samplers.data();
        ComPtr<ID3DBlob> blob,error,vs,ps;
        check_hr(D3D12SerializeRootSignature(&signature,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error),"Serialize temporal signature");
        check_hr(device->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root)),"Create temporal signature");
        auto compile=[&](const char* entry,const char* profile,ComPtr<ID3DBlob>& output) {
            auto hr=D3DCompile(shader,length,nullptr,nullptr,nullptr,entry,profile,D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&output,&error);
            if(FAILED(hr)&&error)std::printf("Temporal shader: %s\n",(char*)error->GetBufferPointer());
            check_hr(hr,"Compile temporal shader");
        };
        compile("vs","vs_5_0",vs);compile("resolve","ps_5_0",ps);
        D3D12_GRAPHICS_PIPELINE_STATE_DESC state{};state.pRootSignature=root.Get();
        state.VS={vs->GetBufferPointer(),vs->GetBufferSize()};state.PS={ps->GetBufferPointer(),ps->GetBufferSize()};
        state.BlendState.IndependentBlendEnable=TRUE;
        for(unsigned i=0;i<2;++i)state.BlendState.RenderTarget[i].RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;
        state.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;state.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;
        state.RasterizerState.DepthClipEnable=TRUE;state.SampleMask=UINT_MAX;
        state.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;state.NumRenderTargets=2;
        state.RTVFormats[0]=DXGI_FORMAT_R16G16B16A16_FLOAT;state.RTVFormats[1]=DXGI_FORMAT_R32_FLOAT;state.SampleDesc.Count=1;
        check_hr(device->CreateGraphicsPipelineState(&state,IID_PPV_ARGS(&pipeline)),"Create temporal pipeline");
        D3D12_DESCRIPTOR_HEAP_DESC heap{};heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heap.NumDescriptors=12;heap.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        check_hr(device->CreateDescriptorHeap(&heap,IID_PPV_ARGS(&srvHeap)),"Create temporal source descriptors");
        heap={};heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;heap.NumDescriptors=8;
        check_hr(device->CreateDescriptorHeap(&heap,IID_PPV_ARGS(&rtvHeap)),"Create temporal target descriptors");
        srvStride=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        rtvStride=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        for(unsigned eye=0;eye<2;++eye)for(unsigned j=0;j<2;++j) {
            auto& image=eyes[eye].images[j];image.colour=makeImage(DXGI_FORMAT_R16G16B16A16_FLOAT,true);
            image.depth=makeImage(DXGI_FORMAT_R32_FLOAT,true);
            resourceView(image.colour.Get(),DXGI_FORMAT_R16G16B16A16_FLOAT,eye*6+2+j*2);
            resourceView(image.depth.Get(),DXGI_FORMAT_R32_FLOAT,eye*6+3+j*2);
            device->CreateRenderTargetView(image.colour.Get(),nullptr,rtv(eye*4+j*2));
            device->CreateRenderTargetView(image.depth.Get(),nullptr,rtv(eye*4+j*2+1));
        }
        std::printf("Temporal antialiasing: %ux%u per eye; independent colour/depth histories\n",w,h);
    }
    void setEnabled(bool value) {if(value!=enabled) {enabled=value;reset();}}
    void reset() {for(auto& eye:eyes)eye.valid=false;}
    void setView(unsigned eye,const TemporalView& view) {eyes[eye].current=view;}
    ID3D12Resource* resolvedResource(unsigned eye) {return eyes[eye].images[1-eyes[eye].writeIndex].colour.Get();}
    void captureDepth(unsigned index,ID3D12GraphicsCommandList* commands,ID3D12Resource* source,D3D12_RESOURCE_STATES state) {
        if(!enabled)return;
        auto& eye=eyes[index];auto desc=source->GetDesc();
        if(desc.Width!=width || desc.Height!=height)throw std::runtime_error("Temporal normal/depth dimensions differ from colour");
        if(!eye.normalDepth) {
            D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;desc.Flags=D3D12_RESOURCE_FLAG_NONE;
            check_hr(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                nullptr,IID_PPV_ARGS(&eye.normalDepth)),"Create temporal depth capture");
            resourceView(eye.normalDepth.Get(),desc.Format,index*6+1);
            std::printf("Temporal normal/depth capture: eye %u, DXGI format %u\n",index,desc.Format);
        }
        std::array<D3D12_RESOURCE_BARRIER,2> barriers{
            transition(source,state,D3D12_RESOURCE_STATE_COPY_SOURCE),
            transition(eye.normalDepth.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_DEST)};
        commands->ResourceBarrier(2,barriers.data());commands->CopyResource(eye.normalDepth.Get(),source);
        for(auto& b:barriers)std::swap(b.Transition.StateBefore,b.Transition.StateAfter);
        commands->ResourceBarrier(2,barriers.data());
    }
    D3D12_CPU_DESCRIPTOR_HANDLE resolve(unsigned index,ID3D12GraphicsCommandList* commands,ID3D12Resource* source) {
        auto format=source->GetDesc().Format;
        // The colour copy is typeless; sample the gamma-encoded source as sRGB.
        if(format==DXGI_FORMAT_R8G8B8A8_TYPELESS || format==DXGI_FORMAT_R8G8B8A8_UNORM)format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        if(format==DXGI_FORMAT_B8G8R8A8_TYPELESS || format==DXGI_FORMAT_B8G8R8A8_UNORM)format=DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        resourceView(source,format,index*6);
        auto& eye=eyes[index];if(!enabled) {eye.valid=false;return srvCpu(index*6);}
        if(!eye.normalDepth)throw std::runtime_error("Temporal resolve needs normal/depth capture");
        const auto& current=eye.current;const auto& previous=eye.previous;
        float delta[3];float distance2=0,rotationDelta=0;
        for(unsigned i=0;i<3;++i) {delta[i]=(float)(current.origin[i]-previous.origin[i]);distance2+=delta[i]*delta[i];}
        for(unsigned i=0;i<12;++i)rotationDelta=std::max(rotationDelta,std::abs(current.unjitteredVP[i]-previous.unjitteredVP[i]));
        bool valid=eye.valid && current.frame==previous.frame+1 && distance2<4 && rotationDelta<.8f;
        std::array<float,40> constants{};
        std::copy(current.inverseJitteredVP.begin(),current.inverseJitteredVP.end(),constants.begin());
        std::copy(previous.unjitteredVP.begin(),previous.unjitteredVP.end(),constants.begin()+16);
        for(unsigned i=0;i<3;++i)constants[32+i]=delta[i];constants[35]=valid?1:0;
        constants[36]=1.f/width;constants[37]=1.f/height;constants[38]=current.jitterX/width;constants[39]=current.jitterY/height;
        unsigned write=eye.writeIndex,read=1-write;
        auto& output=eye.images[write];
        std::array<D3D12_RESOURCE_BARRIER,2> barriers{
            transition(output.colour.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_RENDER_TARGET),
            transition(output.depth.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_RENDER_TARGET)};
        commands->ResourceBarrier(2,barriers.data());
        ID3D12DescriptorHeap* heaps[]={srvHeap.Get()};commands->SetDescriptorHeaps(1,heaps);
        commands->SetPipelineState(pipeline.Get());commands->SetGraphicsRootSignature(root.Get());
        commands->SetGraphicsRootDescriptorTable(0,srvGpu(index*6));
        commands->SetGraphicsRootDescriptorTable(1,srvGpu(index*6+1));
        commands->SetGraphicsRootDescriptorTable(2,srvGpu(index*6+2+read*2));
        commands->SetGraphicsRootDescriptorTable(3,srvGpu(index*6+3+read*2));
        commands->SetGraphicsRoot32BitConstants(4,40,constants.data(),0);
        std::array<D3D12_CPU_DESCRIPTOR_HANDLE,2> targets{rtv(index*4+write*2),rtv(index*4+write*2+1)};
        commands->OMSetRenderTargets(2,targets.data(),FALSE,nullptr);
        D3D12_VIEWPORT viewport{0,0,(float)width,(float)height,0,1};D3D12_RECT scissor{0,0,(LONG)width,(LONG)height};
        commands->RSSetViewports(1,&viewport);commands->RSSetScissorRects(1,&scissor);
        commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);commands->DrawInstanced(3,1,0,0);
        for(auto& b:barriers)std::swap(b.Transition.StateBefore,b.Transition.StateAfter);
        commands->ResourceBarrier(2,barriers.data());
        eye.previous=current;eye.valid=true;eye.writeIndex=read;
        return srvCpu(index*6+2+write*2);
    }
};
} // namespace dtvr
