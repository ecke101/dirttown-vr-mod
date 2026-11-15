#include "temporal.hpp"
#include "game_math.hpp"
#include "temporal_shader.hpp"
#include <io.h>

using namespace dtvr;
static float halfFloat(uint16_t h) {
    unsigned e=(h>>10)&31,m=h&1023;float v;
    if(!e)v=std::ldexp((float)m,-24);else v=std::ldexp(1.f+m/1024.f,(int)e-15);
    return (h&32768)?-v:v;
}
int main() {
    std::freopen("temporal-test.log","w",stdout);_dup2(_fileno(stdout),_fileno(stderr));setvbuf(stdout,nullptr,_IONBF,0);
    try {
        verifyCameraMath();std::puts("Camera, jitter and inverse-matrix checks: passed");
        ComPtr<ID3D12Device> device;check_hr(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)),"Create test device");
        ComPtr<ID3D12CommandQueue> queue;D3D12_COMMAND_QUEUE_DESC q{};q.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
        check_hr(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)),"Create test queue");
        ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> commands;ComPtr<ID3D12Fence> fence;
        check_hr(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)),"Create test allocator");
        check_hr(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&commands)),"Create test commands");
        check_hr(commands->Close(),"Close initial commands");check_hr(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)),"Create test fence");
        HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
        TemporalFilter filter;constexpr unsigned width=32,height=32;
        filter.setup(device.Get(),width,height,temporalShader,sizeof(temporalShader)-1);
        std::array<ComPtr<ID3D12Resource>,2> sources,uploads;
        std::array<float*,2> mapped{};
        D3D12_RESOURCE_DESC texture{};texture.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;texture.Width=width;texture.Height=height;
        texture.DepthOrArraySize=1;texture.MipLevels=1;texture.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;texture.SampleDesc.Count=1;
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT64 uploadBytes=0;
        device->GetCopyableFootprints(&texture,0,1,0,&footprint,nullptr,nullptr,&uploadBytes);
        for(unsigned i=0;i<2;++i) {
            D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
            check_hr(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&texture,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                nullptr,IID_PPV_ARGS(&sources[i])),"Create test input");
            D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=uploadBytes;buffer.Height=1;
            buffer.DepthOrArraySize=1;buffer.MipLevels=1;buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            heap.Type=D3D12_HEAP_TYPE_UPLOAD;
            check_hr(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,IID_PPV_ARGS(&uploads[i])),"Create test upload");
            check_hr(uploads[i]->Map(0,nullptr,reinterpret_cast<void**>(&mapped[i])),"Map test upload");
        }
        float minimum=1,maximum=0;
        for(unsigned frame=0;frame<18;++frame) {
            for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x) {
                unsigned offset=y*footprint.Footprint.RowPitch/4+x*4;
                float colour=((x+y+frame)&1)?.8f:.2f;
                for(unsigned c=0;c<3;++c)mapped[0][offset+c]=colour;
                mapped[0][offset+3]=1;
                mapped[1][offset]=0;mapped[1][offset+1]=1;mapped[1][offset+2]=0;
                mapped[1][offset+3]=frame>=16?2.f:10.f;
            }
            if(frame==17)filter.reset();
            check_hr(allocator->Reset(),"Reset test allocator");check_hr(commands->Reset(allocator.Get(),nullptr),"Reset test commands");
            for(unsigned i=0;i<2;++i) {
                auto b=transition(sources[i].Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_DEST);
                commands->ResourceBarrier(1,&b);
                D3D12_TEXTURE_COPY_LOCATION src{};src.pResource=uploads[i].Get();src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;src.PlacedFootprint=footprint;
                D3D12_TEXTURE_COPY_LOCATION dst{};dst.pResource=sources[i].Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                commands->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
                std::swap(b.Transition.StateBefore,b.Transition.StateAfter);commands->ResourceBarrier(1,&b);
            }
            auto camera=cameraMatrices({0,0,0,1},{-.8f,.9f,.6f,-.7f},.3f,2000);
            TemporalView view;view.unjitteredVP=camera.viewProjection;view.inverseJitteredVP=camera.inverseViewProjection;view.frame=frame;
            filter.setView(0,view);filter.captureDepth(0,commands.Get(),sources[1].Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            filter.resolve(0,commands.Get(),sources[0].Get());
            auto* resolved=filter.resolvedResource(0);auto desc=resolved->GetDesc();
            D3D12_PLACED_SUBRESOURCE_FOOTPRINT readFootprint{};UINT64 readBytes;
            device->GetCopyableFootprints(&desc,0,1,0,&readFootprint,nullptr,nullptr,&readBytes);
            D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=readBytes;buffer.Height=1;
            buffer.DepthOrArraySize=1;buffer.MipLevels=1;buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;ComPtr<ID3D12Resource> readback;
            check_hr(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,
                nullptr,IID_PPV_ARGS(&readback)),"Create test readback");
            auto b=transition(resolved,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE);
            commands->ResourceBarrier(1,&b);
            D3D12_TEXTURE_COPY_LOCATION src{};src.pResource=resolved;src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            D3D12_TEXTURE_COPY_LOCATION dst{};dst.pResource=readback.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=readFootprint;
            commands->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
            std::swap(b.Transition.StateBefore,b.Transition.StateAfter);commands->ResourceBarrier(1,&b);
            check_hr(commands->Close(),"Close temporal test commands");ID3D12CommandList* lists[]={commands.Get()};queue->ExecuteCommandLists(1,lists);
            check_hr(queue->Signal(fence.Get(),frame+1),"Signal temporal test");
            if(fence->GetCompletedValue()<frame+1) {check_hr(fence->SetEventOnCompletion(frame+1,event),"Wait for test GPU");
                if(WaitForSingleObject(event,10000)!=WAIT_OBJECT_0)throw std::runtime_error("Temporal test timed out");}
            const unsigned char* bytes=nullptr;check_hr(readback->Map(0,nullptr,(void**)&bytes),"Read temporal result");
            uint16_t value;std::memcpy(&value,bytes+16*readFootprint.Footprint.RowPitch+16*8,2);
            float colour=halfFloat(value);readback->Unmap(0,nullptr);
            std::printf("Frame %u: input %.3f, resolved %.5f\n",frame,frame&1?.8:.2,colour);
            if(frame>=8 && frame<16) {minimum=std::min(minimum,colour);maximum=std::max(maximum,colour);}
            if(frame>=16 && std::abs(colour-(frame&1?.8f:.2f))>.002f)throw std::runtime_error("Depth/reset history rejection failed");
        }
        CloseHandle(event);
        if(maximum-minimum>.15f)throw std::runtime_error("Temporal filter did not reduce alternating-pixel flicker");
        std::printf("PASS: raw flicker range 0.600; filtered %.5f. Depth rejection and history reset passed.\n",maximum-minimum);
        return 0;
    } catch(const std::exception& e) {std::printf("FAIL: %s\n",e.what());return 1;}
}
