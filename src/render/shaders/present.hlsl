// The game's picture letterboxed into the target: one quad from the vertex id (triangle strip of 4).
// Optional sharpening (Display > Sharpening): contrast-adaptive, after AMD FidelityFX CAS - each pixel against its four
// neighbours, the sharpening weight lowered where the neighbourhood already spans a large range (no halos at strong
// edges), so low-resolution textures gain detail without ringing.
cbuffer Params : register(b0)
{
    float4 rect;  // NDC: left, top, right, bottom
    float2 uvMax; // the used part of the texture
    float2 pad;
    float4 sharpen; // xy: one texel of the picture in uv, z: strength 0..1 (0: off), w: unused
};

struct VSOut
{
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};

VSOut vs_main(uint id : SV_VertexID)
{
    const float2 c = float2(id & 1, id >> 1);
    VSOut o;
    o.pos = float4(lerp(rect.x, rect.z, c.x), lerp(rect.y, rect.w, c.y), 0.0, 1.0);
    o.uv = c * uvMax;
    return o;
}

Texture2D picture : register(t0);
SamplerState pictureSampler : register(s0);

float4 ps_main(VSOut i) : SV_Target
{
    const float4 e = picture.Sample(pictureSampler, i.uv);
    if (sharpen.z <= 0.0)
        return e;
    const float2 t = sharpen.xy;
    const float3 b = picture.Sample(pictureSampler, i.uv + float2(0.0, -t.y)).rgb;
    const float3 d = picture.Sample(pictureSampler, i.uv + float2(-t.x, 0.0)).rgb;
    const float3 f = picture.Sample(pictureSampler, i.uv + float2(t.x, 0.0)).rgb;
    const float3 h = picture.Sample(pictureSampler, i.uv + float2(0.0, t.y)).rgb;
    const float3 mn = min(min(min(b, d), min(f, h)), e.rgb);
    const float3 mx = max(max(max(b, d), max(f, h)), e.rgb);
    // how much room there is to sharpen: small where the neighbourhood is near black or white or already contrasty
    const float3 amp = sqrt(saturate(min(mn, 1.0 - mx) / max(mx, 1.0 / 1024.0)));
    const float peak = -1.0 / lerp(8.0, 5.0, saturate(sharpen.z));
    const float3 w = amp * peak;
    const float3 outRgb = saturate((w * (b + d + f + h) + e.rgb) / (1.0 + 4.0 * w));
    return float4(outRgb, e.a);
}
