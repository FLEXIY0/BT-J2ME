#version 450

/*
   CRT Tube — physically-motivated CRT model, fitted to a photo of a real
   CRT monitor: art at native resolution vs. a photo of the same frame on a
   real CRT; every parameter below was fitted numerically to that photo.

   For every screen pixel:
     1. each nearby source row is reconstructed horizontally by a Gaussian
        electron beam (per colour gun, with a small convergence error);
     2. each row is drawn as a scanline whose Gaussian height grows with
        brightness (bright lines bloom, dark lines are thin);
        the beam is normalised so the average brightness is preserved;
     3. an aperture-grille phosphor mask modulates R, G and B;
     4. a glow (halation) from a blurred copy of the picture is added.

   Copyright (C) 2026 crt-overlay contributors
   GNU General Public License v3 or later.
*/

layout(push_constant) uniform Push
{
	vec4 SourceSize;
	vec4 OutputSize;
	float TUBE_HBEAM;
	float TUBE_WMIN;
	float TUBE_WMAX;
	float TUBE_MASK_PITCH;
	float TUBE_MASK_STR;
	float TUBE_CONV_R;
	float TUBE_CONV_B;
	float TUBE_BRIGHT;
	float TUBE_GAIN_R;
	float TUBE_GAIN_B;
	float TUBE_GAMMA_IN;
	float TUBE_GAMMA_OUT;
	float TUBE_GLOW;
	float TUBE_GLOW_SIZE;
	float TUBE_SAT;
	float TUBE_MASK_PHASE;
	float TUBE_CHROMA;
	float TUBE_COLOR;
} params;

#pragma parameter TUBE_HBEAM "Horizontal beam width (source px)" 0.633 0.05 1.50 0.01
#pragma parameter TUBE_WMIN "Scanline width, dark (share of line)" 0.132 0.03 0.80 0.01
#pragma parameter TUBE_WMAX "Scanline width, bright (share of line)" 0.362 0.03 1.00 0.01
#pragma parameter TUBE_MASK_PITCH "Mask triad pitch (in scanlines)" 0.607 0.2 3.0 0.01
#pragma parameter TUBE_MASK_STR "Mask strength" 0.441 0.0 1.0 0.01
#pragma parameter TUBE_CONV_R "Convergence red (source px)" 0.157 -2.0 2.0 0.01
#pragma parameter TUBE_CONV_B "Convergence blue (source px)" -0.206 -2.0 2.0 0.01
#pragma parameter TUBE_BRIGHT "Brightness" 1.45 0.2 4.0 0.01
#pragma parameter TUBE_GAIN_R "Red gain" 1.05 0.5 1.5 0.01
#pragma parameter TUBE_GAIN_B "Blue gain" 1.158 0.5 1.5 0.01
#pragma parameter TUBE_GAMMA_IN "Gamma in" 2.8 1.0 4.0 0.01
#pragma parameter TUBE_GAMMA_OUT "Gamma out" 2.2 1.0 4.0 0.01
#pragma parameter TUBE_GLOW "Glow / halation" 0.0 0.0 2.0 0.01
#pragma parameter TUBE_GLOW_SIZE "Glow size (source px)" 0.5 0.5 8.0 0.05
#pragma parameter TUBE_SAT "Saturation" 0.95 0.0 2.5 0.01
#pragma parameter TUBE_MASK_PHASE "Mask phase (screen px)" 0.0 0.0 12.0 0.01
#pragma parameter TUBE_CHROMA "Colour smear: chroma beam width (source px)" 1.947 0.05 3.0 0.01
#pragma parameter TUBE_COLOR "Colour (chroma gain, like the TV knob)" 2.0 0.0 3.0 0.01

layout(std140, set = 0, binding = 0) uniform UBO
{
	mat4 MVP;
} global;

#pragma stage vertex
layout(location = 0) in vec4 Position;
layout(location = 1) in vec2 TexCoord;
layout(location = 0) out vec2 vTexCoord;

void main()
{
	gl_Position = global.MVP * Position;
	vTexCoord = TexCoord;
}

#pragma stage fragment
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 FragColor;
layout(set = 0, binding = 2) uniform sampler2D Source;

// Signal-domain (gamma-encoded) read of one source pixel, clamped to the edge.
vec3 fetch(vec2 texel)
{
	vec2 t = clamp(texel, vec2(0.0), params.SourceSize.xy - 1.0);
	return textureLod(Source, (t + 0.5) * params.SourceSize.zw, 0.0).rgb;
}

const mat3 RGB2YIQ = mat3(0.299, 0.596, 0.211,
                          0.587, -0.274, -0.523,
                          0.114, -0.322, 0.312);
