struct VSOutput
{
    float4 pos : SV_POSITION;
    float3 posW : WORLD;
    float3 nrm : NORMAL;
    float2 uvw : TEXCOORD0;
    float4 tan : TANGENT;
};

cbuffer MATRIX_VARS
{
    float4 lightDirection, lightColor, sunAmbient, camPos; // lighting info
    matrix worldMatrix, viewMatrix, projMatrix; // viewing info
};

Texture2D textures[] : register(t0, space1);
SamplerState samplers[] : register(s0, space1);

float4 main(VSOutput vOut) : SV_TARGET
{
    float4 textureColor = textures[2].Sample(samplers[0], vOut.uvw);
    float textureRoughness = textures[1].Sample(samplers[0], vOut.uvw).r;
    
    float4 diffuse = { 0.75f, 0.75f, 0.75f, 0.0f};
    float4 specular = { 1.0f, 1.0f, 1.0f, 1.0f };
    float4 ambient = { 0.1f, 0.1f, 0.1f, 1.0f };
    float4 emissive = { 0.0f, 0.0f, 0.0f, 1.0f };
    float Ns = 150.0f;
    
    float3 N = normalize(vOut.nrm);
    float3 lightDir = normalize(lightDirection);
    float diffuseTerm = saturate(dot(N, -lightDir));
    float4 resultColor = (diffuseTerm + ambient) * textureColor * lightColor;
    
    float3 viewDir = normalize(camPos.xyz - vOut.posW);
    float3 vecCalc = normalize(-lightDir.xyz + viewDir);
    float intensity = pow(saturate(dot(N, vecCalc)), Ns);
    float3 refl = lightColor.xyz * specular.xyz * (intensity * textureRoughness);
    resultColor += float4(refl, 1.0f);
    
	return resultColor;
}