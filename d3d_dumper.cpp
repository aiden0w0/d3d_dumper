#include "kiero/kiero.h"

#include <Windows.h>

#include "ImGui/imgui.h"
#include "ImGui/imgui_impl_dx11.h"
#include "ImGui/imgui_impl_win32.h"

#include <D3Dcompiler.h> //generateshader
#pragma comment(lib, "D3dcompiler.lib")
#include <d3d11.h>
#pragma comment(lib, "d3d11.lib")

#include <iostream>

#define GAME_WINDOW_CLASS_NAME  NULL
#define GAME_WINDOW_NAME		NULL

namespace d3d
{
	typedef HRESULT( __stdcall* d3d11_present ) ( IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags );
	typedef void( __stdcall* d3d11_draw_indexed ) ( ID3D11DeviceContext* pContext, UINT IndexCount, UINT StartIndexLocation, INT BaseVertexLocation );
	typedef void( __stdcall* d3d11_draw_indexed_instanced ) ( ID3D11DeviceContext* pContext, UINT IndexCountPerInstance, UINT InstanceCount, UINT StartIndexLocation, INT  BaseVertexLocation, UINT StartInstanceLocation );
	typedef void( __stdcall* d3d11_draw_indexed_instanced_indirect ) ( ID3D11DeviceContext* pContext, ID3D11Buffer* pBufferForArgs, UINT AlignedByteOffsetForArgs );
	typedef void( __stdcall* d3d11_create_query )( ID3D11Device* pDevice, const D3D11_QUERY_DESC* pQueryDesc, ID3D11Query** ppQuery );
	
	d3d11_present present = nullptr;
	d3d11_draw_indexed draw_indexed = nullptr;
	d3d11_draw_indexed_instanced draw_indexed_instanced = nullptr;
	d3d11_draw_indexed_instanced_indirect draw_indexed_instanced_indirect = nullptr;
	d3d11_create_query create_query = nullptr;

	bool initialized = false;
	ID3D11Device* device = nullptr;
	ID3D11DeviceContext* context = nullptr;
	IDXGISwapChain* swapchain = nullptr;
	ID3D11RenderTargetView* rtv = nullptr;

	float width = 0;
	float height = 0;

	// vertex
	ID3D11Buffer* ve_buffer;
	UINT ve_stride;
	UINT ve_offset;
	D3D11_BUFFER_DESC ve_desc;

	// index
	ID3D11Buffer* in_buffer;
	DXGI_FORMAT in_format;
	UINT in_offset;
	D3D11_BUFFER_DESC in_desc;

	// psgetConstantbuffers
	UINT psc_start;
	ID3D11Buffer* psc_buffer;
	D3D11_BUFFER_DESC psc_desc;

	// vsgetconstantbuffers
	UINT vsc_start;
	ID3D11Buffer* vsc_buffer;
	D3D11_BUFFER_DESC vsc_desc;

	//pssetshaderresources
	UINT pss_start;
	ID3D11Resource* pss_resource;
	D3D11_SHADER_RESOURCE_VIEW_DESC pss_sdesc;
	D3D11_TEXTURE2D_DESC pss_tdesc;

	ID3D11DepthStencilState* dss_off = nullptr;
	ID3D11DepthStencilState* dss = nullptr;

	ID3D11RasterizerState* dbs_off = nullptr;
	ID3D11RasterizerState* dbs_on = nullptr;

	ID3D11PixelShader* green_shader = nullptr;
	ID3D11PixelShader* magenta_shader = nullptr;
}

namespace menu
{
	int stride = -1;
	int index = -1;
	int inbw = -1;
	int vebw = -1;
	int pscbw = -1;
}