const mat3 YIQ2RGB = mat3(1.0, 1.0, 1.0,
                          0.956, -0.272, -1.106,
                          0.621, -0.647, 1.703);

void main()
{
	vec2 src = vTexCoord * params.SourceSize.xy;      // position in source pixels
	vec2 px = vTexCoord * params.OutputSize.xy;       // position in screen pixels
	vec2 scale = params.OutputSize.xy * params.SourceSize.zw; // screen px per source px

	// Convergence error of the R and B guns, in source pixels.
	vec3 shift = vec3(params.TUBE_CONV_R, 0.0, params.TUBE_CONV_B);
	float hw = max(params.TUBE_HBEAM, 0.05);

	vec3 sum = vec3(0.0);
	float row0 = floor(src.y - 0.5);
	for (int dj = -1; dj <= 2; dj++) {
		float row = row0 + float(dj);
		// 1. Horizontal: luma (Y) through a narrow Gaussian beam, colour (IQ)
		//    through a wide one — composite video carries much less colour
		//    detail, so colours smear sideways. Evaluated at three positions
		//    for the R, G and B guns (convergence error).
		vec3 ySum = vec3(0.0), yW = vec3(0.0);
		vec3 iSum = vec3(0.0), qSum = vec3(0.0), cW = vec3(0.0);
		float hwc = max(params.TUBE_CHROMA, 0.05);
		float col0 = floor(src.x - 0.5);
		for (int di = -4; di <= 5; di++) {
			float col = col0 + float(di);
			vec3 yiq = RGB2YIQ * fetch(vec2(col, row));
			vec3 d = src.x - shift - (col + 0.5);
			vec3 ky = exp(-0.5 * (d / hw) * (d / hw));
			vec3 kc = exp(-0.5 * (d / hwc) * (d / hwc));
			ySum += ky * yiq.x; yW += ky;
			iSum += kc * yiq.y; qSum += kc * yiq.z; cW += kc;
		}
		vec3 Y = ySum / max(yW, vec3(1e-6));
		vec3 I = params.TUBE_COLOR * iSum / max(cW, vec3(1e-6));
		vec3 Q = params.TUBE_COLOR * qSum / max(cW, vec3(1e-6));
		vec3 h = vec3((YIQ2RGB * vec3(Y.r, I.r, Q.r)).r,
		              (YIQ2RGB * vec3(Y.g, I.g, Q.g)).g,
		              (YIQ2RGB * vec3(Y.b, I.b, Q.b)).b);
		h = pow(clamp(h, 0.0, 1.0), vec3(params.TUBE_GAMMA_IN));

		// 2. Scanline: Gaussian across the line, wider when brighter.
		float dy = src.y - (row + 0.5);
		vec3 w = params.TUBE_WMIN + (params.TUBE_WMAX - params.TUBE_WMIN) * sqrt(clamp(h, 0.0, 1.0));
		w = max(w, vec3(0.03));
		sum += h * exp(-0.5 * (dy * dy) / (w * w)) / (w * 2.5066283);
	}

	// 3. Aperture grille: R, G, B stripes as a smooth triad pattern.
	float pitch = max(params.TUBE_MASK_PITCH * scale.y, 1.0);
	float ph = 6.2831853 * (px.x - params.TUBE_MASK_PHASE) / pitch;
	vec3 mask = 1.0 + params.TUBE_MASK_STR * cos(ph - vec3(0.0, 2.0943951, 4.1887902));

	vec3 col = sum * mask * params.TUBE_BRIGHT * vec3(params.TUBE_GAIN_R, 1.0, params.TUBE_GAIN_B);

	// 4. Glow: Gaussian blur of the source (5x5 bilinear taps), added on top.
	if (params.TUBE_GLOW > 0.0) {
		float sigma = max(params.TUBE_GLOW_SIZE, 0.5);   // in source px
		vec3 g = vec3(0.0);
		float gw = 0.0;
		for (int gy = -2; gy <= 2; gy++) {
			for (int gx = -2; gx <= 2; gx++) {
				vec2 o = vec2(float(gx), float(gy)) * sigma;
				float k = exp(-0.5 * dot(o, o) / (sigma * sigma));
				vec3 t = textureLod(Source, vTexCoord + o * params.SourceSize.zw, 0.0).rgb;
				g += k * pow(max(t, vec3(0.0)), vec3(params.TUBE_GAMMA_IN));
				gw += k;
			}
		}
		col += params.TUBE_GLOW * g / gw;
	}

	float l = dot(col, vec3(0.2126, 0.7152, 0.0722));
	col = max(vec3(l) + (col - vec3(l)) * params.TUBE_SAT, vec3(0.0));
	FragColor = vec4(pow(clamp(col, 0.0, 1.0), vec3(1.0 / params.TUBE_GAMMA_OUT)), 1.0);
}
