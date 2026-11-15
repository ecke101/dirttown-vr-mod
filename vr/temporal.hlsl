// Reproject a separate, unjittered colour/depth history for each eye.
Texture2D<float4> currentColour : register(t0);
Texture2D<float4> normalDepth : register(t1);
Texture2D<float4> previousColour : register(t2);
Texture2D<float> previousDepth : register(t3);
SamplerState linearClamp : register(s0);
SamplerState pointClamp : register(s1);
cbuffer Temporal : register(b0) {
    column_major float4x4 inverseCurrentVP;
    column_major float4x4 previousVP;
    float4 originDeltaValid;
    float4 texelJitter; // reciprocal dimensions, current jitter in UV units
};
struct Vertex {float4 position:SV_Position;float2 uv:TEXCOORD;};
Vertex vs(uint id:SV_VertexID) {
    Vertex v;float2 p=float2((id<<1)&2,id&2);
    v.position=float4(p*float2(2,-2)+float2(-1,1),0,1);v.uv=p;return v;
}
struct Resolved {float4 colour:SV_Target0;float depth:SV_Target1;};
Resolved resolve(Vertex input) {
    float2 uv=input.uv+texelJitter.zw;
    float3 colour=currentColour.SampleLevel(linearClamp,uv,0).rgb;
    float depth=normalDepth.SampleLevel(pointClamp,uv,0).a;
    Resolved output;output.colour=float4(colour,1);output.depth=depth;
    [branch] if(originDeltaValid.w<0.5 || depth<=0.01 || depth>=100000)return output;
    // VP is camera-relative. For a RH perspective matrix, inverseVP * an NDC
    // point with w=1 has xyz equal to the ray at one metre of view-space depth.
    float2 ndc=float2(2*uv.x-1,1-2*uv.y);
    float3 ray=mul(inverseCurrentVP,float4(ndc,0.5,1)).xyz;
    float4 oldClip=mul(previousVP,float4(ray*depth+originDeltaValid.xyz,1));
    float2 oldUV=float2(oldClip.x,-oldClip.y)/max(oldClip.w,0.0001)*0.5+0.5;
    [branch] if(oldClip.w<=0 || any(oldUV<0) || any(oldUV>1))return output;
    float oldDepth=previousDepth.SampleLevel(pointClamp,oldUV,0);
    // Depth rejection prevents background history crossing a moving silhouette.
    float tolerance=max(0.04,0.015*oldClip.w);
    [branch] if(abs(oldDepth-oldClip.w)>tolerance)return output;
    float3 low=colour,high=colour;
    [unroll] for(int y=-1;y<=1;++y) [unroll] for(int x=-1;x<=1;++x) {
        float3 neighbour=currentColour.SampleLevel(linearClamp,uv+float2(x,y)*texelJitter.xy,0).rgb;
        low=min(low,neighbour);high=max(high,neighbour);
    }
    // Local colour clipping limits trails on objects without velocity vectors.
    float3 history=clamp(previousColour.SampleLevel(linearClamp,oldUV,0).rgb,low,high);
    float motion=length((oldUV-input.uv)/texelJitter.xy);
    float weight=lerp(0.88,0.65,saturate(motion/12));
    output.colour=float4(lerp(colour,history,weight),1);
    return output;
}