ID3D11Texture2D* texc = nullptr;
ID3D11ShaderResourceView* textureColor;
void GenerateTexture( uint32_t pixelcolor, DXGI_FORMAT format ) //DXGI_FORMAT_R32G32B32A32_FLOAT DXGI_FORMAT_R8G8B8A8_UNORM
{
	//static const uint32_t pixelcolor = 0xff00ff00; //0xff00ff00 green, 0xffff0000 blue, 0xff0000ff red
	D3D11_SUBRESOURCE_DATA initData = { &pixelcolor, sizeof( uint32_t ), 0 };

	D3D11_TEXTURE2D_DESC desc;
	desc.Width = 1;
	desc.Height = 1;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = format;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	desc.CPUAccessFlags = 0;
	desc.MiscFlags = 0;
	d3d::device->CreateTexture2D( &desc, &initData, &texc );

	D3D11_SHADER_RESOURCE_VIEW_DESC srdes;
	memset( &srdes, 0, sizeof( srdes ) );
	srdes.Format = format;
	srdes.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srdes.Texture2D.MostDetailedMip = 0;
	srdes.Texture2D.MipLevels = 1;
	d3d::device->CreateShaderResourceView( texc, &srdes, &textureColor );
}

HRESULT GenerateShader( ID3D11Device* pDevice, ID3D11PixelShader** pShader, float r, float g, float b )
{
	char szCast[] = "struct VS_OUT"
		"{"
		" float4 Position : SV_Position;"
		" float4 Color : COLOR0;"
		"};"

		"float4 main( VS_OUT input ) : SV_Target"
		"{"
		" float4 col;"
		" col.a = 1.0f;"
		" col.r = %f;"
		" col.g = %f;"
		" col.b = %f;"
		" return col;"
		"}";

	ID3D10Blob* pBlob;
	char szPixelShader[1000];

	sprintf_s( szPixelShader, szCast, r, g, b );

	ID3DBlob* error;

	HRESULT hr = D3DCompile( szPixelShader, sizeof( szPixelShader ), "shader", NULL, NULL, "main", "ps_4_0", NULL, NULL, &pBlob, &error );

	if ( FAILED( hr ) )
		return hr;

	hr = pDevice->CreatePixelShader( (DWORD*)pBlob->GetBufferPointer( ), pBlob->GetBufferSize( ), NULL, pShader );

	if ( FAILED( hr ) )
		return hr;

	return S_OK;
}

