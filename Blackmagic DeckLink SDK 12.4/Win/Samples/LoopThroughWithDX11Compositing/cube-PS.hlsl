/* -LICENSE-START-
** Copyright (c) 2015 Blackmagic Design
**  
** Permission is hereby granted, free of charge, to any person or organization 
** obtaining a copy of the software and accompanying documentation (the 
** "Software") to use, reproduce, display, distribute, sub-license, execute, 
** and transmit the Software, and to prepare derivative works of the Software, 
** and to permit third-parties to whom the Software is furnished to do so, in 
** accordance with:
** 
** (1) if the Software is obtained from Blackmagic Design, the End User License 
** Agreement for the Software Development Kit (“EULA”) available at 
** https://www.blackmagicdesign.com/EULA/DeckLinkSDK; or
** 
** (2) if the Software is obtained from any third party, such licensing terms 
** as notified by that third party,
** 
** and all subject to the following:
** 
** (3) the copyright notices in the Software and this entire statement, 
** including the above license grant, this restriction and the following 
** disclaimer, must be included in all copies of the Software, in whole or in 
** part, and all derivative works of the Software, unless such copies or 
** derivative works are solely in the form of machine-executable object code 
** generated by a source language processor.
** 
** (4) THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS 
** OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
** FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT 
** SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE 
** FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE, 
** ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER 
** DEALINGS IN THE SOFTWARE.
** 
** A copy of the Software is available free of charge at 
** https://www.blackmagicdesign.com/desktopvideo_sdk under the EULA.
** 
** -LICENSE-END-
*/
#include "cube.hlsli"

float4 rec709YCbCr2rgba(float Y, float Cb, float Cr, float a)
{
	float r, g, b;
	// Y: Undo 1/256 texture value scaling and scale [16..235] to [0..1] range
	// C: Undo 1/256 texture value scaling and scale [16..240] to [-0.5 .. + 0.5] range
	Y = (Y * 256.0 - 16.0) / 219.0;
	Cb = (Cb * 256.0 - 16.0) / 224.0 - 0.5;
	Cr = (Cr * 256.0 - 16.0) / 224.0 - 0.5;
	// Convert to RGB using Rec.709 conversion matrix (see eq 26.7 in Poynton 2003)
	r = Y + 1.5748 * Cr;
	g = Y - 0.1873 * Cb - 0.4681 * Cr;
	b = Y + 1.8556 * Cb;
	return float4(r, g, b, a);
}

// Perform bilinear interpolation between the provided components.
// The samples are expected as shown:
// ---------
// | X | Y |
// |---+---|
// | W | Z |
// ---------
float4 bilinear(float4 W, float4 X, float4 Y, float4 Z, float2 weight)
{
	float4 m0 = lerp(W, Z, weight.x);
	float4 m1 = lerp(X, Y, weight.x);
	return lerp(m0, m1, weight.y);
}

// Gather neighboring YUV macropixels from the given texture coordinate
void textureGatherYUV(Texture2D UYVYsampler, float2 tc, out float4 W, out float4 X, out float4 Y, out float4 Z)
{
	int2 ts;
	UYVYtex.GetDimensions(ts.x, ts.y);
	int2 tx = tc*ts;
	int2 tmin = {0, 0};
	int2 tmax = ts - int2(1,1);
	W = UYVYsampler.Load(int3(tx.xy, 0));
	X = UYVYsampler.Load(int3(clamp(tx + int3(0, 1, 0), tmin, tmax).xy, 0));
	Y = UYVYsampler.Load(int3(clamp(tx + int3(1, 1, 0), tmin, tmax).xy, 0));
	Z = UYVYsampler.Load(int3(clamp(tx + int3(1, 0, 0), tmin, tmax).xy, 0));
}

float4 main(VS_OUTPUT input) : SV_Target{
	/* The shader uses Texture2D::Load to obtain the YUV macropixels to avoid unwanted interpolation
	 * introduced by the GPU interpreting the YUV data as RGBA pixels.
	 * The YUV macropixels are converted into individual RGB pixels and bilinear interpolation is applied. */
	float2 tc = input.Tex.xy;
	float alpha = 0.7;
	uint w, h;
	UYVYtex.GetDimensions(w, h);

	float4 macro, macro_u, macro_r, macro_ur;
	float4 pixel, pixel_r, pixel_u, pixel_ur;
	textureGatherYUV(UYVYtex, tc, macro, macro_u, macro_ur, macro_r);

	//   Select the components for the bilinear interpolation based on the texture coordinate
	//   location within the YUV macropixel:
	//   -----------------          ----------------------
	//   | UY/VY | UY/VY |          | macro_u | macro_ur |
	//   |-------|-------|    =>    |---------|----------|
	//   | UY/VY | UY/VY |          | macro   | macro_r  |
	//   |-------|-------|          ----------------------
	//   | RG/BA | RG/BA |
	//   -----------------
	float2 off = frac(tc * float2(w, h));
	if (off.x > 0.5) {			// right half of macropixel
		pixel = rec709YCbCr2rgba(macro.a, macro.b, macro.r, alpha);
		pixel_r = rec709YCbCr2rgba(macro_r.g, macro_r.b, macro_r.r, alpha);
		pixel_u = rec709YCbCr2rgba(macro_u.a, macro_u.b, macro_u.r, alpha);
		pixel_ur = rec709YCbCr2rgba(macro_ur.g, macro_ur.b, macro_ur.r, alpha);
	} else {					// left half & center of macropixel
		pixel = rec709YCbCr2rgba(macro.g, macro.b, macro.r, alpha);
		pixel_r = rec709YCbCr2rgba(macro.a, macro.b, macro.r, alpha);
		pixel_u = rec709YCbCr2rgba(macro_u.g, macro_u.b, macro_u.r, alpha);
		pixel_ur = rec709YCbCr2rgba(macro_u.a, macro_u.b, macro_u.r, alpha);
	}

	return bilinear(pixel, pixel_u, pixel_ur, pixel_r, off);
}