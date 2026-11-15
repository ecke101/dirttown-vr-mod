#include "game_gpu.hpp"
#include <io.h>

using namespace dtvr;
int main(int argc,char**) {
    std::freopen("eye-transfer-test.log","w",stdout);_dup2(_fileno(stdout),_fileno(stderr));setvbuf(stdout,nullptr,_IONBF,0);
    try {
        ComPtr<ID3D12Device> device;check_hr(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)),"Create test GPU");
        ComPtr<ID3D12CommandQueue> queue;D3D12_COMMAND_QUEUE_DESC q{};
        check_hr(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)),"Create test queue");
        XrBridge xr;xr.useD3D11=argc>1;xr.format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;xr.renderWidth=48;xr.renderHeight=40;
        if(xr.useD3D11)xr.createD3D11Interop(device.Get(),queue.Get());
        std::array<ComPtr<ID3D12Resource>,2> targets;
        std::array<ComPtr<ID3D11Texture2D>,2> targets11;
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
        auto texture=[&](unsigned width,unsigned height,DXGI_FORMAT format,D3D12_RESOURCE_STATES state) {
            D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=width;d.Height=height;
            d.DepthOrArraySize=1;d.MipLevels=1;d.Format=format;d.SampleDesc.Count=1;d.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
            ComPtr<ID3D12Resource> image;check_hr(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,state,nullptr,IID_PPV_ARGS(&image)),"Create test image");return image;
        };
        for(unsigned i=0;i<2;++i) {
            auto& eye=xr.eyes[i];eye.width=i?48:32;eye.height=i?40:24;
            if(xr.useD3D11) {
                xr.createInteropTarget(device.Get(),eye);
                D3D11_TEXTURE2D_DESC desc{};desc.Width=eye.width;desc.Height=eye.height;desc.MipLevels=1;
                desc.ArraySize=1;desc.Format=xr.format;desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_DEFAULT;
                desc.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;
                check_hr(xr.device11->CreateTexture2D(&desc,nullptr,&targets11[i]),"Create fake D3D11 runtime image");
                eye.images11.resize(1,{XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});eye.images11[0].texture=targets11[i].Get();
            } else {
                targets[i]=texture(eye.width,eye.height,xr.format,D3D12_RESOURCE_STATE_COMMON);
                eye.images.resize(1,{XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR});eye.images[0].texture=targets[i].Get();
            }
        }
        auto source=texture(48,40,DXGI_FORMAT_R8G8B8A8_UNORM,D3D12_RESOURCE_STATE_RENDER_TARGET);
        ComPtr<ID3D12DescriptorHeap> rtv;D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;hd.NumDescriptors=1;
        check_hr(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&rtv)),"Create clear descriptor");
        auto handle=rtv->GetCPUDescriptorHandleForHeapStart();device->CreateRenderTargetView(source.Get(),nullptr,handle);
        ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> commands;
        check_hr(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)),"Create test allocator");
        check_hr(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&commands)),"Create test commands");
        EyeTransfer transfer;transfer.setup(queue.Get(),xr);transfer.setAntialiasing(false);
        const float red[]={1,0,0,1};commands->ClearRenderTargetView(handle,red,0,nullptr);
        transfer.capture(0,commands.Get(),source.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET);
        transfer.capture(1,commands.Get(),source.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET);
        check_hr(commands->Close(),"Close clear commands");ID3D12CommandList* lists[]={commands.Get()};queue->ExecuteCommandLists(1,lists);
        transfer.submit(xr);if(xr.useD3D11)xr.copyD3D11Eyes();
        ComPtr<ID3D12Fence> fence;check_hr(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)),"Create readback fence");
        HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);uint64_t sequence=0;
        for(unsigned i=0;i<2;++i) {
            const auto& eye=xr.eyes[i];unsigned char pixel[4]{};
            if(xr.useD3D11) {
                D3D11_TEXTURE2D_DESC desc{};targets11[i]->GetDesc(&desc);desc.Usage=D3D11_USAGE_STAGING;
                desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
                ComPtr<ID3D11Texture2D> staging;check_hr(xr.device11->CreateTexture2D(&desc,nullptr,&staging),"Create D3D11 staging image");
                xr.context11->CopyResource(staging.Get(),targets11[i].Get());
                D3D11_MAPPED_SUBRESOURCE mapped{};check_hr(xr.context11->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"Read D3D11 result");
                std::memcpy(pixel,static_cast<unsigned char*>(mapped.pData)+mapped.RowPitch*(eye.height/2)+4*(eye.width/2),4);
                xr.context11->Unmap(staging.Get(),0);
            } else {
                auto desc=targets[i]->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT64 bytes=0;
                device->GetCopyableFootprints(&desc,0,1,0,&footprint,nullptr,nullptr,&bytes);
                D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=bytes;buffer.Height=1;
                buffer.DepthOrArraySize=1;buffer.MipLevels=1;buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
                D3D12_HEAP_PROPERTIES readHeap{};readHeap.Type=D3D12_HEAP_TYPE_READBACK;ComPtr<ID3D12Resource> readback;
                check_hr(device->CreateCommittedResource(&readHeap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback)),"Create readback");
                check_hr(allocator->Reset(),"Reset readback allocator");check_hr(commands->Reset(allocator.Get(),nullptr),"Reset readback commands");
                auto b=transition(targets[i].Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_SOURCE);commands->ResourceBarrier(1,&b);
                D3D12_TEXTURE_COPY_LOCATION src{},dst{};src.pResource=targets[i].Get();src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                dst.pResource=readback.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=footprint;
                commands->CopyTextureRegion(&dst,0,0,0,&src,nullptr);std::swap(b.Transition.StateBefore,b.Transition.StateAfter);commands->ResourceBarrier(1,&b);
                check_hr(commands->Close(),"Close readback commands");queue->ExecuteCommandLists(1,lists);
                check_hr(queue->Signal(fence.Get(),++sequence),"Signal readback");
                check_hr(fence->SetEventOnCompletion(sequence,event),"Wait for readback");
                if(WaitForSingleObject(event,10000)!=WAIT_OBJECT_0)throw std::runtime_error("Readback timed out");
                void* mapped=nullptr;check_hr(readback->Map(0,nullptr,&mapped),"Map readback");
                std::memcpy(pixel,static_cast<unsigned char*>(mapped)+footprint.Footprint.RowPitch*(eye.height/2)+4*(eye.width/2),4);readback->Unmap(0,nullptr);
            }
            std::printf("Eye %u: %ux%u pixel %u,%u,%u,%u\n",i,eye.width,eye.height,pixel[0],pixel[1],pixel[2],pixel[3]);
            if(pixel[0]<250 || pixel[1]>2 || pixel[2]>2 || pixel[3]<250)throw std::runtime_error("Eye transfer pixel mismatch");
        }
        CloseHandle(event);std::printf("PASS: asymmetric eye transfer via %s\n",xr.useD3D11?(xr.on12?"D3D11On12":"D3D11 shared textures"):"D3D12");return 0;
    } catch(const std::exception& e) {std::printf("FAIL: %s\n",e.what());return 1;}
}