HRESULT __stdcall present( IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags )
{
	if ( !d3d::initialized )
	{
		const auto game_window = FindWindow( GAME_WINDOW_CLASS_NAME, GAME_WINDOW_NAME );
		
		if ( !game_window || !IsWindow( game_window ) ) 
			return d3d::present( pSwapChain, SyncInterval, Flags );

		if ( SUCCEEDED( pSwapChain->GetDevice( __uuidof( d3d::device ), (void**)&d3d::device ) ) )
			d3d::device->GetImmediateContext( &d3d::context );

		ID3D11Texture2D* buffer = nullptr;
		if ( SUCCEEDED( pSwapChain->GetBuffer( 0, __uuidof( ID3D11Texture2D ), reinterpret_cast<LPVOID*>( &buffer ) ) ) )
		{
			D3D11_RENDER_TARGET_VIEW_DESC desc;
			memset( &desc, 0, sizeof( desc ) );
			desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;

			d3d::device->CreateRenderTargetView( buffer, &desc, &d3d::rtv );

			D3D11_TEXTURE2D_DESC bd;
			memset( &bd, 0, sizeof( bd ) );
			buffer->GetDesc( &bd );

			d3d::width = static_cast<float>( bd.Width );
			d3d::height = static_cast<float>( bd.Height );

			buffer->Release( );
		}

		ImGui::CreateContext( );

		ImGui_ImplWin32_Init( game_window );
		ImGui_ImplDX11_Init( d3d::device, d3d::context );

		ImGuiIO& io = ImGui::GetIO( ); (void)io;
		io.IniFilename = NULL; // remove config file

		// Create depthstencil state
		D3D11_DEPTH_STENCIL_DESC depthStencilDesc;
		depthStencilDesc.DepthEnable = TRUE;
		depthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
		depthStencilDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;
		depthStencilDesc.StencilEnable = FALSE;
		depthStencilDesc.StencilReadMask = 0xFF;
		depthStencilDesc.StencilWriteMask = 0xFF;
		// Stencil operations if pixel is front-facing
		depthStencilDesc.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
		depthStencilDesc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_INCR;
		depthStencilDesc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
		depthStencilDesc.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
		// Stencil operations if pixel is back-facing
		depthStencilDesc.BackFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
		depthStencilDesc.BackFace.StencilDepthFailOp = D3D11_STENCIL_OP_DECR;
		depthStencilDesc.BackFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
		depthStencilDesc.BackFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
		d3d::device->CreateDepthStencilState( &depthStencilDesc, &d3d::dss_off );

		//create depthbias rasterizer state
		D3D11_RASTERIZER_DESC rasterizer_desc;
		ZeroMemory( &rasterizer_desc, sizeof( rasterizer_desc ) );
		rasterizer_desc.FillMode = D3D11_FILL_SOLID;
		rasterizer_desc.CullMode = D3D11_CULL_NONE; //D3D11_CULL_FRONT;
		rasterizer_desc.FrontCounterClockwise = false;
		float bias = 1000.0f;
		float bias_float = static_cast<float>( -bias );
		bias_float /= 10000.0f;
#define DEPTH_BIAS_D32_FLOAT(d) (d/(1/pow(2,23)))
		rasterizer_desc.DepthBias = DEPTH_BIAS_D32_FLOAT( *(DWORD*)&bias_float );
		rasterizer_desc.SlopeScaledDepthBias = 0.0f;
		rasterizer_desc.DepthBiasClamp = 0.0f;
		rasterizer_desc.DepthClipEnable = true;
		rasterizer_desc.ScissorEnable = false;
		rasterizer_desc.MultisampleEnable = false;
		rasterizer_desc.AntialiasedLineEnable = false;
		d3d::device->CreateRasterizerState( &rasterizer_desc, &d3d::dbs_off );

		//create normal rasterizer state
		D3D11_RASTERIZER_DESC nrasterizer_desc;
		ZeroMemory( &nrasterizer_desc, sizeof( nrasterizer_desc ) );
		nrasterizer_desc.FillMode = D3D11_FILL_SOLID;
		//nrasterizer_desc.CullMode = D3D11_CULL_BACK; //flickering
		nrasterizer_desc.CullMode = D3D11_CULL_NONE;
		nrasterizer_desc.FrontCounterClockwise = false;
		nrasterizer_desc.DepthBias = 0.0f;
		nrasterizer_desc.SlopeScaledDepthBias = 0.0f;
		nrasterizer_desc.DepthBiasClamp = 0.0f;
		nrasterizer_desc.DepthClipEnable = true;
		nrasterizer_desc.ScissorEnable = false;
		nrasterizer_desc.MultisampleEnable = false;
		nrasterizer_desc.AntialiasedLineEnable = false;
		d3d::device->CreateRasterizerState( &nrasterizer_desc, &d3d::dbs_on );

		// green
		GenerateTexture( 0xff00ff00, DXGI_FORMAT_R10G10B10A2_UNORM );

		// create shaders
		GenerateShader( d3d::device, &d3d::green_shader, 0.0f, 1.0f, 0.0f ); //green
		GenerateShader( d3d::device, &d3d::magenta_shader, 1.0f, 0.0f, 1.0f ); //magenta

		d3d::initialized = true;
	}

	d3d::context->OMSetRenderTargets( 1, &d3d::rtv, NULL );

	// handle input
	{
		if ( GetAsyncKeyState( VK_F1 ) & 1 )
		{
			if ( menu::stride > -1 ) menu::stride--;
		}
		if ( GetAsyncKeyState( VK_F2 ) & 1 )
		{
			if ( menu::stride < 148 ) menu::stride++;
		}
		if ( GetAsyncKeyState( VK_F3 ) & 1 )
		{
			if ( menu::index > -1 ) menu::index--;
		}
		if ( GetAsyncKeyState( VK_F4 ) & 1 )
		{
			if ( menu::index < 1036 ) menu::index++;
		}
		if ( GetAsyncKeyState( VK_F5 ) & 1 )
		{
			if ( menu::pscbw > -1 ) menu::pscbw--;
		}
		if ( GetAsyncKeyState( VK_F6 ) & 1 )
		{
			if ( menu::pscbw < 148 ) menu::pscbw++;
		}
		if ( GetAsyncKeyState( VK_F7 ) & 1 )
		{
			if ( menu::inbw > -1 ) menu::inbw--;
		}
		if ( GetAsyncKeyState( VK_F8 ) & 1 )
		{
			if ( menu::inbw < 148 ) menu::inbw++;
		}
		if ( GetAsyncKeyState( VK_F9 ) & 1 )
		{
			if ( menu::vebw > -1 ) menu::vebw--;
		}
		if ( GetAsyncKeyState( VK_F10 ) & 1 )
		{
			if ( menu::vebw < 148 ) menu::vebw++;
		}
	}

	ImGuiIO& io = ImGui::GetIO( ); (void)io;

	// Start the Dear ImGui Frame
	ImGui_ImplDX11_NewFrame( );
	ImGui_ImplWin32_NewFrame( );
	ImGui::NewFrame( );

	ImGui::PushStyleColor( ImGuiCol_WindowBg, ImVec4( 0.0f, 0.0f, 0.0f, 0.0f ) );
	ImGui::PushStyleVar( ImGuiStyleVar_FramePadding, ImVec2( 0.0f, 0.0f ) );
	ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, ImVec2( 1.0f, 1.0f ) );
	ImGui::PushStyleVar( ImGuiStyleVar_FrameBorderSize, 0.f );
	ImGui::PushStyleVar( ImGuiStyleVar_WindowBorderSize, 0.f );

	ImGui::Begin( "_", reinterpret_cast<bool*>( true ), ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings );

	ImGui::SetWindowPos( ImVec2( 0, 0 ), ImGuiCond_Always );
	ImGui::SetWindowSize( ImVec2( d3d::width, d3d::height ), ImGuiCond_Always );

	ImGui::Text( "FPS %1.f", io.Framerate );

	ImGui::Text( "stride %d", menu::stride );
	ImGui::Text( "index %d", menu::index );
	ImGui::Text( "pscbw %d", menu::pscbw );
	ImGui::Text( "inbw %d", menu::inbw );
	ImGui::Text( "vebw %d", menu::vebw );

	ImGui::GetWindowDrawList( )->PushClipRectFullScreen( );

	ImGui::End( );
	ImGui::PopStyleVar( 4 );
	ImGui::PopStyleColor( 1 );

	ImGui::Render( );
	ImGui_ImplDX11_RenderDrawData( ImGui::GetDrawData( ) );

    return d3d::present( pSwapChain, SyncInterval, Flags );
}

