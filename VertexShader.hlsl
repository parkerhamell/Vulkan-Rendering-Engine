#pragma pack_matrix(row_major)

struct SHADER_VARS
{
    float3 inputPos     : POSITION;
    float3 inputNrm     : NORMAL;
    float2 inputUV      : TEXCOORD0;
    float4 inputTangent : TANGENT;
};

cbuffer MATRIX_VARS
{
    float4 lightDirection, lightColor, sunAmbient, camPos; // lighting info
    matrix worldMatrix, viewMatrix, projMatrix;            // viewing info
};

struct VSOutput
{
    float4 pos  : SV_POSITION;
    float3 posW : WORLD;
    float3 nrm  : NORMAL;
    float2 uvw  : TEXCOORD0;
    float4 tan  : TANGENT;
};

VSOutput main(SHADER_VARS inputVertex : POSITION) : SV_POSITION
{
    
    float4 input = mul(float4(inputVertex.inputPos.xyz, 1), worldMatrix);
    float4 view = mul(input, viewMatrix);
    float4 viewProj = mul(view, projMatrix);
    
    VSOutput output;
    output.pos = viewProj;
    output.posW = input.xyz;
    output.nrm = normalize(mul((float3x3) worldMatrix, inputVertex.inputNrm));
    output.uvw = inputVertex.inputUV;
    output.tan = inputVertex.inputTangent;
    
    return output;
}