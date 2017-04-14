﻿#pragma once

#include "Direct3DBase.h"
#include <d3d11.h>


struct ModelViewProjectionConstantBuffer
{
    DirectX::XMFLOAT4X4 model;
    DirectX::XMFLOAT4X4 view;
    DirectX::XMFLOAT4X4 projection;
};

struct Vertex	//Overloaded Vertex Structure
{
    Vertex(){}
    Vertex(float x, float y, float z,
        float u, float v)
        : pos(x,y,z), texCoord(u, v){}

    DirectX::XMFLOAT3 pos;
    DirectX::XMFLOAT2 texCoord;
};

// This class renders a simple quad.
ref class QuadRenderer sealed : public Direct3DBase
{
public:
    QuadRenderer();

    void Update(float timeTotal = 0.0f, float timeDelta = 0.0f);
    void CreateTextureFromByte(byte  *  buffer,int width,int height);

    // Direct3DBase methods.
    virtual void CreateDeviceResources() override;
    virtual void CreateWindowSizeDependentResources() override;
    virtual void Render() override;

private:
    void Render(Microsoft::WRL::ComPtr<ID3D11RenderTargetView> renderTargetView, Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depthStencilView);
    bool m_loadingComplete;
    uint32 m_indexCount;
    ModelViewProjectionConstantBuffer m_constantBufferData;
    Microsoft::WRL::ComPtr<ID3D11InputLayout>	m_inputLayout;
    Microsoft::WRL::ComPtr<ID3D11Buffer>		m_vertexBuffer;
    Microsoft::WRL::ComPtr<ID3D11Buffer>		m_indexBuffer;
    Microsoft::WRL::ComPtr<ID3D11VertexShader>	m_vertexShader;
    Microsoft::WRL::ComPtr<ID3D11PixelShader>	m_pixelShader;
    Microsoft::WRL::ComPtr<ID3D11Buffer>		m_constantBuffer;
    Microsoft::WRL::ComPtr<ID3D11Texture2D>		 m_Texture;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_SRV;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> m_QuadsTexSamplerState;
    Microsoft::WRL::ComPtr<ID3D11BlendState> m_Transparency;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> CCWcullMode;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> CWcullMode;
};