void __stdcall draw_indexed( ID3D11DeviceContext* pContext, UINT IndexCount, UINT StartIndexLocation, INT BaseVertexLocation )
{
	// std::cout << "draw_indexed" << std::endl;

	// get stride & vedesc.ByteWidth
	pContext->IAGetVertexBuffers( 0, 1, &d3d::ve_buffer, &d3d::ve_stride, &d3d::ve_offset );
	if ( d3d::ve_buffer != nullptr ) {
		d3d::ve_buffer->GetDesc( &d3d::ve_desc );
		d3d::ve_buffer->Release( );
		d3d::ve_buffer = nullptr;
	}

	// get indesc.ByteWidth (comment out if not used)
	pContext->IAGetIndexBuffer( &d3d::in_buffer, &d3d::in_format, &d3d::in_offset );
	if ( d3d::in_buffer != nullptr ) {
		d3d::in_buffer->GetDesc( &d3d::in_desc );
		d3d::in_buffer->Release( );
		d3d::in_buffer = nullptr;
	}

	//get pscdesc.ByteWidth (comment out if not used)
	pContext->PSGetConstantBuffers( d3d::psc_start, 1, &d3d::psc_buffer );
	if ( d3d::psc_buffer != nullptr ) {
		d3d::psc_buffer->GetDesc( &d3d::psc_desc );
		d3d::psc_buffer->Release( );
		d3d::psc_buffer = nullptr;
	}

	// get vscdesc.ByteWidth (comment out if not used)
	pContext->VSGetConstantBuffers( d3d::vsc_start, 1, &d3d::vsc_buffer );
	if ( d3d::vsc_buffer != nullptr ) {
		d3d::vsc_buffer->GetDesc( &d3d::vsc_desc );
		d3d::vsc_buffer->Release( );
		d3d::vsc_buffer = nullptr;
	}

	if ( menu::stride == d3d::ve_stride || menu::index == IndexCount / 100 ||
		menu::pscbw == d3d::psc_desc.ByteWidth / 10 || menu::inbw == d3d::in_desc.ByteWidth / 1000 || menu::vebw == d3d::ve_desc.ByteWidth / 10000 )
	{
		// depthstencil
		{
			pContext->OMGetDepthStencilState( &d3d::dss, 0 ); // get original
			pContext->OMSetDepthStencilState( d3d::dss_off, 0 ); // set depthstencil off

			pContext->PSSetShader( d3d::green_shader, NULL, NULL ); // chams

			d3d::draw_indexed( pContext, IndexCount, StartIndexLocation, BaseVertexLocation ); // redraw

			pContext->PSSetShader( d3d::magenta_shader, NULL, NULL );

			pContext->OMSetDepthStencilState( d3d::dss, 0 ); // set depthstencil back
			
			if ( d3d::dss ) {
				d3d::dss->Release( );
				d3d::dss = nullptr;
			}
		}		
	}

	d3d::draw_indexed( pContext, IndexCount, StartIndexLocation, BaseVertexLocation );
}

/*{
	pContext->RSSetState( d3d::dbs_off ); //depthbias off
	d3d::draw_indexed( pContext, IndexCount, StartIndexLocation, BaseVertexLocation ); //redraw
	pContext->RSSetState( d3d::dbs_on ); //depthbias true
}*/

void __stdcall draw_indexed_instanced( ID3D11DeviceContext* pContext, UINT IndexCountPerInstance, UINT InstanceCount, UINT StartIndexLocation, INT  BaseVertexLocation, UINT StartInstanceLocation )
{
	// std::cout << "draw_indexed_instanced" << std::endl;

	// get stride & vedesc.ByteWidth
	pContext->IAGetVertexBuffers( 0, 1, &d3d::ve_buffer, &d3d::ve_stride, &d3d::ve_offset );
	if ( d3d::ve_buffer != nullptr ) {
		d3d::ve_buffer->GetDesc( &d3d::ve_desc );
		d3d::ve_buffer->Release( );
		d3d::ve_buffer = nullptr;
	}

	// get indesc.ByteWidth (comment out if not used)
	pContext->IAGetIndexBuffer( &d3d::in_buffer, &d3d::in_format, &d3d::in_offset );
	if ( d3d::in_buffer != nullptr ) {
		d3d::in_buffer->GetDesc( &d3d::in_desc );
		d3d::in_buffer->Release( );
		d3d::in_buffer = nullptr;
	}

	//get pscdesc.ByteWidth (comment out if not used)
	pContext->PSGetConstantBuffers( d3d::psc_start, 1, &d3d::psc_buffer );
	if ( d3d::psc_buffer != nullptr ) {
		d3d::psc_buffer->GetDesc( &d3d::psc_desc );
		d3d::psc_buffer->Release( );
		d3d::psc_buffer = nullptr;
	}

	// get vscdesc.ByteWidth (comment out if not used)
	pContext->VSGetConstantBuffers( d3d::vsc_start, 1, &d3d::vsc_buffer );
	if ( d3d::vsc_buffer != nullptr ) {
		d3d::vsc_buffer->GetDesc( &d3d::vsc_desc );
		d3d::vsc_buffer->Release( );
		d3d::vsc_buffer = nullptr;
	}

	if ( menu::stride == d3d::ve_stride || menu::index == IndexCountPerInstance / 100 ||
		menu::pscbw == d3d::psc_desc.ByteWidth / 10 || menu::inbw == d3d::in_desc.ByteWidth / 1000 || menu::vebw == d3d::ve_desc.ByteWidth / 10000 )
	{
		// depthstencil
		{
			pContext->OMGetDepthStencilState( &d3d::dss, 0 ); // get original
			pContext->OMSetDepthStencilState( d3d::dss_off, 0 ); // set depthstencil off

			pContext->PSSetShader( d3d::green_shader, NULL, NULL ); // chams

			d3d::draw_indexed_instanced( pContext, IndexCountPerInstance, InstanceCount, StartIndexLocation, BaseVertexLocation, StartInstanceLocation ); // redraw

			pContext->PSSetShader( d3d::magenta_shader, NULL, NULL );

			pContext->OMSetDepthStencilState( d3d::dss, 0 ); // set depthstencil back

			if ( d3d::dss ) {
				d3d::dss->Release( );
				d3d::dss = nullptr;
			}
		}
	}

	d3d::draw_indexed_instanced( pContext, IndexCountPerInstance, InstanceCount, StartIndexLocation, BaseVertexLocation, StartInstanceLocation );
}

void __stdcall draw_indexed_instanced_indirect( ID3D11DeviceContext* pContext, ID3D11Buffer* pBufferForArgs, UINT AlignedByteOffsetForArgs )
{
	// std::cout << "draw_indexed_instanced_indirect" << std::endl;

	d3d::draw_indexed_instanced_indirect( pContext, pBufferForArgs, AlignedByteOffsetForArgs );
}

void __stdcall create_query( ID3D11Device* pDevice, const D3D11_QUERY_DESC* pQueryDesc, ID3D11Query** ppQuery )
{
	// Disable Occlusion which prevents rendering player models through certain objects (used by wallhack to see models through walls at all distances, REDUCES FPS)
	if ( pQueryDesc->Query == D3D11_QUERY_OCCLUSION )
	{
		D3D11_QUERY_DESC oqueryDesc = CD3D11_QUERY_DESC( );
		( &oqueryDesc )->MiscFlags = pQueryDesc->MiscFlags;
		( &oqueryDesc )->Query = D3D11_QUERY_TIMESTAMP;

		d3d::create_query( pDevice, &oqueryDesc, ppQuery );
		return;
	}

	d3d::create_query( pDevice, pQueryDesc, ppQuery );
	return;
}

namespace hook
{
	using namespace std;

    int init( )
    {
		cout << "init hook" << endl;

        if ( kiero::init( kiero::RenderType::D3D11 ) == kiero::Status::Success )
        {
			cout << " hook" << endl;

            kiero::bind( 8, (void**)&d3d::present, present );
			kiero::bind( 73, (void**)&d3d::draw_indexed, draw_indexed );
			kiero::bind( 81, (void**)&d3d::draw_indexed_instanced, draw_indexed_instanced );
			kiero::bind( 100, (void**)&d3d::draw_indexed_instanced_indirect, draw_indexed_instanced_indirect );

			kiero::bind( 42, (void**)&d3d::create_query, create_query );

			cout << " bind" << endl;
        }

        return 0;
    }
}


BOOL WINAPI DllMain( HINSTANCE hInstance, DWORD fdwReason, LPVOID )
{
    DisableThreadLibraryCalls( hInstance );

    switch ( fdwReason )
    {
    case DLL_PROCESS_ATTACH:
		AllocConsole( );
		freopen_s( reinterpret_cast<FILE**>( stdout ), "CONOUT$", "w", stdout );
        CreateThread( NULL, 0, (LPTHREAD_START_ROUTINE)hook::init, NULL, 0, NULL );
        break;
    }

    return TRUE;
}